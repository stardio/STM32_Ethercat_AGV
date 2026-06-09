/**
 * @file    axis_config.c
 * @brief   Global axis state arrays — definition site.
 *
 * g_shadow[]     : volatile read-only for web protocol / DefaultTask
 * g_axis_param[] : read/write by DefaultTask, read by EtherCAT_Task
 *
 * Default parameter values come from robot_config.h.
 * Flash storage overwrites them at boot via SOEM_LoadAxisParam().
 */
#include "axis_config.h"
#include "axis_types.h"
#include "robot_config.h"
#include <string.h>

/* ── Volatile shadow: written by EtherCAT_Task (1ms), read everywhere ───── */
AxisShadow_t g_shadow[AXIS_COUNT];

/* ── Axis parameters: loaded from flash at boot, applied to drive via SDO ── */
AxisParam_t  g_axis_param[AXIS_COUNT];

/* ── Default parameter tables (indexed by AXIS_J1..AXIS_J6) ─────────────── */
static const int32_t k_default_unit_scale[AXIS_COUNT] = {
    ROBOT_DEFAULT_UNIT_SCALE_J1,
    ROBOT_DEFAULT_UNIT_SCALE_J2,
    ROBOT_DEFAULT_UNIT_SCALE_J3,
    ROBOT_DEFAULT_UNIT_SCALE_J4,
    ROBOT_DEFAULT_UNIT_SCALE_J5,
    ROBOT_DEFAULT_UNIT_SCALE_J6,
};
static const int32_t k_default_vel[AXIS_COUNT] = {
    ROBOT_DEFAULT_VEL_J1_UU_S,
    ROBOT_DEFAULT_VEL_J2_UU_S,
    ROBOT_DEFAULT_VEL_J3_UU_S,
    ROBOT_DEFAULT_VEL_J4_UU_S,
    ROBOT_DEFAULT_VEL_J5_UU_S,
    ROBOT_DEFAULT_VEL_J6_UU_S,
};
static const int32_t k_default_accel[AXIS_COUNT] = {
    ROBOT_DEFAULT_ACCEL_MS_J1,
    ROBOT_DEFAULT_ACCEL_MS_J2,
    ROBOT_DEFAULT_ACCEL_MS_J3,
    ROBOT_DEFAULT_ACCEL_MS_J4,
    ROBOT_DEFAULT_ACCEL_MS_J5,
    ROBOT_DEFAULT_ACCEL_MS_J6,
};
static const int32_t k_default_decel[AXIS_COUNT] = {
    ROBOT_DEFAULT_DECEL_MS_J1,
    ROBOT_DEFAULT_DECEL_MS_J2,
    ROBOT_DEFAULT_DECEL_MS_J3,
    ROBOT_DEFAULT_DECEL_MS_J4,
    ROBOT_DEFAULT_DECEL_MS_J5,
    ROBOT_DEFAULT_DECEL_MS_J6,
};
static const uint16_t k_default_torque[AXIS_COUNT] = {
    ROBOT_DEFAULT_TORQUE_LIMIT_J1,
    ROBOT_DEFAULT_TORQUE_LIMIT_J2,
    ROBOT_DEFAULT_TORQUE_LIMIT_J3,
    ROBOT_DEFAULT_TORQUE_LIMIT_J4,
    ROBOT_DEFAULT_TORQUE_LIMIT_J5,
    ROBOT_DEFAULT_TORQUE_LIMIT_J6,
};

/**
 * @brief  Initialise both arrays to safe defaults.
 *         Call once at boot before flash load.
 */
void AxisConfig_InitDefaults(void)
{
    memset(g_shadow,     0, sizeof(g_shadow));
    memset(g_axis_param, 0, sizeof(g_axis_param));

    for (uint8_t ax = 0; ax < AXIS_COUNT; ax++)
    {
        g_axis_param[ax].unit_scale       = k_default_unit_scale[ax];
        g_axis_param[ax].home_offset      = 0;
        g_axis_param[ax].limit_plus_hw    = INT32_MAX;
        g_axis_param[ax].limit_minus_hw   = INT32_MIN;
        g_axis_param[ax].limit_plus_user  = 0;
        g_axis_param[ax].limit_minus_user = 0;
        g_axis_param[ax].limits_enabled   = 0U;
        g_axis_param[ax].limits_blocked   = 0U;
        g_axis_param[ax].profile_velocity = k_default_vel[ax];
        g_axis_param[ax].profile_accel_ms = k_default_accel[ax];
        g_axis_param[ax].profile_decel_ms = k_default_decel[ax];
        g_axis_param[ax].torque_limit     = k_default_torque[ax];
        g_axis_param[ax].position_gain    = 0;  /* 0 = don't write to drive */
    }
}
