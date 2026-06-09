/**
 * @file    uart_protocol.h
 * @brief   6-axis articulated robot — binary UART protocol over SLIP framing
 *
 * Transport
 * ─────────
 *   USART3 at 921600 bps, 8N1.
 *   Frames are delimited by SLIP (RFC 1055):
 *     END byte  = 0xC0  (frame boundary)
 *     ESC byte  = 0xDB
 *     ESC_END   = 0xDC  (literal 0xC0 inside frame)
 *     ESC_ESC   = 0xDD  (literal 0xDB inside frame)
 *
 * Frame format (inside SLIP delimiters)
 * ──────────────────────────────────────
 *   [pkt_type: 1B] [seq: 1B] [payload: 0..PROTO_MAX_PAYLOAD B] [crc16: 2B LE]
 *
 *   CRC16-CCITT (poly 0x1021, init 0xFFFF) over [pkt_type + seq + payload].
 *   Sequence byte is echoed in ACK packets.
 *
 * Packet IDs
 * ──────────
 *   STM32 → Bridge:
 *     0x01  STATUS              6-axis position/velocity/torque/state snapshot
 *     0x02  PARAM_REPORT        axis parameter snapshot (response to PARAM_READ_REQ)
 *     0x03  ACK                 command acknowledgment
 *     0x04  LOG                 ASCII log string (UTF-8, NOT null-terminated)
 *     0x05  POINT_REPORT        current joint angles [deg] (response to TEACH_CAPTURE)
 *
 *   Bridge → STM32:
 *     0x14  HOME                set home position (wheel encoder zeroing)
 *     0x15  SET_PARAM           write one axis parameter
 *     0x16  SAVE_FLASH          persist all params to flash
 *     0x17  STOP                emergency stop
 *     0x18  RUN_ENABLE          enable / disable drives
 *     0x19  PARAM_READ_REQ      request parameter report
 *     0x22  FAULT_RESET         force CiA402 fault recovery (per-axis or all)
 *
 *   AGV:
 *     0x30  AGV_VELOCITY        differential drive command (Bridge → STM32)
 *     0x31  AGV_ODOMETRY        wheel encoder snapshot (STM32 → Bridge, 10 ms)
 *     0x32  AGV_STATUS          AGV drive health (STM32 → Bridge, 10 ms)
 *
 * Packet sizes (payload only)
 * ───────────────────────────
 *   AxisStatusPkt_t           = 12 bytes  (per axis)
 *   ProtoPktStatus_t          = 74 bytes  (6 × 12 + 2)
 *   ProtoAxisParam_t          = 44 bytes  (per axis)
 *   ProtoPktParamReport_t     = 264 bytes (6 × 44)
 *   ProtoPktFaultReset_t      =  1 byte   (axis)
 *   ProtoPktAgvVelocity_t     =  8 bytes
 *   ProtoPktAgvOdometry_t     = 20 bytes
 *   ProtoPktAgvStatus_t       =  4 bytes
 *
 * Threading
 * ─────────
 *   UartProto_SendStatus()   → DefaultTask (10 ms period via osDelay)
 *   UartProto_PollRx()       → DefaultTask (every loop cycle)
 *   UartProto_FeedRxByte()   → UART RX interrupt (HAL_UART_RxCpltCallback)
 */
#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>
#include "axis_types.h"
#include "interpolator.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Limits ──────────────────────────────────────────────────────────────── */
#define PROTO_MAX_PAYLOAD    300U   /* bytes; largest frame = PARAM_REPORT (264 B) */
#define PROTO_RX_RING_SIZE   512U   /* SLIP RX ring buffer                         */

/* ── Packet type IDs ─────────────────────────────────────────────────────── */

/* STM32 → Bridge */
#define PROTO_PKT_STATUS              0x01U
#define PROTO_PKT_PARAM_REPORT        0x02U
#define PROTO_PKT_ACK                 0x03U
#define PROTO_PKT_LOG                 0x04U
#define PROTO_PKT_POINT_REPORT        0x05U   /* current joint angles [deg]      */

/* Bridge → STM32 */
#define PROTO_PKT_HOME                0x14U
#define PROTO_PKT_SET_PARAM           0x15U
#define PROTO_PKT_SAVE_FLASH          0x16U
#define PROTO_PKT_STOP                0x17U
#define PROTO_PKT_RUN_ENABLE          0x18U
#define PROTO_PKT_PARAM_READ_REQ      0x19U
#define PROTO_PKT_FAULT_RESET         0x22U   /* force CiA402 fault recovery     */

/* AGV packets */
#define PROTO_PKT_AGV_VELOCITY        0x30U   /* Bridge → STM32: differential drive cmd */
#define PROTO_PKT_AGV_ODOMETRY        0x31U   /* STM32 → Bridge: wheel encoder snapshot */
#define PROTO_PKT_AGV_STATUS          0x32U   /* STM32 → Bridge: AGV drive health       */

/* AGV differential drive geometry (metres — adjust after physical measurement) */
#define AGV_WHEEL_BASE_M              0.60f   /* distance between wheel centres [m]     */
#define AGV_WHEEL_RADIUS_M            0.15f   /* wheel radius [m]                       */

/* ── Result / error codes (in ACK.result) ────────────────────────────────── */
#define PROTO_RESULT_OK          0x00U
#define PROTO_RESULT_BUSY        0x01U   /* interpolator busy                */
#define PROTO_RESULT_BAD_PARAM   0x02U   /* invalid parameter ID or value    */
#define PROTO_RESULT_NOT_READY   0x03U   /* drives not in OP_ENABLED         */
#define PROTO_RESULT_FLASH_ERR   0x04U   /* flash write failed               */
#define PROTO_RESULT_BAD_PKT     0xFFU   /* malformed packet / CRC error     */

/* ── Parameter IDs (used in SET_PARAM and PARAM_REPORT) ─────────────────── */
#define PROTO_PARAM_UNIT_SCALE       0x01U
#define PROTO_PARAM_HOME_OFFSET      0x02U
#define PROTO_PARAM_LIMIT_PLUS_HW    0x03U
#define PROTO_PARAM_LIMIT_MINUS_HW   0x04U
#define PROTO_PARAM_LIMIT_PLUS_USER  0x05U
#define PROTO_PARAM_LIMIT_MINUS_USER 0x06U
#define PROTO_PARAM_PROFILE_VEL      0x07U
#define PROTO_PARAM_PROFILE_ACCEL_MS 0x08U
#define PROTO_PARAM_PROFILE_DECEL_MS 0x09U
#define PROTO_PARAM_TORQUE_LIMIT     0x0AU
#define PROTO_PARAM_POSITION_GAIN    0x0BU
#define PROTO_PARAM_LIMITS_ENABLED   0x0CU
#define PROTO_PARAM_RAMP_VEL         0x20U   /* non-persistent jog ramp velocity; 0 = use profile_vel */

/* ── Home command types ──────────────────────────────────────────────────── */
#define PROTO_HOME_SET_CURRENT   0x00U   /* declare current position as home */
#define PROTO_HOME_RUN_SEQUENCE  0x01U   /* run hardware home sequence        */

/* ── Packed payload structs ─────────────────────────────────────────────── */

/* STATUS (0x01) — STM32 → Bridge, 10 ms period */
typedef struct __attribute__((packed)) {
    AxisStatusPkt_t axis[AXIS_COUNT];   /* 12 × 6 = 72 bytes */
    uint8_t         interp_state;       /* InterpState_t      */
    uint8_t         sys_flags;          /* bit0=all_ready, bit1=all_tgt_reached,
                                           bits[6:4]=active_axes (0-6)       */
} ProtoPktStatus_t;                     /* 74 bytes */

/* Per-axis wire-format parameter (no natural padding) — used in PARAM_REPORT */
typedef struct __attribute__((packed)) {
    int32_t  unit_scale;
    int32_t  home_offset;
    int32_t  limit_plus_hw;
    int32_t  limit_minus_hw;
    int32_t  limit_plus_user;
    int32_t  limit_minus_user;
    int32_t  profile_velocity;
    int32_t  profile_accel_ms;
    int32_t  profile_decel_ms;
    int16_t  torque_limit;
    int32_t  position_gain;
    uint8_t  limits_enabled;
    uint8_t  limits_blocked;
} ProtoAxisParam_t;                     /* 44 bytes */

/* PARAM_REPORT (0x02) — STM32 → Bridge */
typedef struct __attribute__((packed)) {
    ProtoAxisParam_t axis[AXIS_COUNT];  /* 44 × 6 = 264 bytes */
} ProtoPktParamReport_t;               /* 264 bytes */

/* ACK (0x03) — STM32 → Bridge */
typedef struct __attribute__((packed)) {
    uint8_t seq;       /* echoed command sequence number */
    uint8_t result;    /* PROTO_RESULT_* code            */
} ProtoPktAck_t;                        /* 2 bytes */

/* HOME (0x14) — Bridge → STM32 */
typedef struct __attribute__((packed)) {
    uint8_t axis;   /* AxisId_t, or 0xFF = all axes */
    uint8_t type;   /* PROTO_HOME_* constant        */
} ProtoPktHome_t;                       /* 2 bytes */

/* SET_PARAM (0x15) — Bridge → STM32 */
typedef struct __attribute__((packed)) {
    uint8_t axis;       /* AxisId_t (0..5)         */
    uint8_t param_id;   /* PROTO_PARAM_* constant  */
    uint8_t _pad[2];    /* alignment               */
    int32_t value;      /* new value               */
} ProtoPktSetParam_t;                   /* 8 bytes */

/* RUN_ENABLE (0x18) — Bridge → STM32 */
typedef struct __attribute__((packed)) {
    uint8_t axis;      /* AxisId_t, or 0xFF = all */
    uint8_t enable;    /* 0=disable, 1=enable      */
} ProtoPktRunEnable_t;                  /* 2 bytes */

/* FAULT_RESET (0x22) — Bridge → STM32 */
typedef struct __attribute__((packed)) {
    uint8_t axis;   /* AxisId_t, or 0xFF = all axes */
} ProtoPktFaultReset_t;                /* 1 byte */

/* AGV_VELOCITY (0x30) — Bridge → STM32 */
typedef struct __attribute__((packed)) {
    float linear_mps;    /* linear velocity  [m/s]   — positive = forward */
    float angular_rps;   /* angular velocity [rad/s] — positive = CCW     */
} ProtoPktAgvVelocity_t;               /* 8 bytes */

/* AGV_ODOMETRY (0x31) — STM32 → Bridge, 10 ms period */
typedef struct __attribute__((packed)) {
    int32_t  pos_left_hw;    /* AXIS_J1 (left wheel) encoder counts          */
    int32_t  pos_right_hw;   /* AXIS_J2 (right wheel) encoder counts         */
    int32_t  vel_left_hw;    /* AXIS_J1 velocity [HW counts/s]               */
    int32_t  vel_right_hw;   /* AXIS_J2 velocity [HW counts/s]               */
    uint32_t timestamp_ms;   /* HAL_GetTick() at capture time                */
} ProtoPktAgvOdometry_t;               /* 20 bytes */

/* AGV_STATUS (0x32) — STM32 → Bridge, 10 ms period */
typedef struct __attribute__((packed)) {
    uint8_t all_ready;     /* 1 if both wheel drives are PDO-ready          */
    uint8_t cia402_left;   /* Cia402State_t of AXIS_J1                      */
    uint8_t cia402_right;  /* Cia402State_t of AXIS_J2                      */
    uint8_t flags;         /* bit0=run_enable_left, bit1=run_enable_right   */
} ProtoPktAgvStatus_t;                 /* 4 bytes */

/* ── TX callback type ────────────────────────────────────────────────────── */
typedef void (*ProtoTxFn_t)(const uint8_t *data, uint16_t len);

/* ── Public API ──────────────────────────────────────────────────────────── */

/**
 * @brief  Initialise the protocol module.
 * @param  tx_fn  Callback that writes bytes to the physical UART TX path.
 */
void UartProto_Init(ProtoTxFn_t tx_fn);

/**
 * @brief  Feed one received byte into the SLIP decoder.
 *         Call from HAL_UART_RxCpltCallback() — interrupt context.
 */
void UartProto_FeedRxByte(uint8_t b);

/**
 * @brief  Drain the SLIP RX ring and dispatch any complete packets.
 *         Call from DefaultTask, once per loop iteration.
 */
void UartProto_PollRx(void);

/**
 * @brief  Transmit a STATUS packet with the current 6-axis shadow data.
 *         Call from DefaultTask every 10 ms.
 */
void UartProto_SendStatus(void);

/**
 * @brief  Transmit a PARAM_REPORT packet with the current g_axis_param[].
 */
void UartProto_SendParamReport(void);

/**
 * @brief  Transmit a LOG packet wrapping an ASCII string.
 * @param  msg  Null-terminated string (max PROTO_MAX_PAYLOAD - 1 bytes).
 */
void UartProto_SendLog(const char *msg);

/**
 * @brief  Transmit an AGV_ODOMETRY packet with current wheel encoder data.
 *         Call from DefaultTask every 10 ms alongside SendStatus.
 */
void UartProto_SendAgvOdometry(void);

/**
 * @brief  Transmit an AGV_STATUS packet with AGV drive health.
 *         Call from DefaultTask every 10 ms alongside SendStatus.
 */
void UartProto_SendAgvStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* UART_PROTOCOL_H */
