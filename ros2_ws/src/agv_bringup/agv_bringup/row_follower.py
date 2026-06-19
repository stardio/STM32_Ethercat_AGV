#!/usr/bin/env python3
"""
row_follower.py — 과수원 나무 열 추종 ROS2 노드

RealSense D435i depth image 기반 PID 중심선 추종 (AGV_DESIGN.md §5.4).

알고리즘:
  1. depth image scan_row(이미지 높이의 scan_row_ratio) 에서
     좌반부 / 우반부 median depth 계산
  2. error = d_left_m - d_right_m  (양수 = 좌측 나무가 가까움 → 우로 치우침)
  3. angular_z = -PID(error)
  4. 전방 center patch 10th-percentile < stop_distance_m → OBSTACLE(정지)
  5. 전방 center patch median > row_end_distance_m → ROW_END(정지)

상태 (State):
  DISABLED   /row_follow/enable = False 또는 초기
  FOLLOWING  정상 추종 중
  OBSTACLE   전방 장애물 감지 — /cmd_vel = 0
  ROW_END    열 끝 감지(전방 개방) — /cmd_vel = 0

토픽:
  Sub: /camera/aligned_depth_to_color/image_raw  sensor_msgs/Image (16UC1, mm)
       /row_follow/enable                         std_msgs/Bool
  Pub: /cmd_vel                                   geometry_msgs/Twist
       /row_follow/status                         std_msgs/String (JSON)
       /row_follow/debug_image                    sensor_msgs/Image (mono8, 구독자 있을 때만)

파라미터 (ros-args -p name:=value):
  depth_topic          (str,   '/camera/aligned_depth_to_color/image_raw')
  scan_row_ratio       (float, 0.60)   이미지 높이 중 어느 행을 스캔할지 (0~1)
  stop_distance_m      (float, 0.80)   전방 장애물 정지 거리 [m]
  row_end_distance_m   (float, 4.00)   전방 개방 → 열 끝 판정 거리 [m]
  max_linear_mps       (float, 0.40)   최대 전진 속도 [m/s]
  min_linear_mps       (float, 0.10)   최소 전진 속도 [m/s] (통로 좁을 때)
  max_angular_rps      (float, 0.60)   최대 회전 속도 [rad/s]
  kp                   (float, 0.50)   PID Proportional gain
  ki                   (float, 0.01)   PID Integral gain
  kd                   (float, 0.10)   PID Derivative gain
  min_valid_pixels     (int,   20)     유효 깊이 픽셀 최소 수
"""

import json
import time
from enum import IntEnum

import numpy as np

try:
    import rclpy
    from rclpy.node import Node
    from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSDurabilityPolicy
    from geometry_msgs.msg import Twist
    from sensor_msgs.msg import Image
    from std_msgs.msg import Bool, String
    _ROS2_AVAILABLE = True
except ImportError:
    _ROS2_AVAILABLE = False
    print("[row_follower] WARNING: rclpy not found — running in stub mode.")


class State(IntEnum):
    DISABLED         = 0
    FOLLOWING        = 1
    OBSTACLE         = 2   # depth 기반 전방 장애물
    ROW_END          = 3   # 열 끝 감지
    OBSTACLE_BLOCKED = 4   # obstacle_classifier(YOLOv8/Geofence) 에 의한 정지


class _PID:
    def __init__(self, kp: float, ki: float, kd: float,
                 integral_limit: float = 1.0):
        self.kp = kp
        self.ki = ki
        self.kd = kd
        self._integral   = 0.0
        self._prev_error = 0.0
        self._limit      = integral_limit

    def reset(self) -> None:
        self._integral   = 0.0
        self._prev_error = 0.0

    def update(self, error: float, dt: float) -> float:
        self._integral  = max(-self._limit,
                              min(self._limit,
                                  self._integral + error * dt))
        deriv           = (error - self._prev_error) / max(dt, 1e-4)
        self._prev_error = error
        return self.kp * error + self.ki * self._integral + self.kd * deriv


class RowFollowerNode(Node):

    def __init__(self):
        super().__init__('row_follower')

        # ── Parameters ────────────────────────────────────────────────────
        self.declare_parameter('depth_topic',
                               '/camera/aligned_depth_to_color/image_raw')
        self.declare_parameter('scan_row_ratio',     0.60)
        self.declare_parameter('stop_distance_m',    0.80)
        self.declare_parameter('row_end_distance_m', 4.00)
        self.declare_parameter('max_linear_mps',     0.40)
        self.declare_parameter('min_linear_mps',     0.10)
        self.declare_parameter('max_angular_rps',    0.60)
        self.declare_parameter('kp',                 0.50)
        self.declare_parameter('ki',                 0.01)
        self.declare_parameter('kd',                 0.10)
        self.declare_parameter('min_valid_pixels',   20)

        depth_topic        = self.get_parameter('depth_topic').value
        self._scan_ratio   = float(self.get_parameter('scan_row_ratio').value)
        self._stop_mm      = float(self.get_parameter('stop_distance_m').value)    * 1000.0
        self._row_end_mm   = float(self.get_parameter('row_end_distance_m').value) * 1000.0
        self._max_lin      = float(self.get_parameter('max_linear_mps').value)
        self._min_lin      = float(self.get_parameter('min_linear_mps').value)
        self._max_ang      = float(self.get_parameter('max_angular_rps').value)
        self._min_valid    = int(self.get_parameter('min_valid_pixels').value)

        kp = float(self.get_parameter('kp').value)
        ki = float(self.get_parameter('ki').value)
        kd = float(self.get_parameter('kd').value)
        self._pid = _PID(kp, ki, kd, integral_limit=1.0)

        self._state            = State.DISABLED
        self._enabled          = False
        self._prev_t           = time.monotonic()
        self._enc_warn         = False  # encoding warning 한 번만 출력
        self._obstacle_action  = 'CLEAR'  # obstacle_classifier 에서 수신

        # ── Publishers / Subscribers ───────────────────────────────────────
        self._pub_cmd    = self.create_publisher(Twist,  '/cmd_vel',               10)
        self._pub_status = self.create_publisher(String, '/row_follow/status',     10)
        self._pub_debug  = self.create_publisher(Image,  '/row_follow/debug_image', 5)

        # RealSense depth 토픽은 BEST_EFFORT QoS로 발행됨 — 동일하게 맞춰야 수신됨
        _depth_qos = QoSProfile(
            depth=5,
            reliability=QoSReliabilityPolicy.BEST_EFFORT,
            durability=QoSDurabilityPolicy.VOLATILE,
        )
        self._sub_depth    = self.create_subscription(
            Image, depth_topic, self._on_depth, _depth_qos)
        self._sub_enable   = self.create_subscription(
            Bool, '/row_follow/enable', self._on_enable, 10)
        self._sub_obstacle = self.create_subscription(
            String, '/obstacle/action', self._on_obstacle_action, 10)

        self.get_logger().info(
            f"row_follower ready — depth={depth_topic}  "
            f"stop={self._stop_mm/1000:.2f}m  row_end={self._row_end_mm/1000:.2f}m  "
            f"pid=({kp:.2f},{ki:.3f},{kd:.2f})"
        )

    # ── Obstacle action from obstacle_classifier ─────────────────────────

    def _on_obstacle_action(self, msg: String) -> None:
        self._obstacle_action = msg.data

    # ── Enable / Disable ─────────────────────────────────────────────────

    def _on_enable(self, msg: Bool) -> None:
        was_enabled    = self._enabled
        self._enabled  = bool(msg.data)
        if not self._enabled:
            self._state = State.DISABLED
            self._pid.reset()
            self._send_cmd(0.0, 0.0)
            self._pub_status_msg(0.0, 0.0, 0.0)  # UI에 DISABLED 즉시 통보
            if was_enabled:
                self.get_logger().info("row_follower DISABLED")
        else:
            self.get_logger().info("row_follower ENABLED")

    # ── Depth callback ────────────────────────────────────────────────────

    def _on_depth(self, msg: Image) -> None:
        if not self._enabled:
            return

        now = time.monotonic()
        dt  = max(now - self._prev_t, 1e-3)
        self._prev_t = now

        if msg.encoding != '16UC1':
            if not self._enc_warn:
                self.get_logger().warning(
                    f"Unexpected depth encoding '{msg.encoding}' — expected 16UC1")
                self._enc_warn = True
            return

        H, W = msg.height, msg.width
        depth = np.frombuffer(bytes(msg.data), dtype=np.uint16).reshape(H, W)

        # ── 1. 전방 장애물 체크 (중앙 1/3 패치) ──────────────────────────
        r0 = H // 3
        r1 = H * 2 // 3
        c0 = W // 3
        c1 = W * 2 // 3
        center = depth[r0:r1, c0:c1]
        valid_center = center[center > 0]

        if len(valid_center) >= 10:
            front_p10_mm  = float(np.percentile(valid_center, 10))
            front_med_mm  = float(np.median(valid_center))
        else:
            front_p10_mm  = 9999.0
            front_med_mm  = 9999.0

        if front_p10_mm < self._stop_mm:
            self._transition(State.OBSTACLE)
            self._send_cmd(0.0, 0.0)
            self._pub_status_msg(front_p10_mm / 1000.0, 0.0, 0.0)
            return

        # ── 2. 열 끝 감지 ────────────────────────────────────────────────
        if front_med_mm > self._row_end_mm or front_med_mm == 0.0:
            self._transition(State.ROW_END)
            self._send_cmd(0.0, 0.0)
            self._pub_status_msg(front_med_mm / 1000.0, 0.0, 0.0)
            return

        # ── 3. 횡방향 오차 계산 ──────────────────────────────────────────
        scan_row = max(0, min(H - 1, int(H * self._scan_ratio)))
        row_data = depth[scan_row, :].astype(np.float32)

        left_valid  = row_data[: W // 2][row_data[: W // 2] > 0]
        right_valid = row_data[W // 2 :][row_data[W // 2 :] > 0]

        if len(left_valid) < self._min_valid or len(right_valid) < self._min_valid:
            # 유효 픽셀 부족 → 직진 유지 (최소 속도)
            self._transition(State.FOLLOWING)
            self._send_cmd(self._min_lin, 0.0)
            self._pub_status_msg(front_p10_mm / 1000.0, 0.0, 0.0)
            return

        d_left_m  = float(np.median(left_valid))  / 1000.0
        d_right_m = float(np.median(right_valid)) / 1000.0

        # error > 0 : 좌측 나무가 가까움 → 우로 치우침 → 좌로 회전 필요
        error   = d_left_m - d_right_m

        angular = -self._pid.update(error, dt)
        angular = max(-self._max_ang, min(self._max_ang, angular))

        # 통로 평균 폭으로 속도 조절: 평균 2 m 이상이면 최대 속도
        gap_avg_m = (d_left_m + d_right_m) / 2.0
        lin_scale = min(1.0, gap_avg_m / 2.0)
        linear    = self._min_lin + (self._max_lin - self._min_lin) * lin_scale

        # ── 4. obstacle_classifier(YOLOv8/Geofence) 안전 override ────────
        obs = self._obstacle_action
        if obs in ('FULL_STOP', 'WAIT', 'SLOW_STOP'):
            self._transition(State.OBSTACLE_BLOCKED)
            self._pid.reset()
            self._send_cmd(0.0, 0.0)
            self._pub_status_msg(front_p10_mm / 1000.0, error, 0.0,
                                 d_left_m, d_right_m)
            return
        if obs == 'SLOW_AVOID':
            linear = min(linear, self._min_lin)  # 최소 속도로 감속

        self._transition(State.FOLLOWING)
        self._send_cmd(linear, angular)
        self._pub_status_msg(front_p10_mm / 1000.0, error, angular,
                             d_left_m, d_right_m)
        self._pub_debug_image(depth, scan_row, H, W)

    # ── Helpers ───────────────────────────────────────────────────────────

    def _transition(self, new_state: State) -> None:
        if self._state != new_state:
            self.get_logger().info(
                f"row_follower: {self._state.name} → {new_state.name}")
            self._state = new_state

    def _send_cmd(self, linear: float, angular: float) -> None:
        msg = Twist()
        msg.linear.x  = float(linear)
        msg.angular.z = float(angular)
        self._pub_cmd.publish(msg)

    def _pub_status_msg(self, front_m: float, error: float, angular: float,
                        d_left_m: float = 0.0, d_right_m: float = 0.0) -> None:
        data = {
            'state':       self._state.name,
            'front_m':     round(front_m,   3),
            'error_m':     round(error,     4),
            'angular_rps': round(angular,   4),
            'd_left_m':    round(d_left_m,  3),
            'd_right_m':   round(d_right_m, 3),
        }
        self._pub_status.publish(String(data=json.dumps(data)))

    def _pub_debug_image(self, depth: np.ndarray,
                         scan_row: int, H: int, W: int) -> None:
        if self._pub_debug.get_subscription_count() == 0:
            return
        vis = np.clip(depth.astype(np.float32), 0.0, 5000.0)
        vis = (vis / 5000.0 * 255.0).astype(np.uint8)
        vis[scan_row, :] = 255  # 스캔 행을 흰색으로 표시
        img = Image()
        img.height   = H
        img.width    = W
        img.encoding = 'mono8'
        img.step     = W
        img.data     = vis.tobytes()
        self._pub_debug.publish(img)


# ── Entry point ───────────────────────────────────────────────────────────────

def main(args=None):
    if not _ROS2_AVAILABLE:
        print("rclpy not available — cannot run.")
        return
    rclpy.init(args=args)
    node = RowFollowerNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
