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
import threading
import time
from typing import Optional

try:
    import rclpy
    from rclpy.node import Node
    from rclpy.parameter import Parameter
    from geometry_msgs.msg import Twist, TransformStamped
    from nav_msgs.msg import Odometry
    from std_msgs.msg import String, Bool
    import tf2_ros
    _ROS2_AVAILABLE = True
except ImportError:
    _ROS2_AVAILABLE = False
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

        # ROS2 publishers
        self._pub_odom       = self.create_publisher(Odometry, "/odom",        10)
        self._pub_status     = self.create_publisher(String,   "/agv_status",  10)
        self._pub_run_enable = self.create_publisher(Bool,     "/agv_run_enable_state", 10)

        # TF broadcaster
        self._tf_broadcaster = tf2_ros.TransformBroadcaster(self)

        # ROS2 subscribers
        self._sub_cmd_vel = self.create_subscription(
            Twist, "/cmd_vel", self._on_cmd_vel, 10)
        self._sub_run_enable = self.create_subscription(
            Bool, "/agv_run_enable", self._on_run_enable, 10)

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
