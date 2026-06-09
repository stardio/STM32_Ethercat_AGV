/**
 * @file    robot_config.h
 * @brief   6-axis articulated robot — machine configuration constants
 *
 * Adjust these values to match the physical machine before build.
 * All angular units are degrees (°) unless stated otherwise.
 *
 * Joint layout:
 *   J1 — base rotation      (vertical axis)
 *   J2 — shoulder           (lift)
 *   J3 — elbow              (bend)
 *   J4 — wrist roll         (forearm rotation)
 *   J5 — wrist pitch        (bend)
 *   J6 — wrist yaw / tool   (tool rotation)
 *
 * unit_scale example (encoder 20-bit = 1048576 ppr, gear ratio 100:1, degree unit):
 *   unit_scale = 1048576 * 100 / 360 = 291271 counts/degree
 */
#ifndef ROBOT_CONFIG_H
#define ROBOT_CONFIG_H

#include "axis_types.h"

/* ── EtherCAT slave assignment ───────────────────────────────────────────── */
/* SOEM slave list is 1-based: slave[1] = first detected slave on the bus.   */
#define ROBOT_SLAVE_IDX(ax)     ((uint16_t)((ax) + 1U))

/* ── Encoder / mechanical defaults ──────────────────────────────────────── */
/*    unit_scale = (enc_res × gear_ratio) / 360                              */
/*    Placeholder: 4000 cpr × 100:1 gear / 360° = 1111 counts/degree        */
/*    Replace with your actual encoder PPR and gear ratio.                   */
#define ROBOT_DEFAULT_UNIT_SCALE_J1   1111     /* counts/degree */
#define ROBOT_DEFAULT_UNIT_SCALE_J2   1111
#define ROBOT_DEFAULT_UNIT_SCALE_J3   1111
#define ROBOT_DEFAULT_UNIT_SCALE_J4   1111
#define ROBOT_DEFAULT_UNIT_SCALE_J5   1111
#define ROBOT_DEFAULT_UNIT_SCALE_J6   1111

/* ── Joint travel limits (degrees, user units) ───────────────────────────── */
/* Set to 0 to disable until calibration.                                     */
#define ROBOT_DEFAULT_LIMIT_PLUS_J1   0
#define ROBOT_DEFAULT_LIMIT_MINUS_J1  0
#define ROBOT_DEFAULT_LIMIT_PLUS_J2   0
#define ROBOT_DEFAULT_LIMIT_MINUS_J2  0
#define ROBOT_DEFAULT_LIMIT_PLUS_J3   0
#define ROBOT_DEFAULT_LIMIT_MINUS_J3  0
#define ROBOT_DEFAULT_LIMIT_PLUS_J4   0
#define ROBOT_DEFAULT_LIMIT_MINUS_J4  0
#define ROBOT_DEFAULT_LIMIT_PLUS_J5   0
#define ROBOT_DEFAULT_LIMIT_MINUS_J5  0
#define ROBOT_DEFAULT_LIMIT_PLUS_J6   0
#define ROBOT_DEFAULT_LIMIT_MINUS_J6  0

/* ── Default motion parameters ──────────────────────────────────────────── */
#define ROBOT_DEFAULT_VEL_J1_UU_S    30    /* degree/s */
#define ROBOT_DEFAULT_VEL_J2_UU_S    30
#define ROBOT_DEFAULT_VEL_J3_UU_S    30
#define ROBOT_DEFAULT_VEL_J4_UU_S    60
#define ROBOT_DEFAULT_VEL_J5_UU_S    60
#define ROBOT_DEFAULT_VEL_J6_UU_S    60

#define ROBOT_DEFAULT_ACCEL_MS_J1    300   /* ms to full speed */
#define ROBOT_DEFAULT_ACCEL_MS_J2    300
#define ROBOT_DEFAULT_ACCEL_MS_J3    300
#define ROBOT_DEFAULT_ACCEL_MS_J4    200
#define ROBOT_DEFAULT_ACCEL_MS_J5    200
#define ROBOT_DEFAULT_ACCEL_MS_J6    200

#define ROBOT_DEFAULT_DECEL_MS_J1    300
#define ROBOT_DEFAULT_DECEL_MS_J2    300
#define ROBOT_DEFAULT_DECEL_MS_J3    300
#define ROBOT_DEFAULT_DECEL_MS_J4    200
#define ROBOT_DEFAULT_DECEL_MS_J5    200
#define ROBOT_DEFAULT_DECEL_MS_J6    200

#define ROBOT_DEFAULT_TORQUE_LIMIT_J1  800  /* per-mille (80.0%) */
#define ROBOT_DEFAULT_TORQUE_LIMIT_J2  800
#define ROBOT_DEFAULT_TORQUE_LIMIT_J3  800
#define ROBOT_DEFAULT_TORQUE_LIMIT_J4  700
#define ROBOT_DEFAULT_TORQUE_LIMIT_J5  700
#define ROBOT_DEFAULT_TORQUE_LIMIT_J6  700

/* ── Jog step increments (degrees) ──────────────────────────────────────── */
#define ROBOT_JOG_STEP_FINE    0.01f   /* fine   */
#define ROBOT_JOG_STEP_MEDIUM  0.10f   /* medium */
#define ROBOT_JOG_STEP_COARSE  1.00f   /* coarse */
#define ROBOT_JOG_STEP_LARGE  10.00f   /* large  */

/* ── Position tolerance (HW counts) for "target reached" detection ────────  */
#define ROBOT_TARGET_TOLERANCE_HW   5

/* ── UART protocol ───────────────────────────────────────────────────────── */
#define ROBOT_UART_BAUD          921600U
#define ROBOT_STATUS_PERIOD_MS       10U  /* status broadcast interval */
#define ROBOT_UART_LOG_BUF_SIZE    8192U  /* TX ring buffer (bytes)    */

/* ── CiA 402 timing ──────────────────────────────────────────────────────── */
#define ROBOT_CIA402_HOLD_CYCLES     5U   /* ~500 ms at 100 ms poll rate */
#define ROBOT_CIA402_TIMEOUT_CYCLES 50U   /* ~5 s before retry           */

/* ── SDO stability gate ──────────────────────────────────────────────────── */
/* Minimum consecutive 1ms cycles with |vel|≈0 and |pos_err|≤tolerance      */
/* before SDO parameter writes are allowed.                                  */
#define ROBOT_SDO_STABLE_CYCLES_MIN  20U

/* ── Flash storage ───────────────────────────────────────────────────────── */
#define ROBOT_FLASH_MAGIC     0xC4753A60UL  /* v4, 6-axis articulated robot */
#define ROBOT_FLASH_VERSION   0x0004U

/* ── Axis name strings (for log messages and UI) ─────────────────────────── */
static inline const char *robot_axis_name(uint8_t ax)
{
    static const char *names[AXIS_COUNT] = {
        "J1", "J2", "J3", "J4", "J5", "J6"
    };
    return (ax < AXIS_COUNT) ? names[ax] : "?";
}

#endif /* ROBOT_CONFIG_H */
