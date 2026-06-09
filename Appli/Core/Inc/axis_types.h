/**
 * @file    axis_types.h
 * @brief   6-axis articulated robot — shared type definitions
 *
 * All EtherCAT, motion, and UI layers reference these types.
 * Axis index convention:
 *   AXIS_J1 = 0  → EtherCAT slave 1  (base rotation)
 *   AXIS_J2 = 1  → EtherCAT slave 2  (shoulder)
 *   AXIS_J3 = 2  → EtherCAT slave 3  (elbow)
 *   AXIS_J4 = 3  → EtherCAT slave 4  (wrist roll)
 *   AXIS_J5 = 4  → EtherCAT slave 5  (wrist pitch)
 *   AXIS_J6 = 5  → EtherCAT slave 6  (wrist yaw)
 *
 * User unit: degree (°).
 * pos_centi field carries degree × 100 (centi-degree) for UART transport.
 */
#ifndef AXIS_TYPES_H
#define AXIS_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Axis count ──────────────────────────────────────────────────────────── */
#define AXIS_COUNT  6U

typedef enum {
    AXIS_J1  = 0,
    AXIS_J2  = 1,
    AXIS_J3  = 2,
    AXIS_J4  = 3,
    AXIS_J5  = 4,
    AXIS_J6  = 5,
    AXIS_ALL = 0xFFU,   /* broadcast / "all axes" sentinel */
} AxisId_t;

/* ── CiA 402 drive state machine stages ─────────────────────────────────── */
typedef enum {
    CIA402_STAGE_SEND_SHUTDOWN = 0,
    CIA402_STAGE_WAIT_READY,
    CIA402_STAGE_SEND_SWITCH_ON,
    CIA402_STAGE_WAIT_SWITCHED_ON,
    CIA402_STAGE_SEND_ENABLE_OP,
    CIA402_STAGE_WAIT_OP_ENABLED,
    CIA402_STAGE_OP_ENABLED,
} Cia402Stage_t;

/* Decoded CiA 402 state (from statusword masked 0x006F) */
typedef enum {
    CIA402_STATE_NOT_READY    = 0,
    CIA402_STATE_SW_DISABLED  = 1,
    CIA402_STATE_READY        = 2,
    CIA402_STATE_SWITCHED_ON  = 3,
    CIA402_STATE_OP_ENABLED   = 4,
    CIA402_STATE_QUICK_STOP   = 5,
    CIA402_STATE_FAULT_REACT  = 6,
    CIA402_STATE_FAULT        = 7,
    CIA402_STATE_UNKNOWN      = 255,
} Cia402State_t;

/* ── Per-axis volatile shadow (read from EtherCAT_Task, read by web protocol) */
typedef struct {
    volatile int32_t  pos_hw;         /* raw encoder counts                       */
    volatile int32_t  pos_centi;      /* degree × 100 (centi-degree, for display) */
    volatile int32_t  velocity;       /* user-units/s (degree/s)                  */
    volatile int16_t  torque;         /* per-mille (1000 = 100.0%)                */
    volatile uint16_t statusword;
    volatile uint8_t  cia402_state;   /* Cia402State_t                            */
    volatile uint8_t  pdo_ready;
    volatile uint8_t  target_reached; /* 1 when within tolerance                  */
    volatile uint8_t  run_enable;
} AxisShadow_t;

/* ── Per-axis parameter set (loaded from flash at boot, applied via SDO) ── */
typedef struct {
    int32_t  unit_scale;        /* HW counts per 1 user-unit (degree) */
    int32_t  home_offset;       /* HW count corresponding to user 0°  */
    int32_t  limit_plus_hw;     /* positive travel limit (HW counts)  */
    int32_t  limit_minus_hw;    /* negative travel limit (HW counts)  */
    int32_t  limit_plus_user;   /* display cache (user units, degree)  */
    int32_t  limit_minus_user;  /* display cache                       */
    int32_t  profile_velocity;  /* user-units/s (degree/s)             */
    int32_t  profile_accel_ms;  /* ms to full speed                    */
    int32_t  profile_decel_ms;  /* ms to stop                          */
    uint16_t torque_limit;      /* per-mille ceiling                   */
    int32_t  position_gain;     /* drive Kp (0 = don't write)          */
    uint8_t  limits_enabled;
    uint8_t  limits_blocked;
} AxisParam_t;

/* ── Compact status snapshot sent over UART to web bridge (10 ms period) ── */
typedef struct __attribute__((packed)) {
    int32_t  pos_centi;      /* degree × 100 */
    int16_t  velocity;       /* degree/s, clamped to int16 range */
    int16_t  torque;         /* per-mille */
    uint16_t statusword;
    uint8_t  cia402_state;
    uint8_t  flags;          /* bit0=pdo_ready, bit1=target_reached, bit2=run_enable */
} AxisStatusPkt_t;           /* 12 bytes per axis */

/* ── Global arrays (defined in axis_config.c) ───────────────────────────── */
extern AxisShadow_t g_shadow[AXIS_COUNT];
extern AxisParam_t  g_axis_param[AXIS_COUNT];

/* ── Helper: axis index validation ──────────────────────────────────────── */
static inline uint8_t axis_valid(uint8_t ax)
{
    return (ax < AXIS_COUNT) ? 1U : 0U;
}

/* ── Unit conversion helpers (inline for speed in 1ms loop) ─────────────── */
static inline int32_t axis_hw_to_user(uint8_t ax, int32_t hw)
{
    int32_t scale = (g_axis_param[ax].unit_scale > 0)
                    ? g_axis_param[ax].unit_scale : 1;
    int64_t user = ((int64_t)hw - (int64_t)g_axis_param[ax].home_offset)
                   / (int64_t)scale;
    if (user >  (int64_t)INT32_MAX) return  INT32_MAX;
    if (user < -(int64_t)INT32_MAX) return -INT32_MAX;
    return (int32_t)user;
}

static inline int32_t axis_user_to_hw(uint8_t ax, int32_t user)
{
    int32_t scale = (g_axis_param[ax].unit_scale > 0)
                    ? g_axis_param[ax].unit_scale : 1;
    int64_t hw = (int64_t)user * (int64_t)scale
                 + (int64_t)g_axis_param[ax].home_offset;
    if (hw >  (int64_t)INT32_MAX) return  INT32_MAX;
    if (hw < -(int64_t)INT32_MAX) return -INT32_MAX;
    return (int32_t)hw;
}

static inline int32_t axis_hw_to_centi(uint8_t ax, int32_t hw)
{
    int32_t scale = (g_axis_param[ax].unit_scale > 0)
                    ? g_axis_param[ax].unit_scale : 1;
    int64_t centi = ((int64_t)hw - (int64_t)g_axis_param[ax].home_offset)
                    * 100LL / (int64_t)scale;
    if (centi >  (int64_t)INT32_MAX) return  INT32_MAX;
    if (centi < -(int64_t)INT32_MAX) return -INT32_MAX;
    return (int32_t)centi;
}

static inline int32_t axis_clamp_hw(uint8_t ax, int32_t hw)
{
    if (!g_axis_param[ax].limits_enabled) return hw;
    if (hw > g_axis_param[ax].limit_plus_hw)  return g_axis_param[ax].limit_plus_hw;
    if (hw < g_axis_param[ax].limit_minus_hw) return g_axis_param[ax].limit_minus_hw;
    return hw;
}

#ifdef __cplusplus
}
#endif

#endif /* AXIS_TYPES_H */
