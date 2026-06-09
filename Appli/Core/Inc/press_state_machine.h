#ifndef PRESS_STATE_MACHINE_H
#define PRESS_STATE_MACHINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* -----------------------------------------------------------------------
 * Operation mode
 * ----------------------------------------------------------------------- */
typedef enum {
    OP_MODE_MANUAL   = 0,
    OP_MODE_SETUP    = 1,
    OP_MODE_AUTO     = 2,
    OP_MODE_ALARM    = 3,
    OP_MODE_RECOVERY = 4,
} OperationMode_t;

/* -----------------------------------------------------------------------
 * Press cycle states
 * ----------------------------------------------------------------------- */
typedef enum {
    PRESS_STATE_IDLE       = 0,
    PRESS_STATE_APPROACH   = 1,
    PRESS_STATE_CONTACT    = 2,
    PRESS_STATE_PRESS      = 3,
    PRESS_STATE_DWELL      = 4,
    PRESS_STATE_RETURN     = 5,
    PRESS_STATE_CYCLE_END  = 6,
    PRESS_STATE_CYCLE_NG   = 7,
    PRESS_STATE_ABORT      = 8,
} PressState_t;

/* -----------------------------------------------------------------------
 * Quality judgment result codes
 * ----------------------------------------------------------------------- */
typedef enum {
    JUDGE_OK              = 0,
    JUDGE_NG_FORCE_HIGH   = 1,
    JUDGE_NG_FORCE_LOW    = 2,
    JUDGE_NG_POS_HIGH     = 3,
    JUDGE_NG_POS_LOW      = 4,
    JUDGE_NG_TIME_OVER    = 5,
    JUDGE_NG_INTERLOCK    = 6,
    JUDGE_NG_ABORT        = 7,
} JudgeResult_t;

/* -----------------------------------------------------------------------
 * Press recipe configuration (Phase 1: simple defaults, expanded in Phase 2)
 * ----------------------------------------------------------------------- */
typedef struct {
    int32_t  approach_speed;        /* user units/s, high speed */
    int32_t  approach_pos;          /* user units: position to switch to contact speed */
    int32_t  contact_speed;         /* user units/s, slow contact search */
    uint16_t contact_torque_th;     /* % torque: contact detection threshold */
    int32_t  press_speed;           /* user units/s */
    int32_t  press_target_pos;      /* user units: final press position */
    uint16_t press_max_force;       /* % torque: press force ceiling */
    uint32_t dwell_time_ms;         /* ms: hold time at press position */
    int32_t  return_speed;          /* user units/s */
    int32_t  return_pos;            /* user units: standby position after return */
    uint32_t cycle_timeout_ms;      /* ms: full cycle timeout (0 = disabled) */

    /* Quality judgment limits */
    uint16_t judge_force_max;       /* % torque upper bound */
    uint16_t judge_force_min;       /* % torque lower bound */
    int32_t  judge_pos_max;         /* user units upper bound */
    int32_t  judge_pos_min;         /* user units lower bound */
    uint32_t judge_cycle_time_max;  /* ms upper bound (0 = disabled) */
} PressConfig_t;

/* -----------------------------------------------------------------------
 * Cycle result (available after CYCLE_END or CYCLE_NG)
 * ----------------------------------------------------------------------- */
typedef struct {
    uint32_t      cycle_number;
    JudgeResult_t result;
    uint16_t      peak_force_pct;
    int32_t       end_position;     /* user units */
    uint32_t      cycle_time_ms;
} PressResult_t;

/* -----------------------------------------------------------------------
 * Production counters
 * ----------------------------------------------------------------------- */
typedef struct {
    uint32_t total;
    uint32_t ok;
    uint32_t ng;
    uint32_t ng_force_high;
    uint32_t ng_force_low;
    uint32_t ng_pos_high;
    uint32_t ng_pos_low;
    uint32_t ng_time_over;
    uint32_t ng_interlock;
    uint32_t consecutive_ng;
    uint8_t  ng_rate_x10;    /* NG rate * 10, range 0-1000 = 0.0-100.0% */
} PressCounter_t;

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
void              PressStateMachine_Init(void);
void              PressStateMachine_Tick(void);

/* Mode control */
uint8_t           PressStateMachine_SetMode(OperationMode_t mode);
OperationMode_t   PressStateMachine_GetMode(void);
const char*       PressStateMachine_GetModeName(OperationMode_t mode);

/* Cycle control */
uint8_t           PressStateMachine_CycleStart(void);
void              PressStateMachine_CycleStop(void);
void              PressStateMachine_CycleAbort(void);

/* State access */
PressState_t      PressStateMachine_GetState(void);
const char*       PressStateMachine_GetStateName(PressState_t state);

/* Config access */
PressConfig_t*    PressStateMachine_GetConfig(void);
void              PressStateMachine_ApplyConfig(const PressConfig_t *cfg);

/* Result and counters */
const PressResult_t*  PressStateMachine_GetLastResult(void);
const PressCounter_t* PressStateMachine_GetCounters(void);
void                  PressStateMachine_ResetCounters(void);
const char*           PressStateMachine_GetResultName(JudgeResult_t result);
void                  PressStateMachine_NgReset(void);

/* Alarm interface (used by alarm_manager) */
void              PressStateMachine_RaiseAlarm(void);
void              PressStateMachine_ClearAlarm(void);

/* Notified by SOEM drive state */
void              PressStateMachine_NotifyDriveState(uint8_t operational);

#ifdef __cplusplus
}
#endif

#endif /* PRESS_STATE_MACHINE_H */
