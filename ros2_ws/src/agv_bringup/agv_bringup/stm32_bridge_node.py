#!/usr/bin/env python3
"""
stm32_bridge_node.py — ROS2 node that bridges the STM32 AGV firmware to ROS2.

Architecture
────────────
  WebSocket client → bridge.py (already running)
    ├─ subscribes to /cmd_vel (geometry_msgs/Twist)
    │    → sends {"cmd":"agv_velocity", ...} to bridge WebSocket
    ├─ receives agv_odometry packets from bridge
    │    → publishes /odom (nav_msgs/Odometry)  +  tf odom→base_link
    ├─ receives agv_status packets from bridge
    │    → publishes /agv_status (std_msgs/String JSON)
    └─ receives param_report from bridge
         → auto-updates unit_scale_left / unit_scale_right

Usage
─────
  # 1. Start bridge.py (serial ↔ WebSocket)
  python bridge.py --port /dev/ttyACM0

  # 2. Run this node (x86 PC or Jetson, ROS2 Humble)
  ros2 run agv_bringup stm32_bridge_node
  # or directly:
  python stm32_bridge_node.py

Parameters (set via --ros-args -p name:=value)
──────────────────────────────────────────────
  ws_url             (str,   default "ws://localhost:8765")  bridge WebSocket URL
  base_frame         (str,   default "base_link")
  odom_frame         (str,   default "odom")
  wheel_base         (float, default 0.60)  [m]  — must match AGV_WHEEL_BASE_M in firmware
  wheel_radius       (float, default 0.15)  [m]  — must match AGV_WHEEL_RADIUS_M in firmware
  unit_scale_left    (float, default 1.0)   HW counts/mm for J1 (left wheel)
  unit_scale_right   (float, default 1.0)   HW counts/mm for J2 (right wheel)
  (Both are auto-updated when a param_report is received from the bridge.)

Requirements
────────────
  pip install websockets rclpy
  (ROS2 Humble must be sourced)
"""

import asyncio
import json
import math
import os
import threading
import time
from typing import Optional

_MAP_PATH_CACHE = os.path.expanduser('~/.ros/rtabmap_path_cache.json')

try:
    import rclpy
    from rclpy.node import Node
    from rclpy.parameter import Parameter
    from geometry_msgs.msg import Twist, TransformStamped, PoseStamped
    from nav_msgs.msg import Odometry, Path
    from std_msgs.msg import String, Bool, Float32
    import tf2_ros
    _ROS2_AVAILABLE = True
    try:
        from geometry_msgs.msg import PoseWithCovarianceStamped
        _HAS_POSE_COV = True
    except ImportError:
        _HAS_POSE_COV = False
    try:
        from nav2_msgs.action import NavigateThroughPoses
        from rclpy.action import ActionClient
        _HAS_NTP = True
    except ImportError:
        _HAS_NTP = False
    try:
        from sensor_msgs.msg import NavSatFix
        _HAS_NAVSATFIX = True
    except ImportError:
        _HAS_NAVSATFIX = False
except ImportError:
    _ROS2_AVAILABLE = False
    _HAS_POSE_COV   = False
    _HAS_NTP        = False
    _HAS_NAVSATFIX  = False
    print("[stm32_bridge_node] WARNING: rclpy not found — running in stub mode (no ROS2).")

try:
    import websockets
    _WS_AVAILABLE = True
except ImportError:
    _WS_AVAILABLE = False
    print("[stm32_bridge_node] WARNING: websockets not found — pip install websockets")


# ── Standalone helpers (no ROS2 dependency) ───────────────────────────────────

def _now_sec() -> float:
    return time.monotonic()


class _OdomIntegrator:
    """
    Integrates wheel encoder counts into (x, y, theta) using differential drive.
    Thread-safe via a simple lock.
    """

    def __init__(self, wheel_base: float, unit_scale_left: float, unit_scale_right: float):
        self.wheel_base   = wheel_base
        self.scale_left   = unit_scale_left   # HW counts per mm
        self.scale_right  = unit_scale_right
        self.x     = 0.0
        self.y     = 0.0
        self.theta = 0.0
        self._prev_left:  Optional[int] = None
        self._prev_right: Optional[int] = None
        self._lock = threading.Lock()

    def reset(self) -> None:
        with self._lock:
            self.x = self.y = self.theta = 0.0
            self._prev_left = self._prev_right = None

    def update(self, pos_left_hw: int, pos_right_hw: int) -> tuple:
        """
        Feed raw encoder counts. Returns (x, y, theta, dist_m, dtheta_rad).
        """
        with self._lock:
            if self._prev_left is None:
                self._prev_left  = pos_left_hw
                self._prev_right = pos_right_hw
                return (self.x, self.y, self.theta, 0.0, 0.0)

            dl_m = (pos_left_hw  - self._prev_left)  / max(self.scale_left,  1.0) / 1000.0
            dr_m = (pos_right_hw - self._prev_right) / max(self.scale_right, 1.0) / 1000.0

            self._prev_left  = pos_left_hw
            self._prev_right = pos_right_hw

            dist   = (dl_m + dr_m) / 2.0
            dtheta = (dr_m - dl_m) / self.wheel_base

            self.theta += dtheta
            self.x     += dist * math.cos(self.theta)
            self.y     += dist * math.sin(self.theta)

            return (self.x, self.y, self.theta, dist, dtheta)


# ── ROS2 Node ─────────────────────────────────────────────────────────────────

class Stm32BridgeNode(Node):

    def __init__(self):
        super().__init__("stm32_bridge_node")

        # Parameters
        self.declare_parameter("ws_url",            "ws://localhost:8765")
        self.declare_parameter("base_frame",        "base_link")
        self.declare_parameter("odom_frame",        "odom")
        self.declare_parameter("wheel_base",        0.60)
        self.declare_parameter("wheel_radius",      0.15)
        self.declare_parameter("unit_scale_left",   1.0)
        self.declare_parameter("unit_scale_right",  1.0)

        ws_url     = self.get_parameter("ws_url").value
        wheel_base = float(self.get_parameter("wheel_base").value)
        sl         = float(self.get_parameter("unit_scale_left").value)
        sr         = float(self.get_parameter("unit_scale_right").value)

        self.base_frame = self.get_parameter("base_frame").value
        self.odom_frame = self.get_parameter("odom_frame").value

        # Odometry integrator
        self._odom = _OdomIntegrator(wheel_base, sl, sr)
        self._last_vel_left_hw  = 0
        self._last_vel_right_hw = 0
        self._last_rf_relay_t   = 0.0  # row_follow_status 릴레이 쓰로틀 타임스탬프

        # ROS2 publishers
        self._pub_odom       = self.create_publisher(Odometry,   "/odom",               10)
        self._pub_status     = self.create_publisher(String,    "/agv_status",         10)
        self._pub_run_enable = self.create_publisher(Bool,      "/agv_run_enable_state", 10)
        self._pub_row_enable = self.create_publisher(Bool,      "/row_follow/enable",  10)
        self._pub_row_speed  = self.create_publisher(Float32,   "/row_follow/speed",   10)
        self._pub_goal       = self.create_publisher(PoseStamped, "/goal_pose",        10)

        self._sub_row_status = self.create_subscription(
            String, "/row_follow/status", self._on_row_follow_status, 10)

        # Nav2 localization status (RTAB-Map이 맵에서 위치를 인식할 때 발행)
        self._last_loc_time = 0.0
        if _HAS_POSE_COV:
            self._sub_loc = self.create_subscription(
                PoseWithCovarianceStamped,
                "/rtabmap/localization_pose",
                self._on_loc_pose, 10)
        self.create_timer(2.0, self._nav_status_timer)  # 2초마다 loc 상태 브로드캐스트

        # RTAB-Map mapPath 구독 (맵 프레임 경로 오버레이용)
        self._last_map_path_relay_t = 0.0
        self._force_map_path_relay  = False   # request_map_path WS 커맨드로 즉시 전송 요청
        self._last_map_path_payload = {"poses": [], "total": 0}
        self._sub_map_path = self.create_subscription(
            Path, "/rtabmap/mapPath", self._on_map_path, 1)

        # 경로 주행 (navigate_through_poses 또는 sequential fallback)
        self._loc_x = 0.0; self._loc_y = 0.0; self._loc_theta = 0.0
        self._route_active  = False
        self._route_total   = 0
        self._route_handle  = None   # Nav2 goal handle
        # Sequential fallback state
        self._route_waypoints: list = []
        self._route_idx       = 0
        self._route_seq_timer_handle = self.create_timer(0.5, self._route_seq_timer)
        # Nav2 NavigateThroughPoses 액션 클라이언트
        if _HAS_NTP and _ROS2_AVAILABLE:
            self._ntp_client = ActionClient(self, NavigateThroughPoses,
                                            'navigate_through_poses')
        else:
            self._ntp_client = None

        # TF broadcaster
        self._tf_broadcaster = tf2_ros.TransformBroadcaster(self)

        # ROS2 subscribers
        self._sub_cmd_vel = self.create_subscription(
            Twist, "/cmd_vel", self._on_cmd_vel, 10)
        self._sub_run_enable = self.create_subscription(
            Bool, "/agv_run_enable", self._on_run_enable, 10)

        # GPS 구독 (gps.launch.py 실행 시 자동 연결, 미실행 시 토픽 없음 — 에러 없음)
        self._last_gps_fix_t = 0.0   # gps_fix 1Hz 쓰로틀
        if _HAS_NAVSATFIX and _ROS2_AVAILABLE:
            self._sub_gps_fix = self.create_subscription(
                NavSatFix, "/gps/fix", self._on_gps_fix, 10)
            self._sub_gps_odom = self.create_subscription(
                Odometry, "/odometry/gps", self._on_gps_odometry, 10)

        # WebSocket thread
        self._ws_url    = ws_url
        self._ws: Optional[object] = None
        self._ws_loop   = asyncio.new_event_loop()
        self._ws_thread = threading.Thread(target=self._ws_thread_main, daemon=True)
        self._ws_thread.start()

        self.get_logger().info(
            f"stm32_bridge_node started — ws={ws_url}  "
            f"wheel_base={wheel_base}m  scale_L={sl}  scale_R={sr}"
        )

    # ── /cmd_vel → WebSocket ─────────────────────────────────────────────────

    def _on_cmd_vel(self, msg: Twist) -> None:
        cmd = json.dumps({
            "cmd":         "agv_velocity",
            "linear_mps":  msg.linear.x,
            "angular_rps": msg.angular.z,
        })
        asyncio.run_coroutine_threadsafe(self._ws_send(cmd), self._ws_loop)

    # ── /agv_run_enable → WebSocket ──────────────────────────────────────────

    def _on_run_enable(self, msg: Bool) -> None:
        cmd = json.dumps({"cmd": "run_enable", "axis": 255, "enable": msg.data})
        asyncio.run_coroutine_threadsafe(self._ws_send(cmd), self._ws_loop)

    # ── GPS → WebSocket ──────────────────────────────────────────────────────

    def _on_gps_fix(self, msg) -> None:
        now = time.monotonic()
        if now - self._last_gps_fix_t < 1.0:   # 1Hz 쓰로틀 (lat/lon은 빨리 안 변함)
            return
        self._last_gps_fix_t = now
        cov = msg.position_covariance[0] if msg.position_covariance_type > 0 else -1
        sigma = round(cov ** 0.5, 2) if cov >= 0 else -1
        asyncio.run_coroutine_threadsafe(self._ws_send(json.dumps({
            "type":    "gps_fix",
            "lat":     round(msg.latitude,  7),
            "lon":     round(msg.longitude, 7),
            "status":  int(msg.status.status),   # -1=no fix, 0=fix, 1=SBAS, 2=GBAS
            "sigma_m": sigma,
        })), self._ws_loop)

    def _on_gps_odometry(self, msg) -> None:
        asyncio.run_coroutine_threadsafe(self._ws_send(json.dumps({
            "type": "gps_odometry",
            "x":    round(msg.pose.pose.position.x, 3),
            "y":    round(msg.pose.pose.position.y, 3),
        })), self._ws_loop)

    async def _ws_send(self, text: str) -> None:
        if self._ws is not None:
            try:
                await self._ws.send(text)
            except Exception as exc:
                self.get_logger().warning(f"WS send failed: {exc}")

    # ── WebSocket receive loop ────────────────────────────────────────────────

    def _ws_thread_main(self) -> None:
        asyncio.set_event_loop(self._ws_loop)
        self._ws_loop.run_until_complete(self._ws_connect_loop())

    async def _ws_connect_loop(self) -> None:
        backoff = 1.0
        while True:
            try:
                async with websockets.connect(self._ws_url) as ws:
                    self._ws = ws
                    backoff  = 1.0
                    # Request param_report so unit_scale auto-updates on connect
                    await ws.send(json.dumps({"cmd": "param_read_req"}))
                    async for raw in ws:
                        try:
                            msg = json.loads(raw)
                            self._dispatch_ws_msg(msg)
                        except json.JSONDecodeError:
                            pass
            except Exception as exc:
                self._ws = None
                self.get_logger().warning(f"WS disconnected: {exc} — retry in {backoff:.1f}s")
                await asyncio.sleep(backoff)
                backoff = min(backoff * 1.5, 30.0)

    def _dispatch_ws_msg(self, msg: dict) -> None:
        t = msg.get("type")
        if t == "agv_odometry":
            self._on_agv_odometry(msg)
        elif t == "agv_status":
            self._on_agv_status(msg)
        elif t == "param_report":
            self._on_param_report(msg)
        elif msg.get("cmd") == "ros_publish":
            self._on_ros_publish(msg)
        elif msg.get("cmd") == "request_map_path":
            self._on_request_map_path()
        elif msg.get("cmd") == "reset_odom":
            self._odom.reset()
            asyncio.run_coroutine_threadsafe(
                self._ws_send(json.dumps({"type": "odom_reset_ack"})), self._ws_loop)
            self.get_logger().info("오덤 리셋 완료")
        elif msg.get("cmd") == "start_route":
            self._on_start_route(msg)
        elif msg.get("cmd") == "stop_route":
            self._on_stop_route()

    def _on_row_follow_status(self, msg: String) -> None:
        # 2Hz 쓰로틀 — depth 30fps 그대로 WebSocket 릴레이하면 UI 폭주
        now = time.monotonic()
        if now - self._last_rf_relay_t < 0.5:
            return
        self._last_rf_relay_t = now
        try:
            data = json.loads(msg.data)
            ws_msg = json.dumps({
                "type":             "row_follow_status",
                "row_follow_state": data.get("state", "DISABLED"),
                "front_m":          data.get("front_m", 0.0),
                "error_m":          data.get("error_m", 0.0),
                "angular_rps":      data.get("angular_rps", 0.0),
            })
            asyncio.run_coroutine_threadsafe(self._ws_send(ws_msg), self._ws_loop)
        except Exception as exc:
            self.get_logger().warning(f"row_follow_status relay error: {exc}")

    def _on_loc_pose(self, msg) -> None:
        """RTAB-Map localization_pose → WS nav_status 릴레이 (맵 프레임 위치+방향)."""
        self._last_loc_time = time.monotonic()
        try:
            px = msg.pose.pose.position.x
            py = msg.pose.pose.position.y
            oz = msg.pose.pose.orientation.z
            ow = msg.pose.pose.orientation.w
        except AttributeError:
            try:
                px = msg.pose.position.x
                py = msg.pose.position.y
                oz = msg.pose.orientation.z
                ow = msg.pose.orientation.w
            except AttributeError:
                px = py = oz = 0.0; ow = 1.0
        theta = 2.0 * math.atan2(oz, ow)
        self._loc_x = px; self._loc_y = py; self._loc_theta = theta  # 경로 주행 거리 판정용
        ws_msg = json.dumps({
            "type":      "nav_status",
            "loc_state": "TRACKING",
            "loc_x":     round(px,    3),
            "loc_y":     round(py,    3),
            "loc_theta": round(theta, 4),
        })
        asyncio.run_coroutine_threadsafe(self._ws_send(ws_msg), self._ws_loop)

    def _on_map_path(self, msg: Path) -> None:
        """RTAB-Map /rtabmap/mapPath → WS nav_map_path 릴레이 (1초 쓰로틀, 강제 요청 즉시)."""
        now = time.monotonic()
        forced = self._force_map_path_relay
        if not forced and (now - self._last_map_path_relay_t < 1.0):
            return
        self._last_map_path_relay_t = now
        self._force_map_path_relay  = False
        poses = msg.poses
        total = len(poses)
        if total == 0:
            self._last_map_path_payload = {"poses": [], "total": 0}
            try:
                with open(_MAP_PATH_CACHE, 'w') as f:
                    json.dump(self._last_map_path_payload, f)
            except Exception as exc:
                self.get_logger().warning(f"nav_map_path empty cache write failed: {exc}")
            asyncio.run_coroutine_threadsafe(
                self._ws_send(json.dumps({"type": "nav_map_path", "poses": [], "total": 0})), self._ws_loop)
            self.get_logger().info("nav_map_path relayed: empty")
            return
        step = max(1, total // 500)  # 최대 500점으로 다운샘플
        pts = []
        for i in range(0, total, step):
            p = poses[i].pose.position
            pts.append([round(p.x, 3), round(p.y, 3)])
        self._last_map_path_payload = {"poses": pts, "total": total}
        payload = {"type": "nav_map_path", "poses": pts, "total": total}
        # 캐시 파일에 저장 (bridge.py /nav/map_path GET으로 즉시 서빙)
        try:
            with open(_MAP_PATH_CACHE, 'w') as f:
                json.dump(self._last_map_path_payload, f)
        except Exception as exc:
            self.get_logger().warning(f"nav_map_path cache write failed: {exc}")
        asyncio.run_coroutine_threadsafe(
            self._ws_send(json.dumps(payload)), self._ws_loop)
        self.get_logger().info(f"nav_map_path relayed: {len(pts)}/{total} poses")

    def _on_request_map_path(self) -> None:
        """WS 'request_map_path' 커맨드 처리: 최신 메모리/캐시 경로 전송 + 다음 mapPath 즉시 릴레이."""
        self._force_map_path_relay = True
        if self._last_map_path_payload is not None:
            payload = dict(self._last_map_path_payload)
            payload["type"] = "nav_map_path"
            asyncio.run_coroutine_threadsafe(
                self._ws_send(json.dumps(payload)), self._ws_loop)
            self.get_logger().info(
                f"nav_map_path: memory sent ({len(payload.get('poses', []))} pts)")
            return
        try:
            with open(_MAP_PATH_CACHE) as f:
                cached = json.load(f)
            cached["type"] = "nav_map_path"
            asyncio.run_coroutine_threadsafe(
                self._ws_send(json.dumps(cached)), self._ws_loop)
            self.get_logger().info(
                f"nav_map_path: cache sent ({len(cached.get('poses', []))} pts)")
        except FileNotFoundError:
            pass  # 첫 실행, 캐시 없음
        except Exception as exc:
            self.get_logger().warning(f"nav_map_path cache read failed: {exc}")

    def _nav_status_timer(self) -> None:
        """2초마다 localization 상태를 WS로 브로드캐스트."""
        elapsed = time.monotonic() - self._last_loc_time
        if elapsed < 5.0:
            loc_state = "TRACKING"
        elif elapsed < 30.0:
            loc_state = "UNCERTAIN"
        else:
            loc_state = "LOST"
        ws_msg = json.dumps({
            "type": "nav_status",
            "loc_state": loc_state,
            "loc_age_s": round(elapsed, 1),
        })
        asyncio.run_coroutine_threadsafe(self._ws_send(ws_msg), self._ws_loop)

    def _on_ros_publish(self, msg: dict) -> None:
        topic = msg.get("topic", "")
        data  = msg.get("data", {})
        if topic == "/row_follow/enable":
            m = Bool()
            m.data = bool(data.get("data", False))
            self._pub_row_enable.publish(m)
            self.get_logger().info(f"ros_publish → {topic} = {m.data}")
        elif topic == "/row_follow/speed":
            m = Float32()
            m.data = float(data.get("data", 0.0))
            self._pub_row_speed.publish(m)
            self.get_logger().info(f"ros_publish → {topic} = {m.data:.2f}")
        elif topic == "/goal_pose":
            ps = PoseStamped()
            ps.header.frame_id = "map"
            ps.header.stamp    = self.get_clock().now().to_msg()
            ps.pose.position.x = float(data.get("x", 0.0))
            ps.pose.position.y = float(data.get("y", 0.0))
            ps.pose.position.z = 0.0
            yaw = float(data.get("theta_rad", 0.0))
            ps.pose.orientation.z = math.sin(yaw / 2.0)
            ps.pose.orientation.w = math.cos(yaw / 2.0)
            self._pub_goal.publish(ps)
            self.get_logger().info(
                f"nav goal → x={ps.pose.position.x:.2f} y={ps.pose.position.y:.2f} "
                f"θ={math.degrees(yaw):.1f}°")
        elif topic == "/nav/cancel":
            # Nav2 목표 취소: 현재 odometry 위치를 새 목표로 설정 → 즉시 "도착" 처리
            ps = PoseStamped()
            ps.header.frame_id = "map"
            ps.header.stamp    = self.get_clock().now().to_msg()
            ps.pose.position.x = float(self._odom.x)
            ps.pose.position.y = float(self._odom.y)
            ps.pose.orientation.w = 1.0
            self._pub_goal.publish(ps)
            self.get_logger().info("nav_cancel: goal set to current odom pos")
        else:
            self.get_logger().warning(f"ros_publish: 미지원 토픽 {topic!r}")

    # ── 경로 주행 ─────────────────────────────────────────────────────────────

    def _on_start_route(self, msg: dict) -> None:
        waypoints = msg.get("waypoints", [])
        if not waypoints:
            return
        self._route_total  = len(waypoints)
        self._route_active = True

        if self._ntp_client is not None:
            # Nav2 NavigateThroughPoses 액션 사용
            goal = NavigateThroughPoses.Goal()
            for wp in waypoints:
                ps = PoseStamped()
                ps.header.frame_id = "map"
                ps.header.stamp    = self.get_clock().now().to_msg()
                ps.pose.position.x = float(wp[0])
                ps.pose.position.y = float(wp[1])
                yaw = float(wp[2]) if len(wp) > 2 else 0.0
                ps.pose.orientation.z = math.sin(yaw / 2.0)
                ps.pose.orientation.w = math.cos(yaw / 2.0)
                goal.poses.append(ps)
            if not self._ntp_client.wait_for_server(timeout_sec=2.0):
                self.get_logger().warning("NTP 서버 없음 — 순차 웨이포인트 모드로 전환")
                self._start_seq_route(waypoints)
                return
            future = self._ntp_client.send_goal_async(
                goal, feedback_callback=self._on_route_feedback)
            future.add_done_callback(self._on_route_goal_response)
        else:
            self._start_seq_route(waypoints)

    def _on_route_feedback(self, feedback_msg) -> None:
        remaining = getattr(feedback_msg.feedback, 'number_of_poses_remaining', 0)
        done = self._route_total - remaining
        asyncio.run_coroutine_threadsafe(self._ws_send(json.dumps({
            "type": "route_status", "state": "NAVIGATING",
            "done": done, "total": self._route_total,
        })), self._ws_loop)

    def _on_route_goal_response(self, future) -> None:
        handle = future.result()
        if not handle.accepted:
            self._route_active = False
            asyncio.run_coroutine_threadsafe(self._ws_send(json.dumps({
                "type": "route_status", "state": "FAILED",
            })), self._ws_loop)
            return
        self._route_handle = handle
        handle.get_result_async().add_done_callback(self._on_route_result)

    def _on_route_result(self, future) -> None:
        self._route_active = False
        self._route_handle = None
        from action_msgs.msg import GoalStatus
        success = (future.result().status == GoalStatus.STATUS_SUCCEEDED)
        asyncio.run_coroutine_threadsafe(self._ws_send(json.dumps({
            "type": "route_status",
            "state": "DONE" if success else "FAILED",
            "total": self._route_total,
        })), self._ws_loop)
        self.get_logger().info(f"경로 주행 {'완료' if success else '실패'}")

    def _on_stop_route(self) -> None:
        self._route_active = False
        self._route_waypoints = []
        self._route_idx = 0
        if self._route_handle is not None:
            self._route_handle.cancel_goal_async()
            self._route_handle = None
        asyncio.run_coroutine_threadsafe(self._ws_send(json.dumps({
            "type": "route_status", "state": "CANCELLED",
        })), self._ws_loop)
        self.get_logger().info("경로 주행 취소")

    # Sequential fallback (nav2_msgs 미설치 시)
    def _start_seq_route(self, waypoints: list) -> None:
        self._route_waypoints = [(w[0], w[1], w[2] if len(w) > 2 else 0.0) for w in waypoints]
        self._route_idx = 0
        self._route_active = True
        self._send_seq_goal()

    def _send_seq_goal(self) -> None:
        if self._route_idx >= len(self._route_waypoints):
            self._route_active = False
            asyncio.run_coroutine_threadsafe(self._ws_send(json.dumps({
                "type": "route_status", "state": "DONE", "total": self._route_total,
            })), self._ws_loop)
            return
        x, y, t = self._route_waypoints[self._route_idx]
        ps = PoseStamped()
        ps.header.frame_id = "map"
        ps.header.stamp    = self.get_clock().now().to_msg()
        ps.pose.position.x = x
        ps.pose.position.y = y
        ps.pose.orientation.z = math.sin(t / 2.0)
        ps.pose.orientation.w = math.cos(t / 2.0)
        self._pub_goal.publish(ps)
        asyncio.run_coroutine_threadsafe(self._ws_send(json.dumps({
            "type": "route_status", "state": "NAVIGATING",
            "done": self._route_idx, "total": self._route_total,
        })), self._ws_loop)

    def _route_seq_timer(self) -> None:
        """0.5초마다 현재 웨이포인트 도달 여부 확인 (sequential 모드 전용)."""
        if not self._route_active or not self._route_waypoints:
            return
        if self._ntp_client is not None:
            return  # NTP 사용 중 — 타이머 불필요
        if self._route_idx >= len(self._route_waypoints):
            self._route_active = False
            return
        tx, ty, _ = self._route_waypoints[self._route_idx]
        # 맵 프레임 위치 우선 사용, 없으면 오덤
        if self._last_loc_time > 0 and (time.monotonic() - self._last_loc_time < 5.0):
            dx, dy = tx - self._loc_x, ty - self._loc_y
        else:
            dx, dy = tx - self._odom.x, ty - self._odom.y
        if math.hypot(dx, dy) < 0.35:
            self._route_idx += 1
            self._send_seq_goal()

    # ── agv_odometry → /odom + tf ────────────────────────────────────────────

    def _on_agv_odometry(self, msg: dict) -> None:
        now   = self.get_clock().now()
        pos_l = msg.get("pos_left_hw",  0)
        pos_r = msg.get("pos_right_hw", 0)
        vel_l = msg.get("vel_left_hw",  0)
        vel_r = msg.get("vel_right_hw", 0)

        x, y, theta, _, _ = self._odom.update(pos_l, pos_r)

        sl = max(self._odom.scale_left,  1.0)
        sr = max(self._odom.scale_right, 1.0)
        wb = self._odom.wheel_base
        vl_mps = (vel_l / sl) / 1000.0
        vr_mps = (vel_r / sr) / 1000.0
        vx     = (vl_mps + vr_mps) / 2.0
        vtheta = (vr_mps - vl_mps) / wb

        qz = math.sin(theta / 2.0)
        qw = math.cos(theta / 2.0)

        # Publish /odom
        odom_msg = Odometry()
        odom_msg.header.stamp    = now.to_msg()
        odom_msg.header.frame_id = self.odom_frame
        odom_msg.child_frame_id  = self.base_frame
        odom_msg.pose.pose.position.x    = x
        odom_msg.pose.pose.position.y    = y
        odom_msg.pose.pose.orientation.z = qz
        odom_msg.pose.pose.orientation.w = qw
        odom_msg.twist.twist.linear.x    = vx
        odom_msg.twist.twist.angular.z   = vtheta

        # Covariance — diagonal 6×6, row-major (index = row*6 + col)
        odom_msg.pose.covariance[0]  = 0.01   # x
        odom_msg.pose.covariance[7]  = 0.01   # y
        odom_msg.pose.covariance[14] = 1e6    # z  (planar robot — unused)
        odom_msg.pose.covariance[21] = 1e6    # roll
        odom_msg.pose.covariance[28] = 1e6    # pitch
        odom_msg.pose.covariance[35] = 0.05   # yaw
        odom_msg.twist.covariance[0]  = 0.01  # vx
        odom_msg.twist.covariance[7]  = 1e6   # vy
        odom_msg.twist.covariance[14] = 1e6   # vz
        odom_msg.twist.covariance[21] = 1e6   # roll rate
        odom_msg.twist.covariance[28] = 1e6   # pitch rate
        odom_msg.twist.covariance[35] = 0.05  # yaw rate

        self._pub_odom.publish(odom_msg)

        # Broadcast TF odom → base_link
        tf_msg = TransformStamped()
        tf_msg.header.stamp    = now.to_msg()
        tf_msg.header.frame_id = self.odom_frame
        tf_msg.child_frame_id  = self.base_frame
        tf_msg.transform.translation.x = x
        tf_msg.transform.translation.y = y
        tf_msg.transform.rotation.z    = qz
        tf_msg.transform.rotation.w    = qw
        self._tf_broadcaster.sendTransform(tf_msg)

        self._last_vel_left_hw  = vel_l
        self._last_vel_right_hw = vel_r

    # ── agv_status → /agv_status ─────────────────────────────────────────────

    def _on_agv_status(self, msg: dict) -> None:
        self._pub_status.publish(String(data=json.dumps(msg)))
        enabled = msg.get("run_enable_left", False) or msg.get("run_enable_right", False)
        self._pub_run_enable.publish(Bool(data=enabled))

    # ── param_report → unit_scale 자동 갱신 ──────────────────────────────────

    def _on_param_report(self, msg: dict) -> None:
        axes = msg.get("axes", [])
        if len(axes) < 2:
            return
        sl = float(axes[0].get("unit_scale", 1.0))
        sr = float(axes[1].get("unit_scale", 1.0))
        if sl <= 0 or sr <= 0:
            return
        self._odom.scale_left  = sl
        self._odom.scale_right = sr
        self.set_parameters([
            Parameter("unit_scale_left",  Parameter.Type.DOUBLE, sl),
            Parameter("unit_scale_right", Parameter.Type.DOUBLE, sr),
        ])
        self.get_logger().info(
            f"unit_scale updated from firmware — L={sl:.1f}  R={sr:.1f} counts/mm"
        )

    def destroy_node(self) -> None:
        self._ws_loop.call_soon_threadsafe(self._ws_loop.stop)
        super().destroy_node()


# ── Entry point ───────────────────────────────────────────────────────────────

def main(args=None):
    if not _ROS2_AVAILABLE:
        print("rclpy not available — cannot run ROS2 node.")
        return
    if not _WS_AVAILABLE:
        print("websockets not available — pip install websockets")
        return

    rclpy.init(args=args)
    node = Stm32BridgeNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
