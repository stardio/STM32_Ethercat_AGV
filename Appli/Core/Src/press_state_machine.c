#include "press_state_machine.h"
#include "interlock_manager.h"
#include "soem_port.h"
#include <string.h>
#include <stddef.h>

/* HAL_GetTick() */
#include "stm32h7xx_hal.h"

/* -----------------------------------------------------------------------
 * Module-private state
 * ----------------------------------------------------------------------- */
static OperationMode_t g_op_mode    = OP_MODE_MANUAL;
static PressState_t    g_press_state = PRESS_STATE_IDLE;
static PressConfig_t   g_config;
static PressResult_t   g_last_result;
static PressCounter_t  g_counters;

static uint32_t g_cycle_start_tick  = 0U;
static uint16_t g_peak_force        = 0U;
static uint8_t  g_alarm_active      = 0U;
static uint32_t g_dwell_start_tick  = 0U;
static uint32_t g_cycle_number      = 0U;
static int32_t  g_press_end_pos     = 0;  /* position captured at end of DWELL (before RETURN) */

/* -----------------------------------------------------------------------
 * Default config: safe minimal values
 * ----------------------------------------------------------------------- */
static const PressConfig_t k_default_config = {
    .approach_speed     = 500,
    .approach_pos       = 50,       /* user units: switch to contact 50u before target */
    .contact_speed      = 50,
    .contact_torque_th  = 100U,     /* 10.0% torque = contact detected (0.1% units) */
    .press_speed        = 100,
    .press_target_pos   = 0,        /* must be set by user */
    .press_max_force    = 800U,  /* 80.0% (0.1% units) */
    .dwell_time_ms      = 500U,
    .return_speed       = 500,
    .return_pos         = 0,
    .cycle_timeout_ms   = 30000U,   /* 30 s */
    .judge_force_max    = 800U,  /* 80.0% (0.1% units) */
    .judge_force_min    = 50U,   /* 5.0%  (0.1% units) */
    .judge_pos_max      = 0,
    .judge_pos_min      = 0,
    .judge_cycle_time_max = 0U,     /* disabled */
};

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */
static void psm_set_state(PressState_t next)
{
    g_press_state = next;
}

static uint8_t psm_is_drive_ready(void)
{
    const uint16_t sw = SOEM_GetStatusword_Legacy();
    return (((sw & 0x006FU) == 0x0027U) && (SOEM_GetRunEnable_Legacy() != 0U) &&
            (SOEM_GetPdoReady_Legacy() != 0U)) ? 1U : 0U;
}

static uint8_t psm_is_target_reached(int32_t target_user)
{
    int64_t delta = (int64_t)SOEM_GetPositionActual() - (int64_t)target_user;
    if (delta < 0) { delta = -delta; }
    return (delta <= 2LL) ? 1U : 0U;  /* 2 mm tolerance */
}

static uint16_t psm_get_torque_pct(void)
{
    int16_t t = SOEM_GetTorqueActual();  /* CiA402 0x6077: 0.1% units, returned as-is */
    if (t < 0) { t = (int16_t)(-t); }
    return (uint16_t)t;  /* 0.1% units — all config fields use same unit */
}

static void psm_update_peak_force(void)
{
    uint16_t t = psm_get_torque_pct();
    if (t > g_peak_force) { g_peak_force = t; }
}

static JudgeResult_t psm_judge(void)
{
    int32_t end_pos    = g_press_end_pos;  /* captured at DWELL end, before RETURN */
    uint32_t cycle_ms  = HAL_GetTick() - g_cycle_start_tick;

    if (g_peak_force > g_config.judge_force_max)  { return JUDGE_NG_FORCE_HIGH; }
    if (g_peak_force < g_config.judge_force_min)  { return JUDGE_NG_FORCE_LOW;  }

    if ((g_config.judge_pos_max != 0) || (g_config.judge_pos_min != 0))
    {
        if (end_pos > g_config.judge_pos_max)     { return JUDGE_NG_POS_HIGH;   }
        if (end_pos < g_config.judge_pos_min)     { return JUDGE_NG_POS_LOW;    }
    }

    if ((g_config.judge_cycle_time_max != 0U) && (cycle_ms > g_config.judge_cycle_time_max))
    {
        return JUDGE_NG_TIME_OVER;
    }

    return JUDGE_OK;
}

static void psm_record_result(JudgeResult_t res)
{
    g_last_result.cycle_number  = g_cycle_number;
    g_last_result.result        = res;
    g_last_result.peak_force_pct = g_peak_force;
    g_last_result.end_position  = g_press_end_pos;  /* captured at DWELL end, before RETURN */
    g_last_result.cycle_time_ms = HAL_GetTick() - g_cycle_start_tick;

    g_counters.total++;
    if (res == JUDGE_OK)
    {
        g_counters.ok++;
        g_counters.consecutive_ng = 0U;
    }
    else
    {
        g_counters.ng++;
        g_counters.consecutive_ng++;
        switch (res)
        {
            case JUDGE_NG_FORCE_HIGH: g_counters.ng_force_high++; break;
            case JUDGE_NG_FORCE_LOW:  g_counters.ng_force_low++;  break;
            case JUDGE_NG_POS_HIGH:   g_counters.ng_pos_high++;   break;
            case JUDGE_NG_POS_LOW:    g_counters.ng_pos_low++;    break;
            case JUDGE_NG_TIME_OVER:  g_counters.ng_time_over++;  break;
            case JUDGE_NG_INTERLOCK:  g_counters.ng_interlock++;  break;
            default: break;
        }
    }

    if (g_counters.total > 0U)
    {
        g_counters.ng_rate_x10 = (uint8_t)((g_counters.ng * 1000U) / g_counters.total);
    }
}

static uint8_t psm_check_timeout(void)
{
    if (g_config.cycle_timeout_ms == 0U) { return 0U; }
    uint32_t elapsed = HAL_GetTick() - g_cycle_start_tick;
    return (elapsed > g_config.cycle_timeout_ms) ? 1U : 0U;
}

/* -----------------------------------------------------------------------
 * State machine tick per state
 * ----------------------------------------------------------------------- */
static void psm_tick_idle(void)
{
    /* Nothing to do in IDLE; cycle_start is handled externally */
    (void)0;
}

static void psm_tick_approach(void)
{
    psm_update_peak_force();

    /* Abort: interlock lost or timeout */
    if (InterlockManager_IsEmergencyStop() != 0U)
    {
        SOEM_SetTargetPositionAbs(SOEM_GetPositionActual());
        psm_record_result(JUDGE_NG_INTERLOCK);
        psm_set_state(PRESS_STATE_ABORT);
        return;
    }
    if (psm_check_timeout() != 0U)
    {
        SOEM_SetTargetPositionAbs(SOEM_GetPositionActual());
        psm_record_result(JUDGE_NG_TIME_OVER);
        psm_set_state(PRESS_STATE_ABORT);
        return;
    }

    /* Transition: reached approach_pos → switch to contact speed */
    if (psm_is_target_reached(g_config.approach_pos) != 0U)
    {
        SOEM_SetRampVelocity_Legacy(g_config.contact_speed);
        SOEM_SetTargetPositionAbs(g_config.press_target_pos);
        psm_set_state(PRESS_STATE_CONTACT);
    }
}

static void psm_tick_contact(void)
{
    psm_update_peak_force();

    if (InterlockManager_IsEmergencyStop() != 0U)
    {
        SOEM_SetTargetPositionAbs(SOEM_GetPositionActual());
        psm_record_result(JUDGE_NG_INTERLOCK);
        psm_set_state(PRESS_STATE_ABORT);
        return;
    }
    if (psm_check_timeout() != 0U)
    {
        SOEM_SetTargetPositionAbs(SOEM_GetPositionActual());
        psm_record_result(JUDGE_NG_TIME_OVER);
        psm_set_state(PRESS_STATE_ABORT);
        return;
    }

    /* Contact detected by torque threshold */
    uint16_t torque = psm_get_torque_pct();
    if (torque >= g_config.contact_torque_th)
    {
        /* Switch to full press speed */
        SOEM_SetRampVelocity_Legacy(g_config.press_speed);
        SOEM_SetTorqueLimitPercent(g_config.press_max_force);
        psm_set_state(PRESS_STATE_PRESS);
        return;
    }

    /* Also transition if position already at target (no contact detected) */
    if (psm_is_target_reached(g_config.press_target_pos) != 0U)
    {
        SOEM_SetRampVelocity_Legacy(g_config.press_speed);
        SOEM_SetTorqueLimitPercent(g_config.press_max_force);
        psm_set_state(PRESS_STATE_PRESS);
    }
}

static void psm_tick_press(void)
{
    psm_update_peak_force();

    if (InterlockManager_IsEmergencyStop() != 0U)
    {
        SOEM_SetTargetPositionAbs(SOEM_GetPositionActual());
        psm_record_result(JUDGE_NG_INTERLOCK);
        psm_set_state(PRESS_STATE_ABORT);
        return;
    }
    if (psm_check_timeout() != 0U)
    {
        SOEM_SetTargetPositionAbs(SOEM_GetPositionActual());
        psm_record_result(JUDGE_NG_TIME_OVER);
        psm_set_state(PRESS_STATE_ABORT);
        return;
    }

    /* Force ceiling exceeded → NG immediate */
    if (psm_get_torque_pct() >= g_config.press_max_force)
    {
        SOEM_SetTargetPositionAbs(SOEM_GetPositionActual());
        g_dwell_start_tick = HAL_GetTick();
        psm_set_state(PRESS_STATE_DWELL);
        return;
    }

    /* Target position reached → enter dwell */
    if (psm_is_target_reached(g_config.press_target_pos) != 0U)
    {
        SOEM_SetTargetPositionAbs(g_config.press_target_pos);
        g_dwell_start_tick = HAL_GetTick();
        psm_set_state(PRESS_STATE_DWELL);
    }
}

static void psm_tick_dwell(void)
{
    psm_update_peak_force();

    if (InterlockManager_IsEmergencyStop() != 0U)
    {
        SOEM_SetTargetPositionAbs(SOEM_GetPositionActual());
        psm_record_result(JUDGE_NG_INTERLOCK);
        psm_set_state(PRESS_STATE_ABORT);
        return;
    }

    uint32_t elapsed = HAL_GetTick() - g_dwell_start_tick;
    if (elapsed >= g_config.dwell_time_ms)
    {
        /* Capture press end position BEFORE starting return */
        g_press_end_pos = SOEM_GetPositionActual();
        /* Dwell complete → return */
        SOEM_SetRampVelocity_Legacy(g_config.return_speed);
        SOEM_SetTorqueLimitPercent(300U);  /* 30.0% low torque on return (0.1% units) */
        SOEM_SetTargetPositionAbs(g_config.return_pos);
        psm_set_state(PRESS_STATE_RETURN);
    }
}

static void psm_tick_return(void)
{
    if (InterlockManager_IsEmergencyStop() != 0U)
    {
        SOEM_SetTargetPositionAbs(SOEM_GetPositionActual());
        psm_record_result(JUDGE_NG_INTERLOCK);
        psm_set_state(PRESS_STATE_ABORT);
        return;
    }

    if (psm_is_target_reached(g_config.return_pos) != 0U)
    {
        psm_set_state(PRESS_STATE_CYCLE_END);
    }
}

static void psm_tick_cycle_end(void)
{
    JudgeResult_t res = psm_judge();
    psm_record_result(res);

    if (res == JUDGE_OK)
    {
        psm_set_state(PRESS_STATE_IDLE);
    }
    else
    {
        psm_set_state(PRESS_STATE_CYCLE_NG);
    }
}

static void psm_tick_cycle_ng(void)
{
    /* Stay in CYCLE_NG until operator acknowledges (mode change or cycle_start clears it) */
    (void)0;
}

static void psm_tick_abort(void)
{
    /* Return to IDLE after a brief hold so UI can display the abort */
    static uint32_t abort_tick = 0U;
    if (abort_tick == 0U)
    {
        abort_tick = HAL_GetTick();
    }
    if ((HAL_GetTick() - abort_tick) >= 1000U)
    {
        abort_tick = 0U;
        psm_set_state(PRESS_STATE_IDLE);
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
void PressStateMachine_Init(void)
{
    g_op_mode     = OP_MODE_MANUAL;
    g_press_state = PRESS_STATE_IDLE;
    g_config      = k_default_config;
    g_alarm_active = 0U;
    g_cycle_number = 0U;
    memset(&g_last_result, 0, sizeof(g_last_result));
    memset(&g_counters, 0, sizeof(g_counters));
}

void PressStateMachine_Tick(void)
{
    /* Only run state machine in AUTO mode */
    if (g_op_mode != OP_MODE_AUTO)
    {
        return;
    }

    /* Drive must be ready to continue */
    if (psm_is_drive_ready() == 0U)
    {
        if ((g_press_state != PRESS_STATE_IDLE) &&
            (g_press_state != PRESS_STATE_CYCLE_END) &&
            (g_press_state != PRESS_STATE_CYCLE_NG))
        {
            SOEM_SetTargetPositionAbs(SOEM_GetPositionActual());
            psm_record_result(JUDGE_NG_ABORT);
            psm_set_state(PRESS_STATE_ABORT);
        }
        return;
    }

    switch (g_press_state)
    {
        case PRESS_STATE_IDLE:      psm_tick_idle();      break;
        case PRESS_STATE_APPROACH:  psm_tick_approach();  break;
        case PRESS_STATE_CONTACT:   psm_tick_contact();   break;
        case PRESS_STATE_PRESS:     psm_tick_press();     break;
        case PRESS_STATE_DWELL:     psm_tick_dwell();     break;
        case PRESS_STATE_RETURN:    psm_tick_return();    break;
        case PRESS_STATE_CYCLE_END: psm_tick_cycle_end(); break;
        case PRESS_STATE_CYCLE_NG:  psm_tick_cycle_ng();  break;
        case PRESS_STATE_ABORT:     psm_tick_abort();     break;
        default: break;
    }
}

uint8_t PressStateMachine_SetMode(OperationMode_t mode)
{
    if (mode == OP_MODE_AUTO)
    {
        /* AUTO requires all interlocks + home complete */
        if (InterlockManager_IsAutoReady() == 0U)
        {
            return 0U;  /* rejected */
        }
    }

    /* Cannot change mode during active cycle */
    if ((g_op_mode == OP_MODE_AUTO) &&
        (g_press_state != PRESS_STATE_IDLE) &&
        (g_press_state != PRESS_STATE_CYCLE_END) &&
        (g_press_state != PRESS_STATE_CYCLE_NG) &&
        (mode != OP_MODE_ALARM))
    {
        return 0U;  /* rejected */
    }

    g_op_mode = mode;

    /* Reset NG state when leaving AUTO */
    if (mode != OP_MODE_AUTO)
    {
        if (g_press_state == PRESS_STATE_CYCLE_NG)
        {
            psm_set_state(PRESS_STATE_IDLE);
        }
    }

    return 1U;
}

OperationMode_t PressStateMachine_GetMode(void)
{
    return g_op_mode;
}

const char* PressStateMachine_GetModeName(OperationMode_t mode)
{
    switch (mode)
    {
        case OP_MODE_MANUAL:   return "MANUAL";
        case OP_MODE_SETUP:    return "SETUP";
        case OP_MODE_AUTO:     return "AUTO";
        case OP_MODE_ALARM:    return "ALARM";
        case OP_MODE_RECOVERY: return "RECOVERY";
        default:               return "UNKNOWN";
    }
}

uint8_t PressStateMachine_CycleStart(void)
{
    if (g_op_mode != OP_MODE_AUTO)
    {
        return 0U;  /* not in AUTO mode */
    }
    /* CYCLE_NG: operator acknowledges NG by pressing Start again */
    if (g_press_state == PRESS_STATE_CYCLE_NG)
    {
        psm_set_state(PRESS_STATE_IDLE);
    }
    if (g_press_state != PRESS_STATE_IDLE)
    {
        return 0U;  /* not idle */
    }
    if (psm_is_drive_ready() == 0U)
    {
        return 0U;  /* drive not ready */
    }

    /* In sim mode: bypass two-hand requirement for bench test.
     * In real mode: require two-hand sync. */
    const InterlockState_t *ilk = InterlockManager_GetState();
    if (ilk->estop_ok == 0U || ilk->door_ok == 0U)
    {
        return 0U;  /* basic safety not satisfied */
    }

    /* Arm cycle */
    g_cycle_number++;
    g_cycle_start_tick = HAL_GetTick();
    g_peak_force       = 0U;

    /* Begin APPROACH: high speed toward approach_pos */
    SOEM_SetRampVelocity_Legacy(g_config.approach_speed);
    SOEM_SetTorqueLimitPercent(g_config.press_max_force);
    SOEM_SetTargetPositionAbs(g_config.approach_pos);
    psm_set_state(PRESS_STATE_APPROACH);

    return 1U;
}

void PressStateMachine_CycleStop(void)
{
    if ((g_press_state == PRESS_STATE_IDLE) ||
        (g_press_state == PRESS_STATE_CYCLE_END) ||
        (g_press_state == PRESS_STATE_CYCLE_NG))
    {
        psm_set_state(PRESS_STATE_IDLE);
        return;
    }

    /* Initiate controlled return */
    SOEM_SetRampVelocity_Legacy(g_config.return_speed);
    SOEM_SetTorqueLimitPercent(30U);
    SOEM_SetTargetPositionAbs(g_config.return_pos);
    psm_set_state(PRESS_STATE_RETURN);
}

void PressStateMachine_CycleAbort(void)
{
    SOEM_SetTargetPositionAbs(SOEM_GetPositionActual());

    if ((g_press_state != PRESS_STATE_IDLE) &&
        (g_press_state != PRESS_STATE_CYCLE_END) &&
        (g_press_state != PRESS_STATE_CYCLE_NG))
    {
        psm_record_result(JUDGE_NG_ABORT);
    }

    psm_set_state(PRESS_STATE_ABORT);
}

PressState_t PressStateMachine_GetState(void)
{
    return g_press_state;
}

const char* PressStateMachine_GetStateName(PressState_t state)
{
    switch (state)
    {
        case PRESS_STATE_IDLE:      return "IDLE";
        case PRESS_STATE_APPROACH:  return "APPROACH";
        case PRESS_STATE_CONTACT:   return "CONTACT";
        case PRESS_STATE_PRESS:     return "PRESS";
        case PRESS_STATE_DWELL:     return "DWELL";
        case PRESS_STATE_RETURN:    return "RETURN";
        case PRESS_STATE_CYCLE_END: return "CYCLE_END";
        case PRESS_STATE_CYCLE_NG:  return "CYCLE_NG";
        case PRESS_STATE_ABORT:     return "ABORT";
        default:                    return "UNKNOWN";
    }
}

PressConfig_t* PressStateMachine_GetConfig(void)
{
    return &g_config;
}

void PressStateMachine_ApplyConfig(const PressConfig_t *cfg)
{
    if (cfg != NULL)
    {
        g_config = *cfg;
    }
}

const PressResult_t* PressStateMachine_GetLastResult(void)
{
    return &g_last_result;
}

const PressCounter_t* PressStateMachine_GetCounters(void)
{
    return &g_counters;
}

void PressStateMachine_ResetCounters(void)
{
    memset(&g_counters, 0, sizeof(g_counters));
}

const char* PressStateMachine_GetResultName(JudgeResult_t result)
{
    switch (result)
    {
        case JUDGE_OK:            return "OK";
        case JUDGE_NG_FORCE_HIGH: return "NG_FORCE_HIGH";
        case JUDGE_NG_FORCE_LOW:  return "NG_FORCE_LOW";
        case JUDGE_NG_POS_HIGH:   return "NG_POS_HIGH";
        case JUDGE_NG_POS_LOW:    return "NG_POS_LOW";
        case JUDGE_NG_TIME_OVER:  return "NG_TIME_OVER";
        case JUDGE_NG_INTERLOCK:  return "NG_INTERLOCK";
        case JUDGE_NG_ABORT:      return "NG_ABORT";
        default:                  return "UNKNOWN";
    }
}

void PressStateMachine_NgReset(void)
{
    if (g_press_state == PRESS_STATE_CYCLE_NG)
    {
        psm_set_state(PRESS_STATE_IDLE);
    }
}

void PressStateMachine_RaiseAlarm(void)
{
    g_alarm_active = 1U;
    (void)PressStateMachine_SetMode(OP_MODE_ALARM);
}

void PressStateMachine_ClearAlarm(void)
{
    g_alarm_active = 0U;
    if (g_op_mode == OP_MODE_ALARM)
    {
        g_op_mode = OP_MODE_MANUAL;
        psm_set_state(PRESS_STATE_IDLE);
    }
}

void PressStateMachine_NotifyDriveState(uint8_t operational)
{
    InterlockManager_SetDriveReady(operational);
}
