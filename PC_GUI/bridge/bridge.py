#!/usr/bin/env python3
"""
bridge.py — Serial ↔ WebSocket bridge for the STM32 AGV.

The bridge relays binary SLIP-framed packets between the STM32 UART
(921600 bps) and any number of WebSocket clients (web UI, test scripts).

Usage:
  pip install -r requirements.txt
  python bridge.py --port COM3
  python bridge.py --port /dev/ttyACM0 --ws-port 8765

WebSocket JSON API
──────────────────
Inbound (bridge → client):
  {"type":"status",       "ts":…, "axes":[…×6], "interp":"IDLE",
                          "all_ready":bool, "all_targets_reached":bool,
                          "active_axes":6}
  {"type":"ack",          "seq":…, "result":"OK"}
  {"type":"log",          "msg":"…"}
  {"type":"param_report", "axes":[…×6]}
  {"type":"connected",    "port":"COM3", "baud":921600}
  {"type":"disconnected"}
  {"type":"agv_odometry", "pos_left_hw":…, "pos_right_hw":…, "vel_left_hw":…,
                           "vel_right_hw":…, "timestamp_ms":…}
  {"type":"agv_status",   "all_ready":bool, "cia402_left":"OP_ENABLED",
                           "cia402_right":"OP_ENABLED",
                           "run_enable_left":bool, "run_enable_right":bool}

Outbound (client → bridge):
  {"cmd":"home",              "axis":255, "type":0}
  {"cmd":"set_param",         "axis":0,   "param_id":7,  "value":100}
  {"cmd":"save_flash"}
  {"cmd":"stop"}
  {"cmd":"run_enable",        "axis":255, "enable":true}
  {"cmd":"param_read_req"}
  {"cmd":"fault_reset",       "axis":255}
  {"cmd":"agv_velocity",      "linear_mps":0.5, "angular_rps":0.0}
"""

import asyncio
import json
import logging
import argparse
import functools
import http.server
import math
import os
import signal
import socketserver
import subprocess
import sys
import platform
import threading
import time
from typing import Optional

# ── 카메라 optional deps ─────────────────────────────────────────────────────
try:
    import pyrealsense2 as _rs
    _rs_available = True
except Exception:
    _rs_available = False

try:
    import numpy as _np
    _np_available = True
except ImportError:
    _np_available = False

try:
    import cv2 as _cv2
    _cv2_available = True
except ImportError:
    _cv2_available = False

try:
    from PIL import Image as _PilImage
    import io as _pil_io
    _pil_available = True
except ImportError:
    _pil_available = False

import websockets
from websockets.server import WebSocketServerProtocol

try:
    import serial_asyncio
except ImportError:
    serial_asyncio = None  # checked at startup

try:
    import serial.tools.list_ports as _list_ports
    _has_list_ports = True
except ImportError:
    _has_list_ports = False

from slip_codec import SlipDecoder, slip_encode
import packet_defs as pkt

log = logging.getLogger("bridge")

# ── OS 감지 ──────────────────────────────────────────────────────────────────

IS_WINDOWS = sys.platform == "win32"
IS_LINUX   = sys.platform.startswith("linux")
IS_MAC     = sys.platform == "darwin"
OS_NAME    = platform.system()          # "Windows" / "Linux" / "Darwin"

# STM32 VCP USB 식별자
# STMicroelectronics VID=0x0483, STM32 Virtual COM PID=0x5740
_STM32_VID = 0x0483
_STM32_PIDS = {0x5740, 0x374B, 0x3748}   # VCP / STLink VCP / H7 VCP


def _auto_detect_port() -> str:
    """
    STM32 Virtual COM Port를 자동으로 탐색합니다.

    탐색 순서:
      1. USB VID/PID 로 STM32 VCP 직접 매칭
      2. 포트 설명(description)에 'STM32' / 'STLink' 포함 여부
      3. OS별 기본 포트 반환 (COM32 / /dev/ttyUSB0 / /dev/ttyACM0)
    """
    if not _has_list_ports:
        return "COM32" if IS_WINDOWS else "/dev/ttyUSB0"

    ports = list(_list_ports.comports())

    # 1단계: VID/PID 매칭
    for p in ports:
        if getattr(p, "vid", None) == _STM32_VID and \
           getattr(p, "pid", None) in _STM32_PIDS:
            log.info("STM32 VCP 자동 감지 (VID/PID): %s — %s", p.device, p.description)
            return p.device

    # 2단계: 설명 문자열 매칭
    for p in ports:
        desc = (p.description or "").upper()
        if any(kw in desc for kw in ("STM32", "STLINK", "ST-LINK", "VIRTUAL COM")):
            log.info("STM32 VCP 자동 감지 (설명): %s — %s", p.device, p.description)
            return p.device

    # 3단계: OS별 기본값
    if IS_WINDOWS:
        default = "COM32"
    elif IS_LINUX:
        # /dev/ttyACM0 우선 (STM32 VCP), 없으면 /dev/ttyUSB0
        import glob
        acm = sorted(glob.glob("/dev/ttyACM*"))
        usb = sorted(glob.glob("/dev/ttyUSB*"))
        default = acm[0] if acm else (usb[0] if usb else "/dev/ttyUSB0")
    else:
        default = "/dev/tty.usbmodem*"

    log.warning("STM32 포트 자동 감지 실패 — 기본값 사용: %s", default)
    return default


def _check_linux_permissions(port: str) -> None:
    """Linux에서 시리얼 포트 접근 권한을 확인하고 안내합니다."""
    if not IS_LINUX:
        return
    import grp, pwd
    try:
        stat = os.stat(port)
        gid  = stat.st_gid
        grp_name = grp.getgrgid(gid).gr_name
        user = pwd.getpwuid(os.getuid()).pw_name
        user_groups = [g.gr_name for g in grp.getgrall() if user in g.gr_mem]
        if grp_name not in user_groups:
            log.warning(
                "⚠  포트 권한 없음: %s (그룹: %s)\n"
                "   해결: sudo usermod -aG %s %s  (재로그인 필요)",
                port, grp_name, grp_name, user
            )
    except (FileNotFoundError, PermissionError, KeyError):
        pass


def _print_startup_banner(port: str, ws_port: int, http_port: int) -> None:
    """실행 환경 정보를 출력합니다."""
    sep = "─" * 56
    print(sep)
    print(f"  Robot HMI Bridge")
    print(f"  OS      : {OS_NAME} {platform.release()} ({platform.machine()})")
    print(f"  Python  : {platform.python_version()}")
    print(f"  Serial  : {port}  @921600bps")
    print(f"  WS      : ws://0.0.0.0:{ws_port}")
    print(f"  HTTP    : http://localhost:{http_port}/")
    print(sep)

# ── Shared state ──────────────────────────────────────────────────────────────

_clients: set[WebSocketServerProtocol] = set()
_serial_writer: Optional[asyncio.StreamWriter] = None
_last_status: Optional[dict] = None      # cached for new-client handshake
_last_params:  Optional[dict] = None     # cached param report

# Dynamic port switching
_current_port:    str                    = "COM32"
_baud_rate:       int                    = 921600
_reconnect_delay_g: float               = 3.0
_serial_task:     Optional[asyncio.Task] = None

_BRIDGE_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.dirname(os.path.dirname(_BRIDGE_DIR))
_ROS2_WS_DIR = os.path.join(_REPO_ROOT, "ros2_ws")
_DEFAULT_MAP_DB = os.path.expanduser("~/.ros/rtabmap.db")

_autonomy_lock = threading.Lock()
_autonomy_proc: Optional[subprocess.Popen] = None
_autonomy_mode: str = "IDLE"
_autonomy_db_path: str = _DEFAULT_MAP_DB
_autonomy_started_at: float = 0.0
_autonomy_log: list[str] = []
_autonomy_exit_code: Optional[int] = None

# ── 카메라 상태 ────────────────────────────────────────────────────────────────
_cam_lock:  threading.Lock = threading.Lock()
_cam_frame: Optional[bytes] = None          # 최신 JPEG 바이트 (None = 아직 없음)
_cam_info:  dict = {"connected": False, "error": "초기화 중…"}
_cam_imu:   dict = {"ax": 0.0, "ay": 0.0, "az": 0.0,
                    "wx": 0.0, "wy": 0.0, "wz": 0.0,
                    "yaw": 0.0,          # gyro Z 적분 yaw [rad]
                    "ts": 0.0,           # 마지막 IMU 타임스탬프
                    "ts_gyro": 0.0}      # 적분용 이전 gyro 타임스탬프

# ── Go-To-Goal controller (경로 B — Nav2 없이 encoder 오도메트리 직접 제어) ────
_loop: Optional[asyncio.AbstractEventLoop] = None   # set in _run()

_NAV_PARAMS_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'nav_params.json')

def _load_nav_params() -> dict:
    """nav_params.json에서 unit_scale / wheel_base 불러오기."""
    defaults = {'unit_scale': 4.0, 'wheel_base': 0.60}
    try:
        with open(_NAV_PARAMS_FILE, 'r') as f:
            data = json.load(f)
        defaults.update({k: float(v) for k, v in data.items() if k in defaults})
        log.info("Nav params loaded: scale=%.4f wb=%.3f", defaults['unit_scale'], defaults['wheel_base'])
    except FileNotFoundError:
        log.info("nav_params.json 없음 — 기본값 사용 (scale=4.0, wb=0.60)")
    except Exception as exc:
        log.warning("nav_params.json 읽기 실패: %s — 기본값 사용", exc)
    return defaults

def _save_nav_params() -> None:
    """unit_scale / wheel_base를 nav_params.json에 저장."""
    try:
        with open(_NAV_PARAMS_FILE, 'w') as f:
            json.dump({'unit_scale': _odom_state['unit_scale'],
                       'wheel_base': _odom_state['wheel_base']}, f, indent=2)
        log.info("Nav params saved → %s  (scale=%.4f wb=%.3f)",
                 _NAV_PARAMS_FILE, _odom_state['unit_scale'], _odom_state['wheel_base'])
    except Exception as exc:
        log.warning("nav_params.json 저장 실패: %s  경로: %s", exc, _NAV_PARAMS_FILE)

_nav_defaults = _load_nav_params()
_odom_state: dict = {
    'x': 0.0, 'y': 0.0, 'theta': 0.0,
    'pos_left_prev':  None,   # m
    'pos_right_prev': None,   # m
    'unit_scale':  _nav_defaults['unit_scale'],  # counts/mm
    'wheel_base':  _nav_defaults['wheel_base'],  # m
    'max_linear':  0.30,   # m/s   — GoToGoal 최대 직진 속도 (HMI 슬라이더 동기)
    'max_angular': 0.50,   # rad/s — GoToGoal 최대 회전 속도 (HMI 슬라이더 동기)
}
_goal_state: dict = {'active': False, 'state': 'IDLE', 'x': 0.0, 'y': 0.0}
_goal_task:  Optional[asyncio.Task] = None
_route_state: dict = {'active': False, 'state': 'IDLE', 'done': 0, 'total': 0}
_route_task:  Optional[asyncio.Task] = None


def _camera_loop() -> None:
    """백그라운드 스레드: D435i 컬러 스트림 → JPEG → _cam_frame 갱신."""
    global _cam_frame, _cam_info
    if not _rs_available:
        with _cam_lock:
            _cam_info = {"connected": False,
                         "error": "pyrealsense2 미설치 (pip install pyrealsense2)"}
        return
    if not _np_available:
        with _cam_lock:
            _cam_info = {"connected": False, "error": "numpy 미설치"}
        return

    pipeline = _rs.pipeline()

    # ── Motion Sensor 콜백 (pipeline 과 별도 실행) ───────────────────────────
    def _imu_frame_cb(frame: '_rs.frame') -> None:
        st = frame.get_profile().stream_type()
        mf = frame.as_motion_frame().get_motion_data()
        now = time.time()
        if st == _rs.stream.accel:
            with _cam_lock:
                _cam_imu.update({"ax": round(float(mf.x), 3),
                                 "ay": round(float(mf.y), 3),
                                 "az": round(float(mf.z), 3),
                                 "ts": now})
        elif st == _rs.stream.gyro:
            # D435i 좌표계: X=오른쪽, Y=아래, Z=전방
            # 수평 회전(AGV yaw) = 세계 수직축 회전 = 카메라 Y축 회전
            # 오른손 법칙: 양의 wy → 오른쪽 선회(CW from above)
            # 표준 yaw(CCW=양수) 에 맞게 부호 반전
            wy = float(mf.y)
            yaw_rate = -wy
            with _cam_lock:
                prev_ts = _cam_imu["ts_gyro"]
                dt = now - prev_ts if 0 < (now - prev_ts) < 0.05 else 0.0
                _cam_imu["yaw"] = (_cam_imu["yaw"] + yaw_rate * dt) % (2 * math.pi)
                _cam_imu.update({"wx": round(float(mf.x), 3),
                                 "wy": round(wy, 3),
                                 "wz": round(float(mf.z), 3),
                                 "ts_gyro": now,
                                 "ts": now})

    def _start_motion_sensor(device: '_rs.device') -> Optional[object]:
        """Motion Module 을 sensor API 로 직접 열기 (pipeline 없이)."""
        try:
            sensors = device.query_sensors()
            motion = next((s for s in sensors
                           if 'Motion' in s.get_info(_rs.camera_info.name)), None)
            if motion is None:
                return None
            profiles = motion.get_stream_profiles()
            accel_p = next((p for p in profiles
                            if p.stream_type() == _rs.stream.accel), None)
            gyro_p  = next((p for p in profiles
                            if p.stream_type() == _rs.stream.gyro), None)
            if accel_p is None or gyro_p is None:
                return None
            motion.open([accel_p, gyro_p])
            motion.start(_imu_frame_cb)
            log.info("D435i Motion Sensor 시작 (accel@%dfps gyro@%dfps)",
                     accel_p.fps(), gyro_p.fps())
            return motion
        except Exception as exc:
            log.warning("D435i IMU 시작 실패: %s", exc)
            return None

    while True:       # 카메라 재연결 루프
        motion_sensor = None
        try:
            cfg = _rs.config()
            cfg.enable_stream(_rs.stream.color, 320, 240, _rs.format.bgr8, 30)
            profile = pipeline.start(cfg)
            device  = profile.get_device()
            model   = device.get_info(_rs.camera_info.name)
            serial  = device.get_info(_rs.camera_info.serial_number)

            # Motion sensor 는 pipeline 과 별도로 시작
            motion_sensor = _start_motion_sensor(device)
            imu_ok = motion_sensor is not None

            with _cam_lock:
                _cam_info = {
                    "connected":  True,
                    "model":      model,
                    "serial":     serial,
                    "resolution": "320×240@30fps",
                }
            log.info("D435i 연결됨: %s  SN:%s  IMU:%s", model, serial, imu_ok)

            while True:
                frames = pipeline.wait_for_frames(timeout_ms=3000)
                color  = frames.get_color_frame()
                if not color:
                    continue
                arr = _np.asanyarray(color.get_data())   # (H,W,3) BGR

                if _cv2_available:
                    ok, buf = _cv2.imencode(
                        '.jpg', arr, [_cv2.IMWRITE_JPEG_QUALITY, 72])
                    jpeg = bytes(buf) if ok else None
                elif _pil_available:
                    img = _PilImage.fromarray(arr[:, :, ::-1])  # BGR→RGB
                    bio = _pil_io.BytesIO()
                    img.save(bio, format='JPEG', quality=72)
                    jpeg = bio.getvalue()
                else:
                    jpeg = None

                if jpeg:
                    with _cam_lock:
                        _cam_frame = jpeg

        except Exception as exc:
            log.warning("D435i 오류: %s — 5초 후 재연결", exc)
            with _cam_lock:
                _cam_info  = {"connected": False, "error": str(exc)}
                _cam_frame = None
        finally:
            if motion_sensor is not None:
                try:
                    motion_sensor.stop()
                    motion_sensor.close()
                except Exception:
                    pass
            try:
                pipeline.stop()
            except Exception:
                pass

        time.sleep(5.0)


# ── Odometry integration ─────────────────────────────────────────────────────

def _integrate_odometry(msg: dict) -> None:
    """AGV_ODOMETRY HW count → (x, y, θ) 누적 적분."""
    scale = _odom_state['unit_scale']   # counts/mm
    wbase = _odom_state['wheel_base']   # m
    pl_m = msg['pos_left_hw']  / scale / 1000.0
    pr_m = msg['pos_right_hw'] / scale / 1000.0
    if _odom_state['pos_left_prev'] is None:
        _odom_state['pos_left_prev']  = pl_m
        _odom_state['pos_right_prev'] = pr_m
        return
    dl = pl_m - _odom_state['pos_left_prev']
    dr = pr_m - _odom_state['pos_right_prev']
    _odom_state['pos_left_prev']  = pl_m
    _odom_state['pos_right_prev'] = pr_m
    dtheta = (dr - dl) / wbase
    dc     = (dl + dr) / 2.0
    _odom_state['theta'] += dtheta
    _odom_state['x'] += dc * math.cos(_odom_state['theta'])
    _odom_state['y'] += dc * math.sin(_odom_state['theta'])


async def _send_agv_velocity(linear: float, angular: float) -> None:
    """시리얼 연결 시 AGV_VELOCITY 패킷 송신 (drain 포함)."""
    if _serial_writer is None:
        log.warning("GoToGoal: serial disconnected — velocity cmd dropped (%.3f, %.3f)", linear, angular)
        return
    try:
        _serial_writer.write(slip_encode(pkt.build_agv_velocity(linear, angular)))
        await _serial_writer.drain()
    except Exception as exc:
        log.warning("GoToGoal: send failed: %s", exc)


# ── Go-To-Goal P controller ───────────────────────────────────────────────────

_GOAL_TOL    = 0.30    # m   — 도달 판정 반경
_HEAD_TOL    = 0.15    # rad — 이 이하면 주행 시작
_HEAD_DECEL  = 0.50    # rad — 이 이하부터 회전 감속 시작 (각속도 비례 감소)
_HEAD_RECORR = 0.25    # rad — 주행 중 이 초과 시 제자리 재회전 (히스테리시스)
_MAX_LINEAR  = 0.30    # m/s
_MAX_ANGULAR = 0.50    # rad/s
_KP_LINEAR   = 0.60
_KP_ANGULAR  = 0.80


async def _goto_goal_loop() -> None:
    global _goal_state
    try:
        while _goal_state['active']:
            await asyncio.sleep(0.05)   # 20 Hz
            o  = _odom_state
            dx = _goal_state['x'] - o['x']
            dy = _goal_state['y'] - o['y']
            dist = math.hypot(dx, dy)

            if dist < _GOAL_TOL:
                await _send_agv_velocity(0.0, 0.0)
                _goal_state['state']  = 'ARRIVED'
                _goal_state['active'] = False
                await _broadcast({'type': 'nav_goal_status', 'state': 'ARRIVED',
                                  'distance': 0.0, 'goal_x': _goal_state['x'],
                                  'goal_y': _goal_state['y'],
                                  'x': round(o['x'], 3), 'y': round(o['y'], 3),
                                  'theta': round(o['theta'], 3)})
                log.info("GoToGoal ARRIVED at (%.2f, %.2f)", _goal_state['x'], _goal_state['y'])
                break

            target_h = math.atan2(dy, dx)
            he = math.atan2(math.sin(target_h - o['theta']),
                            math.cos(target_h - o['theta']))

            max_lin = _odom_state.get('max_linear',  _MAX_LINEAR)
            max_ang = _odom_state.get('max_angular', _MAX_ANGULAR)

            # 회전 감속: HEAD_DECEL 이하에서 max_ang를 비례 감소
            ang_scale   = min(1.0, abs(he) / _HEAD_DECEL) if abs(he) < _HEAD_DECEL else 1.0
            eff_max_ang = max_ang * ang_scale

            # 히스테리시스: DRIVING 중 HEAD_RECORR 초과 시 재회전
            driving = (_goal_state['state'] == 'DRIVING')
            do_rotate = abs(he) > (_HEAD_RECORR if driving else _HEAD_TOL)

            if do_rotate:
                linear  = 0.0
                angular = max(-eff_max_ang, min(eff_max_ang, _KP_ANGULAR * he))
                _goal_state['state'] = 'ROTATING'
            else:
                linear  = min(max_lin, _KP_LINEAR * dist)
                angular = max(-eff_max_ang, min(eff_max_ang, _KP_ANGULAR * he))
                _goal_state['state'] = 'DRIVING'

            log.info("GoToGoal [%s] odom(%.2f,%.2f,%.1f°) goal(%.2f,%.2f) dist=%.2f lin=%.2f ang=%.2f",
                     _goal_state['state'],
                     o['x'], o['y'], math.degrees(o['theta']),
                     _goal_state['x'], _goal_state['y'],
                     dist, linear, angular)
            await _send_agv_velocity(linear, angular)
            await _broadcast({'type': 'nav_goal_status',
                              'state': _goal_state['state'],
                              'distance': round(dist, 3),
                              'goal_x': _goal_state['x'], 'goal_y': _goal_state['y'],
                              'x': round(o['x'], 3), 'y': round(o['y'], 3),
                              'theta': round(o['theta'], 3)})
    except asyncio.CancelledError:
        pass
    finally:
        if _serial_writer is not None:
            _serial_writer.write(slip_encode(pkt.build_agv_velocity(0.0, 0.0)))


async def _set_goal(x: float, y: float) -> None:
    global _goal_state, _goal_task
    if _goal_task and not _goal_task.done():
        _goal_task.cancel()
        try: await _goal_task
        except asyncio.CancelledError: pass
    _goal_state.update({'active': True, 'state': 'ROTATING', 'x': x, 'y': y})
    # 드라이브 활성화 여부 경고
    if _serial_writer is None:
        log.warning("GoToGoal SET: serial not connected — velocity commands will be dropped!")
        await _broadcast({'type': 'log', 'msg':
            '[GoToGoal] 경고: 시리얼 미연결. Connect 후 Run Enable 활성화 필요.'})
    elif _last_status is not None:
        re_l = _last_status.get('run_enable_left',  False)
        re_r = _last_status.get('run_enable_right', False)
        if not (re_l and re_r):
            log.warning("GoToGoal SET: drives not run-enabled (L=%s R=%s)", re_l, re_r)
            await _broadcast({'type': 'log', 'msg':
                '[GoToGoal] 경고: 드라이브 Run Enable 비활성. HMI에서 드라이브 활성화 먼저!'})

    _goal_task = asyncio.ensure_future(_goto_goal_loop())
    await _broadcast({'type': 'nav_goal_status', 'state': 'ROTATING',
                      'distance': math.hypot(x - _odom_state['x'], y - _odom_state['y']),
                      'goal_x': x, 'goal_y': y,
                      'x': round(_odom_state['x'], 3), 'y': round(_odom_state['y'], 3),
                      'theta': round(_odom_state['theta'], 3)})
    log.info("GoToGoal SET: x=%.2f y=%.2f dist=%.2f",
             x, y, math.hypot(x - _odom_state['x'], y - _odom_state['y']))


async def _cancel_goal() -> None:
    global _goal_task
    _goal_state['active'] = False
    if _goal_task and not _goal_task.done():
        _goal_task.cancel()
        try: await _goal_task
        except asyncio.CancelledError: pass
    await _send_agv_velocity(0.0, 0.0)
    _goal_state['state'] = 'IDLE'
    await _broadcast({'type': 'nav_goal_status', 'state': 'IDLE',
                      'distance': 0.0, 'goal_x': 0.0, 'goal_y': 0.0,
                      'x': round(_odom_state['x'], 3),
                      'y': round(_odom_state['y'], 3),
                      'theta': round(_odom_state['theta'], 3)})
    log.info("GoToGoal CANCELLED")


async def _run_route_loop(waypoints: list) -> None:
    global _route_state
    try:
        for i, wp in enumerate(waypoints):
            if not _route_state['active']:
                break
            x, y = float(wp[0]), float(wp[1])
            _route_state.update({'done': i, 'total': len(waypoints), 'state': 'DRIVING'})
            await _broadcast({'type': 'route_status', 'state': 'DRIVING',
                              'done': i, 'total': len(waypoints)})
            log.info("Route: waypoint %d/%d → (%.2f, %.2f)", i + 1, len(waypoints), x, y)
            await _set_goal(x, y)
            # Wait for GoToGoal to finish (ARRIVED) or route cancelled
            while _route_state['active']:
                await asyncio.sleep(0.1)
                if not _goal_state['active'] and _goal_state['state'] == 'ARRIVED':
                    break
        if _route_state['active']:
            _route_state.update({'active': False, 'state': 'DONE',
                                 'done': len(waypoints), 'total': len(waypoints)})
            await _broadcast({'type': 'route_status', 'state': 'DONE',
                              'done': len(waypoints), 'total': len(waypoints)})
            log.info("Route: all %d waypoints reached", len(waypoints))
    except asyncio.CancelledError:
        pass
    finally:
        _route_state['active'] = False


async def _start_route(waypoints: list) -> None:
    global _route_state, _route_task
    await _cancel_goal()
    if _route_task and not _route_task.done():
        _route_task.cancel()
        try: await _route_task
        except asyncio.CancelledError: pass
    _route_state.update({'active': True, 'state': 'STARTING',
                         'done': 0, 'total': len(waypoints)})
    _route_task = asyncio.ensure_future(_run_route_loop(waypoints))
    await _broadcast({'type': 'route_status', 'state': 'STARTING',
                      'done': 0, 'total': len(waypoints)})
    log.info("Route STARTED: %d waypoints", len(waypoints))


async def _stop_route() -> None:
    global _route_task
    _route_state['active'] = False
    if _route_task and not _route_task.done():
        _route_task.cancel()
        try: await _route_task
        except asyncio.CancelledError: pass
    await _cancel_goal()
    _route_state['state'] = 'IDLE'
    await _broadcast({'type': 'route_status', 'state': 'IDLE', 'done': 0, 'total': 0})
    log.info("Route STOPPED")


def _append_autonomy_log(line: str) -> None:
    text = line.rstrip()
    if not text:
        return
    with _autonomy_lock:
        _autonomy_log.append(text)
        if len(_autonomy_log) > 200:
            del _autonomy_log[:-200]


def _autonomy_status() -> dict:
    with _autonomy_lock:
        proc = _autonomy_proc
        running = proc is not None and proc.poll() is None
        return {
            "running": running,
            "mode": _autonomy_mode,
            "pid": proc.pid if running else None,
            "db_path": _autonomy_db_path,
            "started_at": _autonomy_started_at if running else 0.0,
            "uptime_sec": round(time.time() - _autonomy_started_at, 1) if running else 0.0,
            "last_exit_code": _autonomy_exit_code,
            "log_tail": _autonomy_log[-40:],
        }


def _autonomy_reader(proc: subprocess.Popen, mode: str) -> None:
    try:
        assert proc.stdout is not None
        for line in proc.stdout:
            _append_autonomy_log(line)
    finally:
        rc = proc.wait()
        with _autonomy_lock:
            global _autonomy_proc, _autonomy_mode, _autonomy_exit_code
            if _autonomy_proc is proc:
                _autonomy_proc = None
                _autonomy_mode = "IDLE"
                _autonomy_exit_code = rc
        _append_autonomy_log(f"[{mode}] exited with code {rc}")


def _stop_autonomy() -> dict:
    global _autonomy_proc, _autonomy_mode, _autonomy_exit_code
    with _autonomy_lock:
        proc = _autonomy_proc
        if proc is None or proc.poll() is not None:
            _autonomy_proc = None
            _autonomy_mode = "IDLE"
            return {"ok": True, "msg": "already stopped"}
        mode = _autonomy_mode
    try:
        os.killpg(proc.pid, signal.SIGTERM)
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait(timeout=5)
    except ProcessLookupError:
        pass
    with _autonomy_lock:
        _autonomy_proc = None
        _autonomy_mode = "IDLE"
        _autonomy_exit_code = proc.returncode
    _append_autonomy_log(f"[{mode}] stopped")
    return {"ok": True, "msg": f"stopped {mode.lower()} stack"}


def _start_autonomy(mode: str, db_path: Optional[str] = None) -> dict:
    global _autonomy_proc, _autonomy_mode, _autonomy_db_path, _autonomy_started_at, _autonomy_exit_code
    db_target = os.path.expanduser((db_path or _DEFAULT_MAP_DB).strip() or _DEFAULT_MAP_DB)
    script = os.path.join(_ROS2_WS_DIR, f"start_{'slam' if mode == 'SLAM' else 'nav'}.sh")
    if not os.path.exists(script):
        return {"ok": False, "msg": f"script not found: {script}"}
    with _autonomy_lock:
        proc = _autonomy_proc
        if proc is not None and proc.poll() is None:
            if _autonomy_mode == mode and _autonomy_db_path == db_target:
                return {"ok": True, "msg": f"{mode.lower()} already running"}
    _stop_autonomy()
    env = os.environ.copy()
    env["DB_PATH"] = db_target
    proc = subprocess.Popen(
        ["bash", script],
        cwd=_ROS2_WS_DIR,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        preexec_fn=os.setsid,
    )
    with _autonomy_lock:
        _autonomy_proc = proc
        _autonomy_mode = mode
        _autonomy_db_path = db_target
        _autonomy_started_at = time.time()
        _autonomy_exit_code = None
        _autonomy_log.clear()
    _append_autonomy_log(f"[{mode}] starting with db {db_target}")
    threading.Thread(target=_autonomy_reader, args=(proc, mode), daemon=True).start()
    return {"ok": True, "msg": f"started {mode.lower()} stack", "db_path": db_target}


# ── RX frame dispatcher ───────────────────────────────────────────────────────

def _dispatch_rx(raw: bytes) -> Optional[dict]:
    """
    Parse a raw SLIP-decoded frame.
    Returns a JSON-serialisable dict to broadcast, or None on error.
    """
    result = pkt.parse_frame(raw)
    if result is None:
        log.warning("CRC error — dropped frame len=%d hex=%s",
                    len(raw), raw[:8].hex())
        return None

    pkt_type, _seq, payload = result
    pkt_name = pkt.PKT_NAMES.get(pkt_type, f"0x{pkt_type:02X}")

    if pkt_type == pkt.PKT_STATUS:
        s = pkt.parse_status(payload)
        if s is None:
            log.warning("STATUS parse failed len=%d", len(payload))
            return None
        return s.to_dict()

    if pkt_type == pkt.PKT_ACK:
        a = pkt.parse_ack(payload)
        if a is None:
            log.warning("ACK parse failed len=%d", len(payload))
            return None
        log.debug("ACK seq=%d result=%s", a.seq, a.result_name)
        return a.to_dict()

    if pkt_type == pkt.PKT_LOG:
        msg = pkt.parse_log(payload)
        log.info("[STM32] %s", msg)
        return {"type": "log", "msg": msg}

    if pkt_type == pkt.PKT_PARAM_REPORT:
        r = pkt.parse_param_report(payload)
        if r is None:
            log.warning("PARAM_REPORT parse failed len=%d", len(payload))
            return None
        return r.to_dict()

    if pkt_type == pkt.PKT_AGV_ODOMETRY:
        odo = pkt.parse_agv_odometry(payload)
        if odo is None:
            log.warning("AGV_ODOMETRY parse failed len=%d", len(payload))
            return None
        msg = odo.to_dict()
        _integrate_odometry(msg)
        msg['odom_x']     = round(_odom_state['x'],     4)
        msg['odom_y']     = round(_odom_state['y'],     4)
        msg['odom_theta'] = round(_odom_state['theta'], 4)
        return msg

    if pkt_type == pkt.PKT_AGV_STATUS:
        st = pkt.parse_agv_status(payload)
        if st is None:
            log.warning("AGV_STATUS parse failed len=%d", len(payload))
            return None
        return st.to_dict()

    log.debug("Unknown pkt_type=%s payload_len=%d", pkt_name, len(payload))
    return None


# ── WebSocket broadcast ───────────────────────────────────────────────────────

async def _broadcast(msg: dict) -> None:
    global _last_status, _last_params
    msg_type = msg.get("type")
    if msg_type == "status":
        _last_status = msg
    elif msg_type == "param_report":
        _last_params = msg

    if not _clients:
        return
    text = json.dumps(msg, separators=(",", ":"))
    dead: set[WebSocketServerProtocol] = set()
    for ws in list(_clients):
        try:
            await ws.send(text)
        except websockets.exceptions.ConnectionClosed:
            dead.add(ws)
    _clients.difference_update(dead)


# ── Command dispatch (client → STM32) ────────────────────────────────────────

def _build_cmd_frame(data: dict) -> Optional[bytes]:
    """Convert a WebSocket JSON command dict to a raw (pre-SLIP) binary frame."""
    cmd = data.get("cmd", "")
    try:
        if cmd == "home":
            return pkt.build_home(
                int(data.get("axis", 0xFF)),
                int(data.get("type",  pkt.HOME_SET_CURRENT)),
            )
        if cmd == "set_param":
            return pkt.build_set_param(
                int(data["axis"]),
                int(data["param_id"]),
                int(data["value"]),
            )
        if cmd == "save_flash":
            return pkt.build_save_flash()
        if cmd == "stop":
            return pkt.build_stop()
        if cmd == "run_enable":
            return pkt.build_run_enable(
                int(data.get("axis", 0xFF)),
                bool(data.get("enable", False)),
            )
        if cmd == "param_read_req":
            return pkt.build_param_read_req()
        if cmd == "fault_reset":
            return pkt.build_fault_reset(int(data.get("axis", 0xFF)))
        if cmd == "agv_velocity":
            return pkt.build_agv_velocity(
                float(data.get("linear_mps",  0.0)),
                float(data.get("angular_rps", 0.0)),
            )
    except (KeyError, ValueError, TypeError) as exc:
        log.warning("cmd dispatch error cmd=%r: %s", cmd, exc)
        return None

    log.warning("Unknown cmd=%r", cmd)
    return None


# ── WebSocket connection handler ──────────────────────────────────────────────

async def _ws_handler(ws: WebSocketServerProtocol) -> None:
    remote = ws.remote_address
    log.info("WS connected  %s:%s", *remote)
    _clients.add(ws)

    # Push cached state immediately so the client is not blank on connect
    if _last_status is not None:
        try:
            await ws.send(json.dumps(_last_status, separators=(",", ":")))
        except websockets.exceptions.ConnectionClosed:
            _clients.discard(ws)
            return
    if _last_params is not None:
        try:
            await ws.send(json.dumps(_last_params, separators=(",", ":")))
        except websockets.exceptions.ConnectionClosed:
            _clients.discard(ws)
            return
    # Push nav params (unit_scale / wheel_base) so UI shows correct values after F5
    try:
        await ws.send(json.dumps({
            'type': 'nav_params',
            'unit_scale': _odom_state['unit_scale'],
            'wheel_base':  _odom_state['wheel_base'],
        }, separators=(",", ":")))
    except websockets.exceptions.ConnectionClosed:
        _clients.discard(ws)
        return
    # Push current odom so UI shows real AGV position immediately (not 0,0) after F5
    try:
        await ws.send(json.dumps({
            'type':       'agv_odometry',
            'odom_x':     round(_odom_state['x'],     4),
            'odom_y':     round(_odom_state['y'],     4),
            'odom_theta': round(_odom_state['theta'], 4),
        }, separators=(",", ":")))
    except websockets.exceptions.ConnectionClosed:
        _clients.discard(ws)
        return

    try:
        async for raw_msg in ws:
            try:
                data = json.loads(raw_msg)
            except json.JSONDecodeError:
                log.warning("Bad JSON from %s:%s: %.80s", *remote, raw_msg)
                continue

            # Bridge-level command: switch serial port
            if data.get("cmd") == "set_port":
                port = data.get("port", "").strip()
                if port:
                    await _switch_port(port)
                continue

            # Go-To-Goal direct controller commands
            if data.get("cmd") == "nav_goal_set":
                await _set_goal(float(data.get('x', 0.0)), float(data.get('y', 0.0)))
                continue
            if data.get("cmd") == "nav_goal_cancel":
                await _cancel_goal()
                continue
            if data.get("cmd") == "reset_odom":
                _odom_state.update({'x': 0.0, 'y': 0.0, 'theta': 0.0,
                                    'pos_left_prev': None, 'pos_right_prev': None})
                await _broadcast({'type': 'odom_reset_ack'})
                log.info("Odometry reset")
                continue
            if data.get("cmd") == "nav_set_params":
                if 'unit_scale'  in data: _odom_state['unit_scale']  = float(data['unit_scale'])
                if 'wheel_base'  in data: _odom_state['wheel_base']  = float(data['wheel_base'])
                if 'max_linear'  in data: _odom_state['max_linear']  = float(data['max_linear'])
                if 'max_angular' in data: _odom_state['max_angular'] = float(data['max_angular'])
                _save_nav_params()
                await _broadcast({'type': 'nav_params_saved',
                                  'unit_scale':  _odom_state['unit_scale'],
                                  'wheel_base':  _odom_state['wheel_base'],
                                  'max_linear':  _odom_state['max_linear'],
                                  'max_angular': _odom_state['max_angular']})
                continue

            if data.get("cmd") == "start_route":
                wps = data.get('waypoints', [])
                if wps:
                    await _start_route(wps)
                continue
            if data.get("cmd") == "stop_route":
                await _stop_route()
                continue

            if data.get("type") or data.get("cmd") in {
                "ros_publish", "request_map_path",
            }:
                await _broadcast(data)
                continue

            frame = _build_cmd_frame(data)
            if frame is None:
                continue

            if _serial_writer is not None:
                slip_frame = slip_encode(frame)
                _serial_writer.write(slip_frame)
                try:
                    await _serial_writer.drain()
                except Exception as exc:
                    log.error("Serial write error: %s", exc)
            else:
                log.warning("Serial not connected — dropped cmd %r", data.get("cmd"))

    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        _clients.discard(ws)
        log.info("WS disconnected %s:%s", *remote)


# ── Dynamic port switching ────────────────────────────────────────────────────

async def _switch_port(port: str) -> None:
    global _current_port, _serial_task
    if port == _current_port and _serial_task and not _serial_task.done():
        log.info("Port already %s — reconnecting", port)
    _current_port = port
    if _serial_task and not _serial_task.done():
        _serial_task.cancel()
        try:
            await _serial_task
        except asyncio.CancelledError:
            pass
    _serial_task = asyncio.get_event_loop().create_task(
        _serial_loop(port, _baud_rate, _reconnect_delay_g)
    )
    log.info("Serial switched to %s", port)


# ── HTTP server (threading — MJPEG 스트리밍이 다른 요청을 블록하지 않도록) ──────

class _ThreadingHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    """각 요청마다 스레드를 생성해 MJPEG 스트리밍과 일반 API를 동시 처리."""
    daemon_threads   = True
    allow_reuse_address = True


# ── HTTP handler with /ports endpoint ────────────────────────────────────────

class _HttpHandler(http.server.SimpleHTTPRequestHandler):
    def _send_json(self, payload: dict, status: int = 200) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> dict:
        length = int(self.headers.get('Content-Length', '0') or 0)
        if length <= 0:
            return {}
        raw = self.rfile.read(length)
        try:
            return json.loads(raw.decode('utf-8'))
        except Exception:
            return {}

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate, max-age=0')
        self.send_header('Pragma', 'no-cache')
        self.send_header('Expires', '0')
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def do_GET(self):
        path = self.path.split('?')[0]   # 쿼리스트링 제거
        if path == '/ports':
            if _has_list_ports:
                ports = sorted(p.device for p in _list_ports.comports())
            else:
                ports = []
            if _current_port and _current_port not in ports:
                ports.insert(0, _current_port)
            body = json.dumps(
                {"ports": ports, "current": _current_port}
            ).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(body)
            return
        if path == '/autonomy/status':
            self._send_json(_autonomy_status())
            return
        if path == '/camera/status':
            with _cam_lock:
                info = dict(_cam_info)
            self._send_json(info)
            return
        if path == '/camera/imu':
            with _cam_lock:
                imu = dict(_cam_imu)
            imu['fresh'] = imu.get('ts', 0.0) > time.time() - 2.0
            self._send_json(imu)
            return
        if path == '/camera/stream':
            self._handle_mjpeg_stream()
            return
        if path == '/nav/status':
            self._send_json({
                'state':   _goal_state['state'],
                'active':  _goal_state['active'],
                'goal_x':  _goal_state['x'],
                'goal_y':  _goal_state['y'],
                'x':       _odom_state['x'],
                'y':       _odom_state['y'],
                'theta':   _odom_state['theta'],
            })
            return
        super().do_GET()

    def do_POST(self):
        payload = self._read_json()
        if self.path == '/autonomy/start_slam':
            self._send_json(_start_autonomy('SLAM', payload.get('db_path')))
            return
        if self.path == '/autonomy/start_nav':
            self._send_json(_start_autonomy('NAV', payload.get('db_path')))
            return
        if self.path == '/autonomy/stop':
            self._send_json(_stop_autonomy())
            return
        if self.path == '/nav/goal':
            try:
                x = float(payload.get('x', 0.0))
                y = float(payload.get('y', 0.0))
                if _loop:
                    asyncio.run_coroutine_threadsafe(_set_goal(x, y), _loop)
                self._send_json({'ok': True, 'x': x, 'y': y})
            except (TypeError, ValueError) as e:
                self._send_json({'ok': False, 'msg': str(e)}, 400)
            return
        if self.path == '/nav/cancel':
            if _loop:
                asyncio.run_coroutine_threadsafe(_cancel_goal(), _loop)
            self._send_json({'ok': True})
            return
        self._send_json({'ok': False, 'msg': 'not found'}, 404)

    def _handle_mjpeg_stream(self) -> None:
        """multipart/x-mixed-replace MJPEG 스트리밍 — 클라이언트가 끊을 때까지 전송."""
        self.send_response(200)
        self.send_header('Content-Type',
                         'multipart/x-mixed-replace; boundary=AGVcamframe')
        self.send_header('Cache-Control', 'no-cache, no-store')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        try:
            while True:
                with _cam_lock:
                    frame = _cam_frame
                if frame:
                    hdr = (
                        b'--AGVcamframe\r\n'
                        b'Content-Type: image/jpeg\r\n'
                        b'Content-Length: ' + str(len(frame)).encode() + b'\r\n\r\n'
                    )
                    self.wfile.write(hdr + frame + b'\r\n')
                    self.wfile.flush()
                time.sleep(1.0 / 15.0)   # ~15 fps
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception as exc:
            log.debug("MJPEG stream closed: %s", exc)

    def log_message(self, fmt, *args):
        pass  # suppress per-request HTTP logs


# ── Serial reader / reconnect loop ───────────────────────────────────────────

async def _serial_loop(port: str, baud: int, reconnect_delay: float = 3.0) -> None:
    """
    Continuously attempt to open the serial port.
    On each connection: read bytes → SLIP decode → parse → broadcast.
    Automatically reconnects after any error.
    """
    global _serial_writer

    while True:
        try:
            log.info("Opening serial %s @ %d baud …", port, baud)
            reader, writer = await serial_asyncio.open_serial_connection(
                url=port, baudrate=baud,
            )
            _serial_writer = writer
            pkt.reset_seq()
            log.info("Serial open: %s", port)
            await _broadcast({"type": "connected", "port": port, "baud": baud})

            decoder = SlipDecoder()
            while True:
                data = await reader.read(512)
                if not data:
                    raise ConnectionResetError("serial EOF")
                decoder.feed(data)
                for raw_frame in decoder.frames():
                    msg = _dispatch_rx(raw_frame)
                    if msg is not None:
                        await _broadcast(msg)

        except asyncio.CancelledError:
            raise
        except Exception as exc:
            log.error("Serial error: %s — reconnect in %.0fs", exc, reconnect_delay)
        finally:
            _serial_writer = None

        await _broadcast({"type": "disconnected"})
        await asyncio.sleep(reconnect_delay)


# ── HTTP file server (serves index.html) ─────────────────────────────────────

def _get_www_dir() -> str:
    """웹 파일 디렉토리 반환. PyInstaller 번들 모드와 스크립트 모드 모두 지원."""
    if getattr(sys, 'frozen', False) and hasattr(sys, '_MEIPASS'):
        return sys._MEIPASS          # PyInstaller 번들: 임시 압축 해제 디렉토리
    return os.path.dirname(os.path.abspath(__file__))  # 일반 스크립트 실행


def _start_http_server(port: int) -> None:
    """Serve the bridge directory over HTTP in a background thread (threaded)."""
    bridge_dir = _get_www_dir()
    handler = functools.partial(_HttpHandler, directory=bridge_dir)
    with _ThreadingHTTPServer(("", port), handler) as srv:
        log.info("HTTP server (threaded): http://localhost:%d/", port)
        srv.serve_forever()


# ── Entry point ───────────────────────────────────────────────────────────────

async def _run(args: argparse.Namespace) -> None:
    global _current_port, _baud_rate, _reconnect_delay_g, _serial_task, _loop

    _loop = asyncio.get_event_loop()
    _current_port     = args.port
    _baud_rate        = args.baud
    _reconnect_delay_g = args.reconnect

    # HTTP server in a daemon thread (dies when main process exits)
    t = threading.Thread(target=_start_http_server, args=(args.http_port,), daemon=True)
    t.start()

    # RealSense D435i 카메라 캡처 스레드 (카메라 없으면 graceful 종료)
    cam_t = threading.Thread(target=_camera_loop, daemon=True, name="camera-loop")
    cam_t.start()

    ws_server = await websockets.serve(
        _ws_handler,
        args.ws_host,
        args.ws_port,
        ping_interval=20,
        ping_timeout=10,
        max_size=None,
    )
    log.info("WebSocket server: ws://%s:%d", args.ws_host, args.ws_port)

    # Serial loop runs as a cancellable task (set_port can restart it)
    _serial_task = asyncio.get_event_loop().create_task(
        _serial_loop(_current_port, _baud_rate, _reconnect_delay_g)
    )
    await ws_server.wait_closed()


def main() -> None:
    if serial_asyncio is None:
        raise SystemExit(
            "serial_asyncio not installed.\n"
            "Run: pip install pyserial-asyncio"
        )

    parser = argparse.ArgumentParser(
        description="STM32 robot — serial ↔ WebSocket bridge",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--port",      default=None,        metavar="PORT",
                        help="Serial port. 생략 시 STM32 VCP 자동 탐색 "
                             "(예: COM3, /dev/ttyACM0)")
    parser.add_argument("--baud",      default=921600, type=int,
                        help="Baud rate")
    parser.add_argument("--ws-host",   default="0.0.0.0",   metavar="HOST",
                        help="WebSocket bind address")
    parser.add_argument("--ws-port",   default=8765,   type=int, metavar="PORT",
                        help="WebSocket port")
    parser.add_argument("--http-port", default=5100,   type=int, metavar="PORT",
                        help="HTTP port for serving index.html")
    parser.add_argument("--reconnect", default=3.0,    type=float, metavar="SEC",
                        help="Seconds between serial reconnect attempts")
    parser.add_argument("--verbose",   action="store_true",
                        help="Enable DEBUG logging")
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s  %(levelname)-7s  %(name)s: %(message)s",
        datefmt="%H:%M:%S",
    )

    # --port 생략 시 자동 탐색
    if args.port is None:
        args.port = _auto_detect_port()

    # Linux 권한 확인
    _check_linux_permissions(args.port)

    # 시작 배너 출력
    _print_startup_banner(args.port, args.ws_port, args.http_port)

    try:
        asyncio.run(_run(args))
    except KeyboardInterrupt:
        log.info("Bridge stopped.")


if __name__ == "__main__":
    main()
