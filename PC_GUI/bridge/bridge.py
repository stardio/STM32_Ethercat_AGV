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
import os
import sys
import platform
import threading
import time
from typing import Optional

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
        return odo.to_dict()

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


# ── HTTP handler with /ports endpoint ────────────────────────────────────────

class _HttpHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/ports':
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
        super().do_GET()

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
    """Serve the bridge directory over HTTP in a background thread."""
    bridge_dir = _get_www_dir()
    handler = functools.partial(_HttpHandler, directory=bridge_dir)
    with http.server.HTTPServer(("", port), handler) as srv:
        log.info("HTTP server: http://localhost:%d/", port)
        srv.serve_forever()


# ── Entry point ───────────────────────────────────────────────────────────────

async def _run(args: argparse.Namespace) -> None:
    global _current_port, _baud_rate, _reconnect_delay_g, _serial_task

    _current_port     = args.port
    _baud_rate        = args.baud
    _reconnect_delay_g = args.reconnect

    # HTTP server in a daemon thread (dies when main process exits)
    t = threading.Thread(target=_start_http_server, args=(args.http_port,), daemon=True)
    t.start()

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
