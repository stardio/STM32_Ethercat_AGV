#!/usr/bin/env python3
"""
obstacle_classifier.py — YOLOv8 기반 3단계 안전 레이어 ROS2 노드 (Phase 4)

3단계 안전 레이어:
  Layer 1: Nav2 Depth Costmap  — nav2_params.yaml (passive, 별도 노드 불필요)
  Layer 2: 이 노드             — YOLOv8 객체 분류 + 거리 기반 행동 결정
  Layer 3: 이 노드             — Geofence 가상 울타리 (config/geofence.yaml)

행동(action) 종류 및 우선순위:
  FULL_STOP  (5) — 사람 감지 또는 Geofence 진입 → 완전 정지
  WAIT       (4) — 농기계/차량 감지 → 정지, wait_resume_sec 후 재시도
  SLOW_STOP  (3) — 미분류 장애물 → 서행 후 정지
  SLOW_AVOID (2) — 동물 감지 → 30% 감속 유지
  CLEAR      (1) — 장애물 없음

행동별 COCO 클래스 매핑:
  person(0)                                     → FULL_STOP
  cat(15) dog(16) horse(17) sheep(18) cow(19)   → SLOW_AVOID
  elephant(20) bear(21) zebra(22) giraffe(23)   → SLOW_AVOID
  bicycle(1) car(2) motorcycle(3) bus(5)        → WAIT
  train(6) truck(7)                             → WAIT
  그 외 감지 (conf>threshold, dist<max_react)    → SLOW_STOP

토픽:
  Sub: /camera/color/image_raw                    sensor_msgs/Image (bgr8)
       /camera/aligned_depth_to_color/image_raw   sensor_msgs/Image (16UC1)
       /odometry/filtered  OR  /odom              nav_msgs/Odometry (geofence)
  Pub: /obstacle/action                           std_msgs/String
       /obstacle/detections                        std_msgs/String (JSON)
       /obstacle/debug_image                       sensor_msgs/Image (bgr8, 선택)

파라미터:
  model_path          (str,   'yolov8n.pt')    YOLOv8 모델 경로
  infer_hz            (float, 5.0)             추론 주기 [Hz]
  conf_threshold      (float, 0.50)            YOLOv8 confidence threshold
  min_react_dist_m    (float, 0.30)            이 거리 이하 감지는 무시 (바닥 노이즈)
  max_react_dist_m    (float, 3.00)            이 거리 이상 감지는 무시
  confirm_frames      (int,   3)               행동 확정에 필요한 연속 감지 횟수
  clear_frames        (int,   5)               CLEAR 전환에 필요한 연속 무감지 횟수
  wait_resume_sec     (float, 5.0)             WAIT 상태 자동 재시도 대기 시간
  geofence_config     (str,   '')              geofence.yaml 경로 (빈 문자열 = 비활성)
  odom_topic          (str,   '/odometry/filtered')
"""

import json
import math
import os
import time
from typing import Optional

import numpy as np

try:
    import rclpy
    from rclpy.node import Node
    from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSDurabilityPolicy
    from nav_msgs.msg import Odometry
    from sensor_msgs.msg import Image
    from std_msgs.msg import String
    _ROS2_AVAILABLE = True
except ImportError:
    _ROS2_AVAILABLE = False
    print('[obstacle_classifier] WARNING: rclpy not found.')

try:
    from ultralytics import YOLO
    _YOLO_AVAILABLE = True
except ImportError:
    _YOLO_AVAILABLE = False
    YOLO = None

try:
    import yaml as _yaml
    _YAML_AVAILABLE = True
except ImportError:
    _YAML_AVAILABLE = False

# ── COCO class → obstacle type mapping ───────────────────────────────────────

_COCO_PERSON  = {0}
_COCO_ANIMAL  = {15, 16, 17, 18, 19, 20, 21, 22, 23}
_COCO_VEHICLE = {1, 2, 3, 5, 6, 7}   # bicycle, car, motorcycle, bus, train, truck

def _coco_to_type(cls_id: int) -> str:
    if cls_id in _COCO_PERSON:  return 'person'
    if cls_id in _COCO_ANIMAL:  return 'animal'
    if cls_id in _COCO_VEHICLE: return 'vehicle'
    return 'unknown'

_TYPE_TO_ACTION = {
    'person':  'FULL_STOP',
    'animal':  'SLOW_AVOID',
    'vehicle': 'WAIT',
    'unknown': 'SLOW_STOP',
}

_ACTION_PRIORITY = {
    'CLEAR':      1,
    'SLOW_AVOID': 2,
    'SLOW_STOP':  3,
    'WAIT':       4,
    'FULL_STOP':  5,
}

# ── Geofence helpers ──────────────────────────────────────────────────────────

def _point_in_polygon(px: float, py: float, polygon: list) -> bool:
    """Ray casting 알고리즘으로 점이 다각형 내부인지 판정."""
    n = len(polygon)
    if n < 3:
        return False
    inside = False
    j = n - 1
    for i in range(n):
        xi, yi = polygon[i]
        xj, yj = polygon[j]
        if ((yi > py) != (yj > py)) and \
           (px < (xj - xi) * (py - yi) / max(yj - yi, 1e-9) + xi):
            inside = not inside
        j = i
    return inside


def _load_geofence(path: str) -> list:
    """geofence.yaml 로드. 실패 시 빈 리스트 반환."""
    if not path or not _YAML_AVAILABLE:
        return []
    try:
        with open(path, 'r') as f:
            cfg = _yaml.safe_load(f)
        gf = cfg.get('geofence', {})
        if not gf.get('enabled', False):
            return []
        zones = []
        for z in gf.get('zones', []):
            pts = z.get('polygon', [])
            if len(pts) >= 3:
                zones.append({
                    'name':    z.get('name', 'unknown'),
                    'action':  z.get('action', 'FULL_STOP'),
                    'polygon': [[float(p[0]), float(p[1])] for p in pts],
                })
        return zones
    except Exception as exc:
        print(f'[obstacle_classifier] geofence load failed: {exc}')
        return []


# ── Main node ─────────────────────────────────────────────────────────────────

class ObstacleClassifierNode(Node):

    def __init__(self):
        super().__init__('obstacle_classifier')

        # ── Parameters ────────────────────────────────────────────────────
        self.declare_parameter('model_path',       'yolov8n.pt')
        self.declare_parameter('infer_hz',         5.0)
        self.declare_parameter('conf_threshold',   0.50)
        self.declare_parameter('min_react_dist_m', 0.30)
        self.declare_parameter('max_react_dist_m', 3.00)
        self.declare_parameter('confirm_frames',   3)
        self.declare_parameter('clear_frames',     5)
        self.declare_parameter('wait_resume_sec',  5.0)
        self.declare_parameter('geofence_config',  '')
        self.declare_parameter('odom_topic',       '/odometry/filtered')

        model_path       = self.get_parameter('model_path').value
        infer_hz         = float(self.get_parameter('infer_hz').value)
        self._conf_thr   = float(self.get_parameter('conf_threshold').value)
        self._min_mm     = float(self.get_parameter('min_react_dist_m').value) * 1000.0
        self._max_mm     = float(self.get_parameter('max_react_dist_m').value) * 1000.0
        self._confirm_n  = int(self.get_parameter('confirm_frames').value)
        self._clear_n    = int(self.get_parameter('clear_frames').value)
        self._wait_sec   = float(self.get_parameter('wait_resume_sec').value)
        gf_path          = self.get_parameter('geofence_config').value
        odom_topic       = self.get_parameter('odom_topic').value

        # ── YOLOv8 model ──────────────────────────────────────────────────
        self._model: Optional[object] = None
        if _YOLO_AVAILABLE:
            try:
                self._model = YOLO(model_path)
                self.get_logger().info(f'YOLOv8 loaded: {model_path}')
            except Exception as exc:
                self.get_logger().error(f'YOLOv8 load failed: {exc}')
        else:
            self.get_logger().warning(
                'ultralytics not installed — pip install ultralytics\n'
                'Obstacle detection DISABLED (passing through commands).')

        # ── Geofence ──────────────────────────────────────────────────────
        self._geofence_zones = _load_geofence(gf_path)
        if self._geofence_zones:
            self.get_logger().info(
                f'Geofence: {len(self._geofence_zones)} zones loaded from {gf_path}')
        self._robot_x = 0.0
        self._robot_y = 0.0

        # ── State ─────────────────────────────────────────────────────────
        self._latest_depth: Optional[np.ndarray] = None
        self._last_infer_t   = 0.0
        self._infer_period   = 1.0 / max(infer_hz, 0.1)
        self._current_action = 'CLEAR'
        self._confirm_buf: list[str] = []   # ring buffer of recent raw actions
        self._clear_count    = 0
        self._wait_until     = 0.0          # monotonic time to resume after WAIT

        # ── Publishers ────────────────────────────────────────────────────
        self._pub_action = self.create_publisher(String, '/obstacle/action',     10)
        self._pub_dets   = self.create_publisher(String, '/obstacle/detections', 10)
        self._pub_debug  = self.create_publisher(Image,  '/obstacle/debug_image', 5)

        # ── Subscribers ───────────────────────────────────────────────────
        _cam_qos = QoSProfile(
            depth=5,
            reliability=QoSReliabilityPolicy.BEST_EFFORT,
            durability=QoSDurabilityPolicy.VOLATILE,
        )
        self._sub_color = self.create_subscription(
            Image, '/camera/color/image_raw',
            self._on_color, _cam_qos)
        self._sub_depth = self.create_subscription(
            Image, '/camera/realsense2_camera/depth/image_rect_raw',
            self._on_depth, _cam_qos)
        self._sub_odom = self.create_subscription(
            Odometry, odom_topic, self._on_odom, 10)

        self.get_logger().info(
            f'obstacle_classifier ready — '
            f'yolo={"OK" if self._model else "DISABLED"}  '
            f'infer={infer_hz}Hz  conf={self._conf_thr}  '
            f'react={self._min_mm/1000:.1f}~{self._max_mm/1000:.1f}m  '
            f'geofence={len(self._geofence_zones)} zones'
        )

    # ── Depth / Color / Odom callbacks ────────────────────────────────────

    def _on_depth(self, msg: Image) -> None:
        if msg.encoding != '16UC1':
            return
        self._latest_depth = np.frombuffer(
            bytes(msg.data), dtype=np.uint16
        ).reshape(msg.height, msg.width)

    def _on_odom(self, msg: Odometry) -> None:
        self._robot_x = msg.pose.pose.position.x
        self._robot_y = msg.pose.pose.position.y
        self._check_geofence()

    def _on_color(self, msg: Image) -> None:
        now = time.monotonic()
        if now - self._last_infer_t < self._infer_period:
            # 추론 주기 아님 — 마지막 action 재발행
            self._publish_action(self._current_action, [])
            return
        self._last_infer_t = now

        # ── Decode color image (bgr8) ──────────────────────────────────
        if msg.encoding not in ('bgr8', 'rgb8'):
            return
        frame = np.frombuffer(bytes(msg.data), dtype=np.uint8).reshape(
            msg.height, msg.width, 3)
        if msg.encoding == 'rgb8':
            frame = frame[:, :, ::-1]  # RGB → BGR for YOLO

        # ── YOLOv8 inference ──────────────────────────────────────────
        raw_action = 'CLEAR'
        detections: list[dict] = []

        if self._model is not None and self._latest_depth is not None:
            try:
                results = self._model(
                    frame, conf=self._conf_thr, verbose=False)
                raw_action, detections = self._process_results(
                    results, msg.width, msg.height)
            except Exception as exc:
                self.get_logger().warning_once(f'YOLO inference error: {exc}')
                raw_action = 'CLEAR'

        # ── Hysteresis filter ─────────────────────────────────────────
        confirmed = self._hysteresis(raw_action)

        # ── Geofence action is injected in _check_geofence() ─────────
        # Take the higher-priority of YOLO and geofence
        final_action = confirmed  # geofence may override in _check_geofence

        self._current_action = final_action
        self._publish_action(final_action, detections)

        # ── Debug image (only when subscribed) ────────────────────────
        if self._pub_debug.get_subscription_count() > 0 and detections:
            self._publish_debug(frame, detections, msg.height, msg.width)

    # ── Detection processing ───────────────────────────────────────────────

    def _process_results(self, results, img_w: int, img_h: int) -> tuple:
        """YOLOv8 결과에서 가장 위험한 action과 감지 목록 반환."""
        depth = self._latest_depth
        best_action  = 'CLEAR'
        best_priority = 0
        detections   = []

        for box in results[0].boxes:
            cls_id = int(box.cls[0])
            conf   = float(box.conf[0])
            x1, y1, x2, y2 = map(int, box.xyxy[0])
            cx = max(0, min(img_w - 1, (x1 + x2) // 2))
            cy = max(0, min(img_h - 1, (y1 + y2) // 2))

            # 중심 3×3 패치의 중앙값 depth 사용 (단일 픽셀 노이즈 억제)
            py0 = max(0, cy - 1);  py1 = min(img_h, cy + 2)
            px0 = max(0, cx - 1);  px1 = min(img_w, cx + 2)
            patch = depth[py0:py1, px0:px1]
            valid = patch[patch > 0]
            dist_mm = float(np.median(valid)) if len(valid) > 0 else 0.0

            if dist_mm < self._min_mm or dist_mm > self._max_mm:
                continue   # 거리 범위 밖 → 무시

            obs_type = _coco_to_type(cls_id)
            action   = _TYPE_TO_ACTION.get(obs_type, 'SLOW_STOP')
            priority = _ACTION_PRIORITY.get(action, 0)

            detections.append({
                'class':    results[0].names[cls_id],
                'type':     obs_type,
                'action':   action,
                'conf':     round(conf, 3),
                'dist_m':   round(dist_mm / 1000.0, 3),
                'bbox':     [x1, y1, x2, y2],
            })

            if priority > best_priority:
                best_priority = priority
                best_action   = action

        return best_action, detections

    # ── Geofence check ─────────────────────────────────────────────────────

    def _check_geofence(self) -> None:
        if not self._geofence_zones:
            return
        gf_action  = 'CLEAR'
        gf_priority = 0
        for zone in self._geofence_zones:
            if _point_in_polygon(self._robot_x, self._robot_y, zone['polygon']):
                a  = zone['action']
                p  = _ACTION_PRIORITY.get(a, 0)
                if p > gf_priority:
                    gf_priority = p
                    gf_action   = a
                    self.get_logger().warning_once(
                        f"Geofence violation: zone '{zone['name']}' → {a}")

        # Geofence가 현재 YOLO action보다 높으면 override
        if _ACTION_PRIORITY.get(gf_action, 0) > \
           _ACTION_PRIORITY.get(self._current_action, 0):
            self._current_action = gf_action
            self._publish_action(gf_action, [])

    # ── Hysteresis ─────────────────────────────────────────────────────────

    def _hysteresis(self, raw_action: str) -> str:
        """
        Confirmation filter:
          - CLEAR: 현재 action이 CLEAR가 아니면 clear_frames 연속 후 전환
          - Non-CLEAR: confirm_frames 연속 확인 후 행동 확정
        """
        self._confirm_buf.append(raw_action)
        if len(self._confirm_buf) > max(self._confirm_n, self._clear_n):
            self._confirm_buf.pop(0)

        recent = self._confirm_buf[-self._confirm_n:]

        # Upgrade: N 연속 같은 action이 오면 즉시 채택
        if len(recent) == self._confirm_n and len(set(recent)) == 1:
            new_action = recent[0]
            if new_action != 'CLEAR':
                # WAIT 자동 재시도 타이머
                if new_action == 'WAIT' and \
                   self._current_action != 'WAIT':
                    self._wait_until = time.monotonic() + self._wait_sec
                return new_action

        # WAIT 재시도 만료 체크
        if self._current_action == 'WAIT' and \
           time.monotonic() >= self._wait_until:
            return 'CLEAR'

        # Downgrade: clear_frames 연속 CLEAR 일 때만 CLEAR로 전환
        if raw_action == 'CLEAR':
            self._clear_count += 1
        else:
            self._clear_count = 0

        if self._clear_count >= self._clear_n:
            return 'CLEAR'

        return self._current_action  # 아직 변동 없음

    # ── Publish helpers ────────────────────────────────────────────────────

    def _publish_action(self, action: str, detections: list) -> None:
        self._pub_action.publish(String(data=action))
        payload = {
            'action':     action,
            'robot_pos':  [round(self._robot_x, 3), round(self._robot_y, 3)],
            'detections': detections,
        }
        self._pub_dets.publish(String(data=json.dumps(payload)))

    def _publish_debug(self, frame: np.ndarray,
                       detections: list, H: int, W: int) -> None:
        try:
            import cv2
            vis = frame.copy()
            for d in detections:
                x1, y1, x2, y2 = d['bbox']
                color = (0, 0, 255) if d['action'] == 'FULL_STOP' else \
                        (0, 165, 255) if d['action'] == 'WAIT' else \
                        (0, 255, 255)
                cv2.rectangle(vis, (x1, y1), (x2, y2), color, 2)
                label = f"{d['class']} {d['dist_m']:.1f}m"
                cv2.putText(vis, label, (x1, max(y1 - 6, 12)),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 1)
            img = Image()
            img.height   = H
            img.width    = W
            img.encoding = 'bgr8'
            img.step     = W * 3
            img.data     = vis.tobytes()
            self._pub_debug.publish(img)
        except ImportError:
            pass  # cv2 없으면 debug image 건너뜀


# ── Entry point ───────────────────────────────────────────────────────────────

def main(args=None):
    if not _ROS2_AVAILABLE:
        print('rclpy not available — cannot run.')
        return
    rclpy.init(args=args)
    node = ObstacleClassifierNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
