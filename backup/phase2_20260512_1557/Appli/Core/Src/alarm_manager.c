#include "alarm_manager.h"
#include "press_state_machine.h"
#include "interlock_manager.h"
#include "stm32h7rsxx_hal.h"
#include <string.h>
#include <stdio.h>

static AlarmRecord_t g_history[ALARM_LOG_MAX];
static uint8_t       g_hist_head  = 0U;
static uint8_t       g_hist_count = 0U;

static AlarmCode_t   g_active_code = ALARM_NONE;
static uint8_t       g_ack_done    = 0U;
static uint8_t       g_estop_prev  = 1U;
static uint8_t       g_door_prev   = 1U;

static const char* alarm_message(AlarmCode_t code)
{
    switch (code)
    {
        case ALARM_ESTOP:            return "E-Stop activated";
        case ALARM_DOOR_OPEN:        return "Safety door opened";
        case ALARM_TWOHAND_RELEASED: return "Two-hand released during cycle";
        case ALARM_DRIVE_FAULT:      return "Drive fault";
        case ALARM_DRIVE_NOT_READY:  return "Drive not ready";
        case ALARM_CYCLE_TIMEOUT:    return "Cycle timeout";
        case ALARM_OVERLOAD:         return "Press overload";
        case ALARM_POS_ERROR:        return "Position error";
        case ALARM_CONSECUTIVE_NG:   return "Consecutive NG limit reached";
        case ALARM_NVM_ERROR:        return "NVM read/write error";
        case ALARM_COMM_ERROR:       return "Communication error";
        default:                     return "Unknown alarm";
    }
}

static void push_history(AlarmCode_t code)
{
    AlarmRecord_t *r = &g_history[g_hist_head];
    memset(r, 0, sizeof(*r));
    r->code        = code;
    r->occurred_ms = HAL_GetTick();
    r->cleared_ms  = 0U;
    r->ack_done    = 0U;
    strncpy(r->message, alarm_message(code), sizeof(r->message) - 1U);

    g_hist_head = (uint8_t)((g_hist_head + 1U) % ALARM_LOG_MAX);
    if (g_hist_count < ALARM_LOG_MAX) { g_hist_count++; }
}

void AlarmManager_Init(void)
{
    memset(g_history, 0, sizeof(g_history));
    g_hist_head  = 0U;
    g_hist_count = 0U;
    g_active_code = ALARM_NONE;
    g_ack_done    = 0U;
    g_estop_prev  = 1U;
    g_door_prev   = 1U;
}

void AlarmManager_Tick(void)
{
    const InterlockState_t *ilk = InterlockManager_GetState();

    /* E-Stop rising edge (estop_ok: 1→0 means E-Stop pressed) */
    if ((ilk->estop_ok == 0U) && (g_estop_prev != 0U))
    {
        AlarmManager_Raise(ALARM_ESTOP);
    }
    g_estop_prev = ilk->estop_ok;

    /* Door open during AUTO run */
    if ((ilk->door_ok == 0U) && (g_door_prev != 0U))
    {
        if (PressStateMachine_GetMode() == OP_MODE_AUTO)
        {
            AlarmManager_Raise(ALARM_DOOR_OPEN);
        }
    }
    g_door_prev = ilk->door_ok;

    /* Consecutive NG threshold */
    const PressCounter_t *cnt = PressStateMachine_GetCounters();
    if (cnt->consecutive_ng >= ALARM_CONSECUTIVE_NG_LIMIT)
    {
        if (g_active_code == ALARM_NONE)
        {
            AlarmManager_Raise(ALARM_CONSECUTIVE_NG);
        }
    }
}

void AlarmManager_Raise(AlarmCode_t code)
{
    if (g_active_code != ALARM_NONE) { return; }  /* keep first alarm */

    g_active_code = code;
    g_ack_done    = 0U;
    push_history(code);
    PressStateMachine_RaiseAlarm();
}

void AlarmManager_Ack(void)
{
    g_ack_done = 1U;

    /* Mark latest history entry as ACKed */
    if (g_hist_count > 0U)
    {
        uint8_t latest = (uint8_t)((g_hist_head == 0U) ? (ALARM_LOG_MAX - 1U) : (g_hist_head - 1U));
        g_history[latest].ack_done = 1U;
    }
}

void AlarmManager_Reset(void)
{
    if (g_ack_done == 0U) { return; }  /* must ACK first */

    /* Check that the cause is gone */
    const InterlockState_t *ilk = InterlockManager_GetState();
    if ((g_active_code == ALARM_ESTOP) && (ilk->estop_ok == 0U)) { return; }
    if ((g_active_code == ALARM_DOOR_OPEN) && (ilk->door_ok == 0U)) { return; }

    /* Mark cleared in history */
    if (g_hist_count > 0U)
    {
        uint8_t latest = (uint8_t)((g_hist_head == 0U) ? (ALARM_LOG_MAX - 1U) : (g_hist_head - 1U));
        g_history[latest].cleared_ms = HAL_GetTick();
    }

    g_active_code = ALARM_NONE;
    g_ack_done    = 0U;
    PressStateMachine_ClearAlarm();
}

AlarmCode_t AlarmManager_GetActive(void)
{
    return g_active_code;
}

uint8_t AlarmManager_IsActive(void)
{
    return (g_active_code != ALARM_NONE) ? 1U : 0U;
}

uint8_t AlarmManager_IsAcked(void)
{
    return g_ack_done;
}

uint8_t AlarmManager_GetHistoryCount(void)
{
    return g_hist_count;
}

const AlarmRecord_t* AlarmManager_GetHistoryEntry(uint8_t offset)
{
    if ((g_hist_count == 0U) || (offset >= g_hist_count)) { return NULL; }

    int32_t idx = (int32_t)g_hist_head - 1 - (int32_t)offset;
    while (idx < 0) { idx += (int32_t)ALARM_LOG_MAX; }
    return &g_history[(uint8_t)((uint32_t)idx % ALARM_LOG_MAX)];
}

void AlarmManager_ReplyActive(void (*reply_fn)(const char *fmt, ...))
{
    if (reply_fn == NULL) { return; }
    if (g_active_code == ALARM_NONE)
    {
        reply_fn("ALM,code=0,msg=NONE,ack=0");
        return;
    }
    reply_fn("ALM,code=%u,msg=%s,ack=%u",
             (unsigned int)g_active_code,
             alarm_message(g_active_code),
             (unsigned int)g_ack_done);
}
