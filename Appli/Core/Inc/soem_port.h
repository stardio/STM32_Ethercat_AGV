/**
 * @file    soem_port.h
 * @brief   3-axis EtherCAT CSP master — public API
 *
 * All motion functions take an AxisId_t (AXIS_J1/Y/Z) parameter.
 * The single-axis legacy names are retained as axis-0 wrappers
 * for backward compatibility with existing main.c code until full migration.
 *
 * Thread safety:
 *   EtherCAT_Task (1ms, Realtime priority) : calls SOEM_PeriodicPoll()
 *   DefaultTask   (low priority)           : calls all Set/Get APIs
 *   Accessors read volatile g_shadow[] — safe from any task.
 */
#ifndef SOEM_PORT_H
#define SOEM_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "axis_types.h"

/* Number of values returned by SOEM_FetchParamRead() */
#define SOEM_PARAM_READ_COUNT   8U

#ifdef SOEM_ENABLED
#include "soem/soem.h"

#ifndef SOEM_IFNAME
#define SOEM_IFNAME "st_eth"
#endif

extern ecx_contextt soem_context;

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/** One-time hardware init (call before FreeRTOS scheduler starts or from task). */
void SOEM_PortInit(void);

/**
 * Main EtherCAT polling function.
 * Handles slave config state machine, then steady-state PDO + CiA402 FSM.
 * Called every 1 ms from EtherCAT_Task via vTaskDelayUntil.
 */
void SOEM_PeriodicPoll(void);

/** Register a log callback (called from SOEM internals and CiA402 FSM). */
void SOEM_PortSetLog(void (*log_fn)(const char *msg));

/** Returns number of slaves detected and active (0..AXIS_COUNT). */
uint8_t SOEM_GetActiveAxes(void);

/* ── Per-axis data reads (from volatile shadow — safe any task) ─────────── */

int32_t  SOEM_GetPositionHw(AxisId_t ax);         /* raw HW encoder counts */
int32_t  SOEM_GetPositionUser(AxisId_t ax);        /* user units (mm)       */
int32_t  SOEM_GetPositionCenti(AxisId_t ax);       /* 0.01 mm for display   */
int32_t  SOEM_GetVelocity(AxisId_t ax);            /* user-units/s          */
int16_t  SOEM_GetTorque(AxisId_t ax);              /* per-mille (0.1%)      */
uint16_t SOEM_GetStatusword(AxisId_t ax);
uint8_t  SOEM_GetCia402State(AxisId_t ax);         /* Cia402State_t         */
uint8_t  SOEM_GetPdoReady(AxisId_t ax);
uint8_t  SOEM_GetRunEnable(AxisId_t ax);
uint8_t  SOEM_IsTargetReached(AxisId_t ax);

/** Fill caller's AxisStatusPkt_t snapshot (12 bytes, used by web protocol). */
void     SOEM_GetStatusPkt(AxisId_t ax, AxisStatusPkt_t *pkt);

/** 1 if ALL active axes are in OP_ENABLED and PDO ready. */
uint8_t  SOEM_AllAxesReady(void);

/** 1 if ALL active axes have reached their target. */
uint8_t  SOEM_AllTargetsReached(void);

/* ── Fault recovery ──────────────────────────────────────────────────────── */

/**
 * @brief  Force a CiA402 fault-reset cycle on the specified axis (or all).
 *         Clears fault_active and restarts the enable sequence from Shutdown.
 *         Safe to call from DefaultTask at any time; takes effect within 1 ms.
 * @param  ax  AxisId_t (AXIS_J1..AXIS_J6), or AXIS_ALL (0xFF) for all active axes.
 */
void SOEM_FaultReset(AxisId_t ax);

/* ── Run enable ──────────────────────────────────────────────────────────── */

void SOEM_SetRunEnable(AxisId_t ax, uint8_t enable);

/** Convenience: enable/disable all active axes atomically. */
void SOEM_SetAllRunEnable(uint8_t enable);

/* ── Target position ─────────────────────────────────────────────────────── */

/** Set absolute target (user units). Clamped to soft limits. */
void SOEM_SetTargetUser(AxisId_t ax, int32_t pos_user);

/** Set absolute target (HW encoder counts). Clamped to soft limits. */
void SOEM_SetTargetHw(AxisId_t ax, int32_t pos_hw);

/** Relative move from current commanded position (user units). */
void SOEM_SetTargetDelta(AxisId_t ax, int32_t delta_user);

/**
 * @brief  Set the next interpolated target for G01/G02/G03.
 *         Writes BOTH target_hw and target_hw_out to bypass the SOEM ramp
 *         generator.  Call once per axis per 1ms tick from Interp_Tick(),
 *         before SOEM_PeriodicPoll() executes.
 * @note   NOT clamped to soft limits (interpolator is responsible for
 *         planning within the workspace envelope).
 */
void SOEM_SetInterpolatedTarget(AxisId_t ax, int32_t hw);

/* ── CSV velocity (AGV direct velocity command) ──────────────────────────── */

/**
 * Set CSV target velocity directly [HW counts/s].
 * Written to PDO TargetVelocity (0x60FF) every 1ms cycle.
 * Use for AGV differential drive; call from DefaultTask.
 */
void SOEM_SetTargetVelocity(AxisId_t ax, int32_t vel_hw);

/* ── Velocity / profile ──────────────────────────────────────────────────── */

/**
 * Set ramp velocity without SDO write (local ramp only).
 * Use for JOG or real-time speed changes.
 */
void SOEM_SetRampVelocity(AxisId_t ax, int32_t vel_user);

/** Immediately snap all axes' target positions to the actual encoder position.
 *  Used to cancel pending JOG motion when the jog button is released. */
void SOEM_LatchAllTargetToActual(void);

/**
 * Set profile velocity with drive SDO write pending.
 * Applied when motor is stationary (safety gate).
 */
void SOEM_SetProfileVelocity(AxisId_t ax, int32_t vel_user);

/** Set acceleration ramp time (ms). SDO pending write. */
void SOEM_SetProfileAccel(AxisId_t ax, int32_t accel_ms);

/** Set deceleration ramp time (ms). SDO pending write. */
void SOEM_SetProfileDecel(AxisId_t ax, int32_t decel_ms);

/** Set torque ceiling (per-mille, 1000 = 100.0%). SDO pending write. */
void SOEM_SetTorqueLimit(AxisId_t ax, uint16_t permille);

/**
 * Copy velocity/accel/decel/torque from g_axis_param[] into g_rt[].
 * Call after RobotFlash_Load() so that flash-restored values take effect
 * in the software ramp (g_rt.profile_velocity is used by step_per_cycle).
 */
void SOEM_SyncRtFromAxisParam(void);

/* ── Unit scaling ────────────────────────────────────────────────────────── */

/** Set unit scale (HW counts per user-unit). Triggers SDO write. */
void SOEM_SetUnitScale(AxisId_t ax, int32_t scale);

/* ── Homing ──────────────────────────────────────────────────────────────── */

/**
 * Passive calibration: set current HW position as the user-unit origin.
 * Equivalent to: home_offset = pos_actual_hw - (user_home_param * unit_scale)
 */
void SOEM_SetHomePosition(AxisId_t ax);

/**
 * Load a previously saved HW offset from flash.
 * Called at boot by main.c before CiA402 enables.
 */
void SOEM_LoadHomeHwOffset(AxisId_t ax, int32_t hw_offset);

int32_t SOEM_GetHomeOffset(AxisId_t ax);

/* ── Software limits ─────────────────────────────────────────────────────── */

/** Set positive (max) travel limit in user units. */
void SOEM_SetLimitPlusUser(AxisId_t ax, int32_t lim_user);

/** Set negative (min) travel limit in user units. */
void SOEM_SetLimitMinusUser(AxisId_t ax, int32_t lim_user);

/** Capture positive limit at current position. */
void SOEM_CaptureLimitPlusHere(AxisId_t ax);

/** Capture negative limit at current position. */
void SOEM_CaptureLimitMinusHere(AxisId_t ax);

/** Override limit directly in HW counts (for restore from flash). */
void SOEM_SetLimitPlusHw(AxisId_t ax, int32_t hw);
void SOEM_SetLimitMinusHw(AxisId_t ax, int32_t hw);

int32_t SOEM_GetLimitPlusHw(AxisId_t ax);
int32_t SOEM_GetLimitMinusHw(AxisId_t ax);

/** Re-derive limits_enabled for all axes from their stored user values.
 *  Call once after RobotFlash_Load() so that flash-restored limits are active. */
void SOEM_RefreshAllLimits(void);

/* ── Position gain ───────────────────────────────────────────────────────── */

/** Write Kp to drive (SDO pending, safety gated). 0 = skip. */
void    SOEM_SetPositionGain(AxisId_t ax, int32_t gain);

/** Request async read of position gain from drive. */
void    SOEM_RequestGainRead(AxisId_t ax);

/** Poll for result. Returns 1 and fills *gain when ready, else 0. */
uint8_t SOEM_FetchGainRead(AxisId_t ax, int32_t *gain);

int32_t SOEM_GetGainReadStatus(AxisId_t ax);

/* ── Parameter bulk read ─────────────────────────────────────────────────── */

/** Request async read of all drive parameters for one axis. */
void    SOEM_RequestParamRead(AxisId_t ax);

/**
 * Poll for result.
 * Returns 1 and fills values[0..SOEM_PARAM_READ_COUNT-1] when ready.
 * Layout: [0]=jog_spd [1]=accel [2]=decel [3]=lim+ [4]=lim-
 *         [5]=unit_scale [6]=home_offset [7]=pos_gain
 */
uint8_t SOEM_FetchParamRead(AxisId_t ax, int32_t *values, uint8_t count);

/* ────────────────────────────────────────────────────────────────────────── */
/* Legacy single-axis wrappers (axis 0 only).                                */
/* Kept for backward compatibility with existing main.c call sites.          */
/* Will be removed after main.c is migrated to multi-axis protocol.          */
/* ────────────────────────────────────────────────────────────────────────── */
static inline int32_t  SOEM_GetPositionActual(void)
    { return SOEM_GetPositionUser(AXIS_J1); }
static inline int32_t  SOEM_GetPositionActualHw(void)
    { return SOEM_GetPositionHw(AXIS_J1); }
static inline int32_t  SOEM_GetPositionActualCenti(void)
    { return SOEM_GetPositionCenti(AXIS_J1); }
static inline int32_t  SOEM_GetVelocityActual(void)
    { return SOEM_GetVelocity(AXIS_J1); }
static inline int16_t  SOEM_GetTorqueActual(void)
    { return SOEM_GetTorque(AXIS_J1); }
static inline uint16_t SOEM_GetStatusword_Legacy(void)
    { return SOEM_GetStatusword(AXIS_J1); }
static inline uint8_t  SOEM_GetPdoReady_Legacy(void)
    { return SOEM_GetPdoReady(AXIS_J1); }
static inline uint8_t  SOEM_GetRunEnable_Legacy(void)
    { return SOEM_GetRunEnable(AXIS_J1); }
static inline void SOEM_SetRunEnable_Legacy(uint8_t en)
    { SOEM_SetRunEnable(AXIS_J1, en); }
static inline void SOEM_SetTargetPositionAbs(int32_t pos)
    { SOEM_SetTargetUser(AXIS_J1, pos); }
static inline void SOEM_SetTargetPositionAbsHw(int32_t hw)
    { SOEM_SetTargetHw(AXIS_J1, hw); }
static inline void SOEM_SetTargetPositionDelta(int32_t d)
    { SOEM_SetTargetDelta(AXIS_J1, d); }
static inline void SOEM_SetRampVelocity_Legacy(int32_t v)
    { SOEM_SetRampVelocity(AXIS_J1, v); }
static inline void SOEM_SetProfileVelocity_Legacy(int32_t v)
    { SOEM_SetProfileVelocity(AXIS_J1, v); }
static inline void SOEM_SetTorqueLimitPercent(uint16_t p)
    { SOEM_SetTorqueLimit(AXIS_J1, p); }
static inline void SOEM_SetProfileAcceleration(int32_t a)
    { SOEM_SetProfileAccel(AXIS_J1, a); }
static inline void SOEM_SetProfileDeceleration(int32_t d)
    { SOEM_SetProfileDecel(AXIS_J1, d); }
static inline void SOEM_SetSoftwareLimitPlus(int32_t l)
    { SOEM_SetLimitPlusUser(AXIS_J1, l); }
static inline void SOEM_SetSoftwareLimitMinus(int32_t l)
    { SOEM_SetLimitMinusUser(AXIS_J1, l); }
static inline void SOEM_CaptureLimitPlusHere_Legacy(void)
    { SOEM_CaptureLimitPlusHere(AXIS_J1); }
static inline void SOEM_CaptureLimitMinusHere_Legacy(void)
    { SOEM_CaptureLimitMinusHere(AXIS_J1); }
static inline void SOEM_SetSoftwareLimitPlusHw(int32_t h)
    { SOEM_SetLimitPlusHw(AXIS_J1, h); }
static inline void SOEM_SetSoftwareLimitMinusHw(int32_t h)
    { SOEM_SetLimitMinusHw(AXIS_J1, h); }
static inline int32_t SOEM_GetLimitPlusHw_Legacy(void)
    { return SOEM_GetLimitPlusHw(AXIS_J1); }
static inline int32_t SOEM_GetLimitMinusHw_Legacy(void)
    { return SOEM_GetLimitMinusHw(AXIS_J1); }
static inline void SOEM_SetUnitScale_Legacy(int32_t s)
    { SOEM_SetUnitScale(AXIS_J1, s); }
static inline void SOEM_SetHomePosition_Legacy(void)
    { SOEM_SetHomePosition(AXIS_J1); }
static inline int32_t SOEM_GetHomeOffset_Legacy(void)
    { return SOEM_GetHomeOffset(AXIS_J1); }
static inline void SOEM_LoadHomeHwOffset_Legacy(int32_t o)
    { SOEM_LoadHomeHwOffset(AXIS_J1, o); }
static inline void SOEM_SetPositionGain_Legacy(int32_t g)
    { SOEM_SetPositionGain(AXIS_J1, g); }
static inline void SOEM_RequestParameterReadAll(void)
    { SOEM_RequestParamRead(AXIS_J1); }
static inline uint8_t SOEM_FetchParameterReadAll(int32_t *v, uint8_t c)
    { return SOEM_FetchParamRead(AXIS_J1, v, c); }
static inline void SOEM_RequestPositionGainRead(void)
    { SOEM_RequestGainRead(AXIS_J1); }
static inline uint8_t SOEM_FetchPositionGainRead(int32_t *g)
    { return SOEM_FetchGainRead(AXIS_J1, g); }
static inline int32_t SOEM_GetPositionGainReadStatus(void)
    { return SOEM_GetGainReadStatus(AXIS_J1); }
/* Legacy alias for SOEM_PeriodicPoll (called as SOEM_PortPoll in main.c) */
static inline void SOEM_PortPoll(void);  /* defined after SOEM_PeriodicPoll */

#else /* !SOEM_ENABLED — stub implementations */

static inline void    SOEM_FaultReset(AxisId_t ax)                         { (void)ax; }
static inline void    SOEM_PortInit(void)                                  {}
static inline void    SOEM_PeriodicPoll(void)                              {}
static inline void    SOEM_PortSetLog(void (*f)(const char *))             { (void)f; }
static inline uint8_t SOEM_GetActiveAxes(void)                            { return 0; }
static inline int32_t  SOEM_GetPositionHw(AxisId_t ax)                    { (void)ax; return 0; }
static inline int32_t  SOEM_GetPositionUser(AxisId_t ax)                  { (void)ax; return 0; }
static inline int32_t  SOEM_GetPositionCenti(AxisId_t ax)                 { (void)ax; return 0; }
static inline int32_t  SOEM_GetVelocity(AxisId_t ax)                      { (void)ax; return 0; }
static inline int16_t  SOEM_GetTorque(AxisId_t ax)                        { (void)ax; return 0; }
static inline uint16_t SOEM_GetStatusword(AxisId_t ax)                    { (void)ax; return 0; }
static inline uint8_t  SOEM_GetCia402State(AxisId_t ax)                   { (void)ax; return 0; }
static inline uint8_t  SOEM_GetPdoReady(AxisId_t ax)                      { (void)ax; return 0; }
static inline uint8_t  SOEM_GetRunEnable(AxisId_t ax)                     { (void)ax; return 0; }
static inline uint8_t  SOEM_IsTargetReached(AxisId_t ax)                  { (void)ax; return 0; }
static inline void     SOEM_GetStatusPkt(AxisId_t ax, AxisStatusPkt_t *p) { (void)ax; (void)p; }
static inline uint8_t  SOEM_AllAxesReady(void)                            { return 0; }
static inline uint8_t  SOEM_AllTargetsReached(void)                       { return 0; }
static inline void     SOEM_SetTargetVelocity(AxisId_t ax, int32_t v)      { (void)ax; (void)v; }
static inline void     SOEM_SetRunEnable(AxisId_t ax, uint8_t e)          { (void)ax; (void)e; }
static inline void     SOEM_SetAllRunEnable(uint8_t e)                    { (void)e; }
static inline void     SOEM_SetTargetUser(AxisId_t ax, int32_t p)         { (void)ax; (void)p; }
static inline void     SOEM_SetTargetHw(AxisId_t ax, int32_t h)           { (void)ax; (void)h; }
static inline void     SOEM_SetTargetDelta(AxisId_t ax, int32_t d)        { (void)ax; (void)d; }
static inline void     SOEM_SetInterpolatedTarget(AxisId_t ax, int32_t h) { (void)ax; (void)h; }
static inline void     SOEM_SetRampVelocity(AxisId_t ax, int32_t v)       { (void)ax; (void)v; }
static inline void     SOEM_LatchAllTargetToActual(void)                   { }
static inline void     SOEM_SetProfileVelocity(AxisId_t ax, int32_t v)    { (void)ax; (void)v; }
static inline void     SOEM_SetProfileAccel(AxisId_t ax, int32_t a)       { (void)ax; (void)a; }
static inline void     SOEM_SetProfileDecel(AxisId_t ax, int32_t d)       { (void)ax; (void)d; }
static inline void     SOEM_SetTorqueLimit(AxisId_t ax, uint16_t p)       { (void)ax; (void)p; }
static inline void     SOEM_SetUnitScale(AxisId_t ax, int32_t s)          { (void)ax; (void)s; }
static inline void     SOEM_SetHomePosition(AxisId_t ax)                  { (void)ax; }
static inline void     SOEM_LoadHomeHwOffset(AxisId_t ax, int32_t o)      { (void)ax; (void)o; }
static inline int32_t  SOEM_GetHomeOffset(AxisId_t ax)                    { (void)ax; return 0; }
static inline void     SOEM_SetLimitPlusUser(AxisId_t ax, int32_t l)      { (void)ax; (void)l; }
static inline void     SOEM_SetLimitMinusUser(AxisId_t ax, int32_t l)     { (void)ax; (void)l; }
static inline void     SOEM_CaptureLimitPlusHere(AxisId_t ax)             { (void)ax; }
static inline void     SOEM_CaptureLimitMinusHere(AxisId_t ax)            { (void)ax; }
static inline void     SOEM_SetLimitPlusHw(AxisId_t ax, int32_t h)        { (void)ax; (void)h; }
static inline void     SOEM_SetLimitMinusHw(AxisId_t ax, int32_t h)       { (void)ax; (void)h; }
static inline int32_t  SOEM_GetLimitPlusHw(AxisId_t ax)                   { (void)ax; return INT32_MAX; }
static inline int32_t  SOEM_GetLimitMinusHw(AxisId_t ax)                  { (void)ax; return INT32_MIN; }
static inline void     SOEM_RefreshAllLimits(void)                         { }
static inline void     SOEM_SetPositionGain(AxisId_t ax, int32_t g)       { (void)ax; (void)g; }
static inline void     SOEM_RequestGainRead(AxisId_t ax)                   { (void)ax; }
static inline uint8_t  SOEM_FetchGainRead(AxisId_t ax, int32_t *g)        { (void)ax; (void)g; return 0; }
static inline int32_t  SOEM_GetGainReadStatus(AxisId_t ax)                { (void)ax; return 0; }
static inline void     SOEM_RequestParamRead(AxisId_t ax)                  { (void)ax; }
static inline uint8_t  SOEM_FetchParamRead(AxisId_t ax, int32_t *v, uint8_t c)
    { (void)ax; (void)v; (void)c; return 0; }

/* Legacy stubs for SOEM_DISABLED build */
static inline int32_t  SOEM_GetPositionActual(void)         { return 0; }
static inline int32_t  SOEM_GetPositionActualHw(void)       { return 0; }
static inline int32_t  SOEM_GetPositionActualCenti(void)    { return 0; }
static inline int32_t  SOEM_GetVelocityActual(void)         { return 0; }
static inline int16_t  SOEM_GetTorqueActual(void)           { return 0; }
static inline uint16_t SOEM_GetStatusword_Legacy(void)      { return 0; }
static inline uint8_t  SOEM_GetPdoReady_Legacy(void)        { return 0; }
static inline uint8_t  SOEM_GetRunEnable_Legacy(void)       { return 0; }
static inline void SOEM_SetRunEnable_Legacy(uint8_t e)      { (void)e; }
static inline void SOEM_SetTargetPositionAbs(int32_t p)     { (void)p; }
static inline void SOEM_SetTargetPositionAbsHw(int32_t h)   { (void)h; }
static inline void SOEM_SetTargetPositionDelta(int32_t d)   { (void)d; }
static inline void SOEM_SetRampVelocity_Legacy(int32_t v)   { (void)v; }
static inline void SOEM_SetProfileVelocity_Legacy(int32_t v){ (void)v; }
static inline void SOEM_SetTorqueLimitPercent(uint16_t p)   { (void)p; }
static inline void SOEM_SetProfileAcceleration(int32_t a)   { (void)a; }
static inline void SOEM_SetProfileDeceleration(int32_t d)   { (void)d; }
static inline void SOEM_SetSoftwareLimitPlus(int32_t l)     { (void)l; }
static inline void SOEM_SetSoftwareLimitMinus(int32_t l)    { (void)l; }
static inline void SOEM_CaptureLimitPlusHere_Legacy(void)   {}
static inline void SOEM_CaptureLimitMinusHere_Legacy(void)  {}
static inline void SOEM_SetSoftwareLimitPlusHw(int32_t h)   { (void)h; }
static inline void SOEM_SetSoftwareLimitMinusHw(int32_t h)  { (void)h; }
static inline int32_t SOEM_GetLimitPlusHw_Legacy(void)      { return INT32_MAX; }
static inline int32_t SOEM_GetLimitMinusHw_Legacy(void)     { return INT32_MIN; }
static inline void SOEM_SetUnitScale_Legacy(int32_t s)      { (void)s; }
static inline void SOEM_SetHomePosition_Legacy(void)        {}
static inline int32_t SOEM_GetHomeOffset_Legacy(void)       { return 0; }
static inline void SOEM_LoadHomeHwOffset_Legacy(int32_t o)  { (void)o; }
static inline void SOEM_SetPositionGain_Legacy(int32_t g)   { (void)g; }
static inline void SOEM_RequestParameterReadAll(void)       {}
static inline uint8_t SOEM_FetchParameterReadAll(int32_t *v, uint8_t c)
    { (void)v; (void)c; return 0; }
static inline void     SOEM_RequestPositionGainRead(void)   {}
static inline uint8_t  SOEM_FetchPositionGainRead(int32_t *g) { (void)g; return 0; }
static inline int32_t  SOEM_GetPositionGainReadStatus(void) { return 0; }

#endif /* SOEM_ENABLED */

/* ── Legacy alias: SOEM_PortPoll() → SOEM_PeriodicPoll() ──────────────── */
#ifdef SOEM_ENABLED
static inline void SOEM_PortPoll(void) { SOEM_PeriodicPoll(); }
#else
static inline void SOEM_PortPoll(void) {}
#endif

#ifdef __cplusplus
}
#endif

#endif /* SOEM_PORT_H */
