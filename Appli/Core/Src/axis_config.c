/**
 * @file    axis_config.c
 * @brief   AGV wheel axis state arrays — definition site.
 *
 * g_shadow[]     : volatile snapshot written by EtherCAT_Task (1ms)
 * g_axis_param[] : configuration written by DefaultTask, read by EtherCAT_Task
 */
#include "axis_config.h"
#include "axis_types.h"
#include "robot_config.h"
#include <string.h>

AxisShadow_t g_shadow[AXIS_COUNT];
AxisParam_t  g_axis_param[AXIS_COUNT];

static const int32_t  k_default_unit_scale[AXIS_COUNT] = {
    ROBOT_DEFAULT_UNIT_SCALE_J1,
    ROBOT_DEFAULT_UNIT_SCALE_J2,
};
static const int32_t  k_default_vel[AXIS_COUNT] = {
    ROBOT_DEFAULT_VEL_J1_UU_S,
    ROBOT_DEFAULT_VEL_J2_UU_S,
};
static const int32_t  k_default_accel[AXIS_COUNT] = {
    ROBOT_DEFAULT_ACCEL_MS_J1,
    ROBOT_DEFAULT_ACCEL_MS_J2,
};
static const int32_t  k_default_decel[AXIS_COUNT] = {
    ROBOT_DEFAULT_DECEL_MS_J1,
    ROBOT_DEFAULT_DECEL_MS_J2,
};
static const uint16_t k_default_torque[AXIS_COUNT] = {
    ROBOT_DEFAULT_TORQUE_LIMIT_J1,
    ROBOT_DEFAULT_TORQUE_LIMIT_J2,
};

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
        g_axis_param[ax].position_gain    = 0;
    }
}
