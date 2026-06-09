"""
packet_defs.py — Binary UART protocol frame builder/parser.

Wire frame format (inside SLIP delimiters):
  [pkt_type: 1B] [seq: 1B] [payload: 0..N B] [crc16: 2B LE]
  CRC16-CCITT (poly 0x1021, init 0xFFFF) covers [pkt_type + seq + payload].

Packet sizes (payload only):
  AxisStatusPkt_t           = 12 bytes  (int32 pos_centi, int16 vel, int16 torq,
                                          uint16 statusword, uint8 cia402, uint8 flags)
  ProtoPktStatus_t          = 74 bytes  (6 × AxisStatusPkt_t + 2 bytes sys)
  ProtoAxisParam_t          = 44 bytes  (packed, matches uart_protocol.h)
  ProtoPktParamReport_t     = 264 bytes (6 × ProtoAxisParam_t)
  ProtoPktFaultReset_t      =  1 byte   (axis)
  ProtoPktAgvVelocity_t     =  8 bytes
  ProtoPktAgvOdometry_t     = 20 bytes
  ProtoPktAgvStatus_t       =  4 bytes

pos_centi unit: degree × 100 (centi-degree) for display.
"""

import struct
import time
from dataclasses import dataclass
from typing import Optional, List

AXIS_COUNT = 6

# ── Packet type IDs ──────────────────────────────────────────────────────────

# STM32 → Bridge
PKT_STATUS              = 0x01
PKT_PARAM_REPORT        = 0x02
PKT_ACK                 = 0x03
PKT_LOG                 = 0x04

# Bridge → STM32
PKT_HOME                = 0x14
PKT_SET_PARAM           = 0x15
PKT_SAVE_FLASH          = 0x16
PKT_STOP                = 0x17
PKT_RUN_ENABLE          = 0x18
PKT_PARAM_READ_REQ      = 0x19
PKT_FAULT_RESET         = 0x22   # force CiA402 fault recovery

# AGV packets
PKT_AGV_VELOCITY        = 0x30   # Bridge → STM32: differential drive command
PKT_AGV_ODOMETRY        = 0x31   # STM32 → Bridge: wheel encoder snapshot (10 ms)
PKT_AGV_STATUS          = 0x32   # STM32 → Bridge: AGV drive health (10 ms)

PKT_NAMES = {
    0x01: "STATUS", 0x02: "PARAM_REPORT", 0x03: "ACK", 0x04: "LOG",
    0x14: "HOME", 0x15: "SET_PARAM", 0x16: "SAVE_FLASH", 0x17: "STOP",
    0x18: "RUN_ENABLE", 0x19: "PARAM_READ_REQ",
    0x22: "FAULT_RESET",
    0x30: "AGV_VELOCITY", 0x31: "AGV_ODOMETRY", 0x32: "AGV_STATUS",
}

# ── Result codes ─────────────────────────────────────────────────────────────

RESULT_OK        = 0x00
RESULT_BUSY      = 0x01
RESULT_BAD_PARAM = 0x02
RESULT_NOT_READY = 0x03
RESULT_FLASH_ERR = 0x04
RESULT_BAD_PKT   = 0xFF

RESULT_NAMES = {
    0x00: "OK", 0x01: "BUSY", 0x02: "BAD_PARAM",
    0x03: "NOT_READY", 0x04: "FLASH_ERR", 0xFF: "BAD_PKT",
}

# ── Parameter IDs ─────────────────────────────────────────────────────────────

PARAM_UNIT_SCALE       = 0x01
PARAM_HOME_OFFSET      = 0x02
PARAM_LIMIT_PLUS_HW    = 0x03
PARAM_LIMIT_MINUS_HW   = 0x04
PARAM_LIMIT_PLUS_USER  = 0x05
PARAM_LIMIT_MINUS_USER = 0x06
PARAM_PROFILE_VEL      = 0x07
PARAM_PROFILE_ACCEL_MS = 0x08
PARAM_PROFILE_DECEL_MS = 0x09
PARAM_TORQUE_LIMIT     = 0x0A
PARAM_POSITION_GAIN    = 0x0B
PARAM_LIMITS_ENABLED   = 0x0C

PARAM_NAMES = {
    0x01: "unit_scale", 0x02: "home_offset",
    0x03: "limit_plus_hw", 0x04: "limit_minus_hw",
    0x05: "limit_plus_user", 0x06: "limit_minus_user",
    0x07: "profile_vel", 0x08: "profile_accel_ms", 0x09: "profile_decel_ms",
    0x0A: "torque_limit", 0x0B: "position_gain", 0x0C: "limits_enabled",
}

# ── Home types ────────────────────────────────────────────────────────────────

HOME_SET_CURRENT  = 0x00
HOME_RUN_SEQUENCE = 0x01

# ── Interpolator state names (mirrors InterpState_t) ─────────────────────────

INTERP_STATE_NAMES = {0: "IDLE", 1: "MOVING", 2: "DONE", 3: "ERROR"}

# ── CiA 402 state names ───────────────────────────────────────────────────────

CIA402_NAMES = {
    0: "NOT_READY", 1: "SW_DISABLED", 2: "READY", 3: "SWITCHED_ON",
    4: "OP_ENABLED", 5: "QUICK_STOP", 6: "FAULT_REACT", 7: "FAULT",
    255: "UNKNOWN",
}

# ── Joint names (J1..J6) ─────────────────────────────────────────────────────

JOINT_NAMES = ["J1", "J2", "J3", "J4", "J5", "J6"]

# ── CRC16-CCITT (poly 0x1021, init 0xFFFF, no final XOR) ────────────────────

def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
        crc &= 0xFFFF
    return crc

# ── TX sequence counter ───────────────────────────────────────────────────────

_tx_seq: int = 0


def reset_seq() -> None:
    """Reset TX sequence counter — call after serial reconnect."""
    global _tx_seq
    _tx_seq = 0


# ── Frame builder / parser ────────────────────────────────────────────────────

def build_frame(pkt_type: int, payload: bytes = b"") -> bytes:
    """
    Build a raw (pre-SLIP) frame and auto-increment the TX sequence counter.
    Returns: [pkt_type:1] [seq:1] [payload:N] [crc16:2 LE]
    """
    global _tx_seq
    seq = _tx_seq & 0xFF
    _tx_seq = (_tx_seq + 1) & 0xFF
    raw = bytes([pkt_type, seq]) + payload
    crc = crc16_ccitt(raw)
    return raw + struct.pack("<H", crc)


def parse_frame(raw: bytes) -> Optional[tuple]:
    """
    Validate CRC and split a raw frame.
    Returns (pkt_type: int, seq: int, payload: bytes) on success, None on error.
    """
    if len(raw) < 4:
        return None
    crc_recv = struct.unpack_from("<H", raw, len(raw) - 2)[0]
    if crc16_ccitt(raw[:-2]) != crc_recv:
        return None
    return raw[0], raw[1], raw[2:-2]

# ── RX data classes ───────────────────────────────────────────────────────────

# AxisStatusPkt_t (packed) — 12 bytes
# int32 pos_centi, int16 velocity, int16 torque, uint16 statusword,
# uint8 cia402_state, uint8 flags
_AXIS_STATUS_FMT  = "<ihhHBB"
_AXIS_STATUS_SIZE = struct.calcsize(_AXIS_STATUS_FMT)  # 12

# flags bit positions
_FLAG_PDO_READY      = 0x01
_FLAG_TARGET_REACHED = 0x02
_FLAG_RUN_ENABLE     = 0x04


@dataclass
class AxisStatus:
    pos_deg:        float   # joint angle in degrees (converted from centi-degree)
    velocity:       int     # degree/s (clamped to int16)
    torque:         int     # per-mille (1000 = 100.0%)
    statusword:     int     # CiA 402 raw statusword
    cia402_state:   int     # Cia402State_t enum value
    pdo_ready:      bool
    target_reached: bool
    run_enable:     bool

    @property
    def cia402_name(self) -> str:
        return CIA402_NAMES.get(self.cia402_state, f"0x{self.cia402_state:02X}")

    def to_dict(self) -> dict:
        return {
            "pos_deg":        round(self.pos_deg, 4),
            "velocity":       self.velocity,
            "torque":         self.torque,
            "statusword":     self.statusword,
            "cia402":         self.cia402_name,
            "pdo_ready":      self.pdo_ready,
            "target_reached": self.target_reached,
            "run_enable":     self.run_enable,
        }


@dataclass
class StatusPacket:
    axis:                list   # [AxisStatus × 6]
    interp_state:        int
    all_ready:           bool
    all_targets_reached: bool
    active_axes:         int    # number of EtherCAT slaves detected (0 = unknown)

    @property
    def interp_name(self) -> str:
        return INTERP_STATE_NAMES.get(self.interp_state, f"{self.interp_state}")

    def to_dict(self) -> dict:
        return {
            "type":                "status",
            "ts":                  time.monotonic(),
            "axes":                [a.to_dict() for a in self.axis],
            "interp":              self.interp_name,
            "all_ready":           self.all_ready,
            "all_targets_reached": self.all_targets_reached,
            "active_axes":         self.active_axes,
        }


def parse_status(payload: bytes) -> Optional[StatusPacket]:
    """Parse a STATUS payload (74 bytes = 6 × 12 + 2)."""
    expected = _AXIS_STATUS_SIZE * AXIS_COUNT + 2
    if len(payload) != expected:
        return None
    axes = []
    for i in range(AXIS_COUNT):
        off = i * _AXIS_STATUS_SIZE
        pos_c, vel, torq, sw, cia, flags = struct.unpack_from(_AXIS_STATUS_FMT, payload, off)
        axes.append(AxisStatus(
            pos_deg        = pos_c / 100.0,
            velocity       = vel,
            torque         = torq,
            statusword     = sw,
            cia402_state   = cia,
            pdo_ready      = bool(flags & _FLAG_PDO_READY),
            target_reached = bool(flags & _FLAG_TARGET_REACHED),
            run_enable     = bool(flags & _FLAG_RUN_ENABLE),
        ))
    interp_state = payload[-2]
    sys_flags    = payload[-1]
    return StatusPacket(
        axis                = axes,
        interp_state        = interp_state,
        all_ready           = bool(sys_flags & 0x01),
        all_targets_reached = bool(sys_flags & 0x02),
        active_axes         = (sys_flags >> 4) & 0x07,  # 3 bits for 0-6
    )


# ProtoAxisParam_t (packed) — 44 bytes
# 9× int32, int16 torque_limit, int32 position_gain, 2× uint8
_AXIS_PARAM_FMT  = "<iiiiiiiiihiBB"
_AXIS_PARAM_SIZE = struct.calcsize(_AXIS_PARAM_FMT)  # 44


@dataclass
class AxisParam:
    unit_scale:       int
    home_offset:      int
    limit_plus_hw:    int
    limit_minus_hw:   int
    limit_plus_user:  int
    limit_minus_user: int
    profile_velocity: int
    profile_accel_ms: int
    profile_decel_ms: int
    torque_limit:     int   # int16 (per-mille)
    position_gain:    int
    limits_enabled:   bool
    limits_blocked:   bool

    def to_dict(self) -> dict:
        return {
            "unit_scale":       self.unit_scale,
            "home_offset":      self.home_offset,
            "limit_plus_hw":    self.limit_plus_hw,
            "limit_minus_hw":   self.limit_minus_hw,
            "limit_plus_user":  self.limit_plus_user,
            "limit_minus_user": self.limit_minus_user,
            "profile_velocity": self.profile_velocity,
            "profile_accel_ms": self.profile_accel_ms,
            "profile_decel_ms": self.profile_decel_ms,
            "torque_limit":     self.torque_limit,
            "position_gain":    self.position_gain,
            "limits_enabled":   self.limits_enabled,
            "limits_blocked":   self.limits_blocked,
        }


@dataclass
class ParamReport:
    axis: list  # [AxisParam × 6]

    def to_dict(self) -> dict:
        return {
            "type": "param_report",
            "axes": [p.to_dict() for p in self.axis],
        }


def parse_param_report(payload: bytes) -> Optional[ParamReport]:
    """Parse a PARAM_REPORT payload (264 bytes = 6 × 44)."""
    if len(payload) != _AXIS_PARAM_SIZE * AXIS_COUNT:
        return None
    params = []
    for i in range(AXIS_COUNT):
        off = i * _AXIS_PARAM_SIZE
        v = struct.unpack_from(_AXIS_PARAM_FMT, payload, off)
        params.append(AxisParam(
            unit_scale       = v[0],
            home_offset      = v[1],
            limit_plus_hw    = v[2],
            limit_minus_hw   = v[3],
            limit_plus_user  = v[4],
            limit_minus_user = v[5],
            profile_velocity = v[6],
            profile_accel_ms = v[7],
            profile_decel_ms = v[8],
            torque_limit     = v[9],
            position_gain    = v[10],
            limits_enabled   = bool(v[11]),
            limits_blocked   = bool(v[12]),
        ))
    return ParamReport(axis=params)


@dataclass
class AckPacket:
    seq:    int
    result: int

    @property
    def ok(self) -> bool:
        return self.result == RESULT_OK

    @property
    def result_name(self) -> str:
        return RESULT_NAMES.get(self.result, f"0x{self.result:02X}")

    def to_dict(self) -> dict:
        return {"type": "ack", "seq": self.seq, "result": self.result_name}


def parse_ack(payload: bytes) -> Optional[AckPacket]:
    if len(payload) != 2:
        return None
    return AckPacket(seq=payload[0], result=payload[1])


def parse_log(payload: bytes) -> str:
    return payload.decode("utf-8", errors="replace")


# ── TX packet builders ────────────────────────────────────────────────────────

def build_home(axis: int = 0xFF, home_type: int = HOME_SET_CURRENT) -> bytes:
    """
    HOME (0x14) — set home position.
    axis: 0..5 for single joint, 0xFF for all.
    home_type: HOME_SET_CURRENT (0) or HOME_RUN_SEQUENCE (1).
    """
    return build_frame(PKT_HOME, struct.pack("<BB", axis & 0xFF, home_type & 0xFF))


def build_set_param(axis: int, param_id: int, value: int) -> bytes:
    """
    SET_PARAM (0x15) — write one parameter to one axis.
    value is int32 (clamped by firmware).
    """
    return build_frame(PKT_SET_PARAM, struct.pack("<BB2xi", axis & 0xFF, param_id & 0xFF, value))


def build_save_flash() -> bytes:
    """SAVE_FLASH (0x16) — persist all parameters to flash."""
    return build_frame(PKT_SAVE_FLASH)


def build_stop() -> bytes:
    """STOP (0x17) — emergency stop all axes."""
    return build_frame(PKT_STOP)


def build_run_enable(axis: int = 0xFF, enable: bool = True) -> bytes:
    """
    RUN_ENABLE (0x18) — enable or disable drives.
    axis: 0..5 for single joint, 0xFF for all.
    """
    return build_frame(PKT_RUN_ENABLE, struct.pack("<BB", axis & 0xFF, 1 if enable else 0))


def build_param_read_req() -> bytes:
    """PARAM_READ_REQ (0x19) — request PARAM_REPORT from drives."""
    return build_frame(PKT_PARAM_READ_REQ)


def build_fault_reset(axis: int = 0xFF) -> bytes:
    """
    FAULT_RESET (0x22) — force CiA402 fault recovery and re-enable sequence.
    axis: 0..5 for single joint, 0xFF for all.
    """
    return build_frame(PKT_FAULT_RESET, struct.pack("<B", axis & 0xFF))


# ── AGV packet builders / parsers ─────────────────────────────────────────────

def build_agv_velocity(linear_mps: float, angular_rps: float) -> bytes:
    """
    AGV_VELOCITY (0x30) — differential drive velocity command.
    linear_mps  : forward speed [m/s]
    angular_rps : turning rate  [rad/s], positive = CCW
    """
    return build_frame(PKT_AGV_VELOCITY, struct.pack("<ff", linear_mps, angular_rps))


# AGV_ODOMETRY (0x31) — 20 bytes: 4× int32 + 1× uint32
_AGV_ODOMETRY_FMT  = "<iiiiI"
_AGV_ODOMETRY_SIZE = struct.calcsize(_AGV_ODOMETRY_FMT)   # 20


@dataclass
class AgvOdometry:
    pos_left_hw:   int    # AXIS_J1 encoder counts
    pos_right_hw:  int    # AXIS_J2 encoder counts
    vel_left_hw:   int    # AXIS_J1 velocity [HW counts/s]
    vel_right_hw:  int    # AXIS_J2 velocity [HW counts/s]
    timestamp_ms:  int    # HAL_GetTick() at capture time

    def to_dict(self) -> dict:
        return {
            "type":          "agv_odometry",
            "pos_left_hw":   self.pos_left_hw,
            "pos_right_hw":  self.pos_right_hw,
            "vel_left_hw":   self.vel_left_hw,
            "vel_right_hw":  self.vel_right_hw,
            "timestamp_ms":  self.timestamp_ms,
        }


def parse_agv_odometry(payload: bytes) -> Optional[AgvOdometry]:
    """Parse an AGV_ODOMETRY payload (20 bytes)."""
    if len(payload) != _AGV_ODOMETRY_SIZE:
        return None
    pl, pr, vl, vr, ts = struct.unpack(_AGV_ODOMETRY_FMT, payload)
    return AgvOdometry(pos_left_hw=pl, pos_right_hw=pr,
                       vel_left_hw=vl, vel_right_hw=vr, timestamp_ms=ts)


# AGV_STATUS (0x32) — 4 bytes: 4× uint8
_AGV_STATUS_FMT  = "<BBBB"
_AGV_STATUS_SIZE = struct.calcsize(_AGV_STATUS_FMT)   # 4


@dataclass
class AgvStatus:
    all_ready:     bool
    cia402_left:   int    # Cia402State_t of AXIS_J1
    cia402_right:  int    # Cia402State_t of AXIS_J2
    run_enable_left:  bool
    run_enable_right: bool

    def to_dict(self) -> dict:
        return {
            "type":              "agv_status",
            "all_ready":         self.all_ready,
            "cia402_left":       CIA402_NAMES.get(self.cia402_left,  f"0x{self.cia402_left:02X}"),
            "cia402_right":      CIA402_NAMES.get(self.cia402_right, f"0x{self.cia402_right:02X}"),
            "run_enable_left":   self.run_enable_left,
            "run_enable_right":  self.run_enable_right,
        }


def parse_agv_status(payload: bytes) -> Optional[AgvStatus]:
    """Parse an AGV_STATUS payload (4 bytes)."""
    if len(payload) != _AGV_STATUS_SIZE:
        return None
    all_rdy, cia_l, cia_r, flags = struct.unpack(_AGV_STATUS_FMT, payload)
    return AgvStatus(
        all_ready         = bool(all_rdy),
        cia402_left       = cia_l,
        cia402_right      = cia_r,
        run_enable_left   = bool(flags & 0x01),
        run_enable_right  = bool(flags & 0x02),
    )
