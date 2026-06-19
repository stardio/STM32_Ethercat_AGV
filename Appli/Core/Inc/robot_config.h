/**
 * @file    robot_config.h
 * @brief   AGV differential drive — machine configuration constants
 *
 * Adjust these values to match physical hardware before building.
 *
 * Axis mapping:
 *   AXIS_J1 (slave 1) — left wheel
 *   AXIS_J2 (slave 2) — right wheel
 *
 * unit_scale example (encoder 4000 cpr, 1:1 gear, mm unit):
 *   wheel_circumference = 2π × 150 mm = 942.5 mm
 *   unit_scale = 4000 / 942.5 ≈ 4 counts/mm
 */
#ifndef ROBOT_CONFIG_H
#define ROBOT_CONFIG_H

#include "axis_types.h"

/* ── EtherCAT slave assignment ───────────────────────────────────────────── */
#define ROBOT_SLAVE_IDX(ax)     ((uint16_t)((ax) + 1U))

/* ── AGV geometry ────────────────────────────────────────────────────────── */
/* These must match AGV_WHEEL_BASE_M / AGV_WHEEL_RADIUS_M in uart_protocol.h */
#define AGV_WHEEL_BASE_MM       600.0f  /* distance between wheel centres [mm] */
#define AGV_WHEEL_RADIUS_MM     150.0f  /* wheel radius [mm]                   */

/* ── Encoder / mechanical defaults ──────────────────────────────────────── */
/* unit_scale = encoder_cpr / wheel_circumference_mm                          */
/* Placeholder: 4000 cpr / (2π×150 mm) ≈ 4 counts/mm — replace with actual  */
#define ROBOT_DEFAULT_UNIT_SCALE_J1   4   /* counts/mm  left wheel  */
#define ROBOT_DEFAULT_UNIT_SCALE_J2   4   /* counts/mm  right wheel */

/* ── Default velocity / accel limits ────────────────────────────────────── */
/* Expressed in mm/s and ms respectively; applied to CSV TargetVelocity.     */
#define ROBOT_DEFAULT_VEL_J1_UU_S    500   /* mm/s */
#define ROBOT_DEFAULT_VEL_J2_UU_S    500

#define ROBOT_DEFAULT_ACCEL_MS_J1    300   /* ms to full speed */
#define ROBOT_DEFAULT_ACCEL_MS_J2    300

#define ROBOT_DEFAULT_DECEL_MS_J1    300
#define ROBOT_DEFAULT_DECEL_MS_J2    300

/* ── Torque limits ───────────────────────────────────────────────────────── */
#define ROBOT_DEFAULT_TORQUE_LIMIT_J1  800  /* per-mille (80.0%) */
#define ROBOT_DEFAULT_TORQUE_LIMIT_J2  800

/* ── Flash storage ───────────────────────────────────────────────────────── */
#define ROBOT_FLASH_MAGIC    0xC4754100UL  /* AGV v1 magic (different from 6-axis 0xC4753A60) */
#define ROBOT_FLASH_VERSION  1U

/* ── EtherCAT / CiA402 timing ────────────────────────────────────────────── */
#define ROBOT_TARGET_TOLERANCE_HW    50     /* HW counts: position error ≤ this = "reached" */
#define ROBOT_SDO_STABLE_CYCLES_MIN  20     /* 1ms ticks stable before SDO write allowed    */
#define ROBOT_CIA402_TIMEOUT_CYCLES  3000   /* 3 s CiA402 state transition timeout          */
#define ROBOT_CIA402_HOLD_CYCLES     5      /* hold cycles between controlword transitions  */

/* ── Axis name helper ────────────────────────────────────────────────────── */
static inline const char *robot_axis_name(uint8_t ax)
{
    static const char *names[] = { "L", "R" };
    return (ax < 2U) ? names[ax] : "?";
}

#endif /* ROBOT_CONFIG_H */
