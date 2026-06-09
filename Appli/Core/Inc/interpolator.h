/**
 * @file    interpolator.h
 * @brief   6-axis articulated robot — joint-space motion interpolator
 *
 * Supported motion types
 * ──────────────────────
 *   JointRapid  Each joint moves to its target at its configured profile
 *               velocity (SOEM ramp handles accel/decel per joint).
 *               All joints start simultaneously; finish independently.
 *               Done when ALL joints have reached their targets.
 *
 *   JointSync   All joints move from current to target using a quintic
 *               (5th-order) polynomial velocity profile.  Every joint
 *               starts and finishes at the same time (JTRAJ).
 *               s(τ) = 10τ³ − 15τ⁴ + 6τ⁵   (τ = t / duration)
 *
 * Threading model
 * ───────────────
 *   Interp_Tick()            → EtherCAT_Task (1 ms, highest priority)
 *                               Call BEFORE SOEM_PeriodicPoll() each cycle.
 *   Interp_JointRapid/Sync() → DefaultTask (low priority)
 *                               Commands posted via volatile request buffer;
 *                               picked up by Interp_Tick() within 1 ms.
 *
 * Units
 * ─────
 *   All API joint positions : degrees (float)
 *   duration_s              : seconds (float)
 */
#ifndef INTERPOLATOR_H
#define INTERPOLATOR_H

#include <stdint.h>
#include "axis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Interpolator states ─────────────────────────────────────────────────── */
typedef enum {
    INTERP_IDLE    = 0,   /* no motion, accepts new commands         */
    INTERP_MOVING  = 1,   /* executing a move                        */
    INTERP_DONE    = 2,   /* move complete, call Interp_AckDone()    */
    INTERP_ERROR   = 3,   /* planning error                          */
} InterpState_t;

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

/**
 * @brief  One-time init — call after AxisConfig_InitDefaults() at boot.
 */
void Interp_Init(void);

/**
 * @brief  1 ms tick — call from EtherCAT_Task, before SOEM_PeriodicPoll().
 *
 *  • Latches any pending command from DefaultTask.
 *  • Advances the active move by one step.
 *  • Calls SOEM_SetInterpolatedTarget() (JointSync) or monitors
 *    target_reached flags (JointRapid).
 */
void Interp_Tick(void);

/* ── Status ──────────────────────────────────────────────────────────────── */

InterpState_t Interp_GetState(void);

/** 1 while a move is being executed (state == INTERP_MOVING). */
uint8_t       Interp_IsBusy(void);

/**
 * @brief  Acknowledge a completed move.
 *         Transitions INTERP_DONE → INTERP_IDLE.
 *         No effect in other states.
 */
void Interp_AckDone(void);

/**
 * @brief  Immediate stop.
 *         Holds all joints at their current interpolated positions.
 *         Transitions any state → INTERP_IDLE.
 */
void Interp_Stop(void);

/* ── Move commands ───────────────────────────────────────────────────────── */
/*
 * Return 1 if the command was accepted.
 * Return 0 if a planning error occurred (invalid duration).
 *
 * Posting a command always pre-empts any in-progress move, then starts
 * on the next Interp_Tick() call (within 1 ms).
 */

/**
 * @brief  Joint Rapid — independent rapid move to joint targets.
 *         Each joint moves at its configured profile velocity.
 *         Joints are NOT synchronised; they finish independently.
 *         Done when ALL joints have reached their targets.
 * @param  j1..j6  Target joint angles [degrees].
 */
uint8_t Interp_JointRapid(float j1, float j2, float j3,
                           float j4, float j5, float j6);

/**
 * @brief  Joint Sync — synchronised JTRAJ move (5th-order polynomial).
 *         All joints start and finish at the same time.
 *         Uses SOEM_SetInterpolatedTarget() to bypass SOEM ramp.
 *         Effective duration is scaled by the current speed override.
 * @param  j1..j6      Target joint angles [degrees].
 * @param  duration_s  Nominal move time [seconds] at 100 % override.  Must be > 0.
 */
uint8_t Interp_JointSync(float j1, float j2, float j3,
                          float j4, float j5, float j6,
                          float duration_s);

/**
 * @brief  Dwell — hold current position for the specified time.
 *         The interpolator enters MOVING state for @p ms milliseconds,
 *         then transitions to DONE.  No axis movement is generated.
 * @param  ms  Dwell time [milliseconds].  0 = instant done.
 */
uint8_t Interp_Dwell(uint32_t ms);

/**
 * @brief  Set global speed override percentage.
 *         Applied to JointSync duration: effective = nominal × (100 / pct).
 * @param  pct  1..200  (100 = normal speed, 200 = twice as fast).
 *              Values outside this range are clamped.
 */
void    Interp_SetSpeedOverride(uint8_t pct);

/**
 * @brief  Return current speed override percentage (1..200, default 100).
 */
uint8_t Interp_GetSpeedOverride(void);

#ifdef __cplusplus
}
#endif

#endif /* INTERPOLATOR_H */
