/**
 * @file    interpolator.c
 * @brief   6-axis articulated robot — joint-space motion interpolator
 *
 * Algorithms
 * ──────────
 *  JointRapid  Set final HW targets via SOEM_SetTargetHw() for all joints.
 *              SOEM ramp handles accel/decel per joint independently.
 *              Tick polls g_shadow[ax].target_reached for all active joints.
 *
 *  JointSync   Quintic (5th-order) polynomial trajectory (JTRAJ).
 *              Precompute start_deg[ax] and delta_deg[ax] for each joint.
 *              Single progress variable τ = ticks_done / ticks_total.
 *              s(τ) = 10τ³ − 15τ⁴ + 6τ⁵  ensures zero velocity and
 *              acceleration at both endpoints.
 *              Each tick: pos[ax] = start[ax] + delta[ax] × s(τ).
 *              Position written via SOEM_SetInterpolatedTarget() (bypasses
 *              SOEM ramp — interpolator owns the trajectory here).
 *
 * Thread safety
 * ─────────────
 *  DefaultTask  writes g_req then sets g_req_pending (with __DMB()).
 *  EtherCAT_Task reads g_req_pending in Interp_Tick(); copies g_req
 *  before clearing the flag.  A 32-bit volatile flag provides the
 *  necessary visibility; no RTOS primitive is needed.
 *
 *  post_cmd() force-idles the interpolator before posting so a new command
 *  always pre-empts any running move cleanly.
 */

#include "interpolator.h"
#include "stm32h7xx_hal.h"   /* __DMB() data memory barrier */
#include "soem_port.h"       /* SOEM_SetInterpolatedTarget, SOEM_SetTargetHw */
#include "robot_config.h"    /* ROBOT_TARGET_TOLERANCE_HW */

#include <math.h>            /* fabsf */
#include <string.h>          /* memset */

/* ── Constants ──────────────────────────────────────────────────────────── */

#define INTERP_MIN_DURATION_S   0.001f   /* guard against divide-by-zero  */

/* ── Internal command types ─────────────────────────────────────────────── */

typedef enum {
    ICMD_NONE         = 0,
    ICMD_JOINT_RAPID  = 1,
    ICMD_JOINT_SYNC   = 2,
    ICMD_STOP         = 3,
    ICMD_DWELL        = 4,
} ICmd_t;

/* ── Request packet (written by DefaultTask, read by EtherCAT_Task) ──────── */

typedef struct {
    ICmd_t   cmd;
    float    target_deg[AXIS_COUNT];   /* target joint angles [degrees] */
    float    duration_s;               /* JointSync only                */
    uint32_t dwell_ms;                 /* Dwell only                    */
} InterpReq_t;

/* ── Active JointSync state ──────────────────────────────────────────────── */

typedef struct {
    float    start_deg[AXIS_COUNT];
    float    delta_deg[AXIS_COUNT];
    float    target_deg[AXIS_COUNT];  /* exact end (for snap) */
    uint32_t ticks_total;
    uint32_t ticks_done;
} SyncState_t;

/* ── Module-level state ──────────────────────────────────────────────────── */

static volatile InterpState_t g_state      = INTERP_IDLE;
static volatile ICmd_t        g_active_cmd = ICMD_NONE;

static SyncState_t g_sync;
static uint32_t    g_dwell_remaining = 0U;

/* Speed override: 100 = normal, 200 = 2× speed, 50 = half speed. */
static volatile uint8_t g_speed_override = 100U;

/* Request from DefaultTask */
static InterpReq_t      g_req;
static volatile uint8_t g_req_pending = 0U;

/* ── Unit conversion (float) ─────────────────────────────────────────────── */

static inline float deg_to_hw_f(uint8_t ax, float deg)
{
    float scale  = (g_axis_param[ax].unit_scale > 0)
                   ? (float)g_axis_param[ax].unit_scale : 1.0f;
    float origin = (float)g_axis_param[ax].home_offset;
    return deg * scale + origin;
}

static inline float hw_to_deg_f(uint8_t ax, int32_t hw)
{
    float scale  = (g_axis_param[ax].unit_scale > 0)
                   ? (float)g_axis_param[ax].unit_scale : 1.0f;
    float origin = (float)g_axis_param[ax].home_offset;
    return ((float)hw - origin) / scale;
}

/* ── Quintic blend (JTRAJ) ───────────────────────────────────────────────── */
/* s(τ) = 10τ³ − 15τ⁴ + 6τ⁵,  τ ∈ [0,1]                                   */
static inline float quintic_s(float tau)
{
    float t2 = tau  * tau;
    float t3 = t2   * tau;
    float t4 = t3   * tau;
    float t5 = t4   * tau;
    return 10.0f * t3 - 15.0f * t4 + 6.0f * t5;
}

/* ── JointRapid planning / tick ──────────────────────────────────────────── */

static void rapid_plan(const InterpReq_t *req)
{
    for (uint8_t ax = 0U; ax < (uint8_t)AXIS_COUNT; ax++) {
        int32_t hw = (int32_t)deg_to_hw_f(ax, req->target_deg[ax]);
        SOEM_SetTargetHw((AxisId_t)ax, hw);
    }
    g_active_cmd = ICMD_JOINT_RAPID;
    g_state      = INTERP_MOVING;
}

static void rapid_tick(void)
{
    for (uint8_t ax = 0U; ax < (uint8_t)AXIS_COUNT; ax++) {
        if (g_shadow[ax].target_reached == 0U) return;  /* still moving */
    }
    g_state = INTERP_DONE;
}

/* ── JointSync planning / tick ───────────────────────────────────────────── */

static void sync_plan(const InterpReq_t *req)
{
    /* Scale duration by speed override: 200% → half time, 50% → double time */
    float duration = (req->duration_s > INTERP_MIN_DURATION_S)
                     ? req->duration_s : INTERP_MIN_DURATION_S;
    uint8_t ovr = (uint8_t)g_speed_override;
    if (ovr < 1U) ovr = 1U;
    duration = duration * 100.0f / (float)ovr;
    if (duration < INTERP_MIN_DURATION_S) duration = INTERP_MIN_DURATION_S;

    g_sync.ticks_total = (uint32_t)(duration * 1000.0f);
    if (g_sync.ticks_total < 1U) g_sync.ticks_total = 1U;
    g_sync.ticks_done = 0U;

    for (uint8_t ax = 0U; ax < (uint8_t)AXIS_COUNT; ax++) {
        g_sync.start_deg[ax]  = hw_to_deg_f(ax, (int32_t)g_shadow[ax].pos_hw);
        g_sync.target_deg[ax] = req->target_deg[ax];
        g_sync.delta_deg[ax]  = req->target_deg[ax] - g_sync.start_deg[ax];
    }

    g_active_cmd = ICMD_JOINT_SYNC;
    g_state      = INTERP_MOVING;
}

static void sync_tick(void)
{
    g_sync.ticks_done++;

    if (g_sync.ticks_done >= g_sync.ticks_total) {
        /* Snap to exact targets to eliminate float accumulation */
        for (uint8_t ax = 0U; ax < (uint8_t)AXIS_COUNT; ax++) {
            int32_t hw = (int32_t)deg_to_hw_f(ax, g_sync.target_deg[ax]);
            SOEM_SetInterpolatedTarget((AxisId_t)ax, hw);
        }
        g_state = INTERP_DONE;
        return;
    }

    float tau = (float)g_sync.ticks_done / (float)g_sync.ticks_total;
    float s   = quintic_s(tau);

    for (uint8_t ax = 0U; ax < (uint8_t)AXIS_COUNT; ax++) {
        float   deg = g_sync.start_deg[ax] + g_sync.delta_deg[ax] * s;
        int32_t hw  = (int32_t)deg_to_hw_f(ax, deg);
        SOEM_SetInterpolatedTarget((AxisId_t)ax, hw);
    }
}

/* ── Dwell planning / tick ───────────────────────────────────────────────── */

static void dwell_plan(const InterpReq_t *req)
{
    g_dwell_remaining = req->dwell_ms;
    g_active_cmd      = ICMD_DWELL;
    g_state           = (g_dwell_remaining > 0U) ? INTERP_MOVING : INTERP_DONE;
}

static void dwell_tick(void)
{
    if (g_dwell_remaining == 0U) {
        g_state = INTERP_DONE;
        return;
    }
    g_dwell_remaining--;
    if (g_dwell_remaining == 0U)
        g_state = INTERP_DONE;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void Interp_Init(void)
{
    memset(&g_sync, 0, sizeof(g_sync));
    memset(&g_req,  0, sizeof(g_req));
    g_req_pending     = 0U;
    g_state           = INTERP_IDLE;
    g_active_cmd      = ICMD_NONE;
    g_dwell_remaining = 0U;
    g_speed_override  = 100U;
}

void Interp_Tick(void)
{
    /* ── Latch pending request from DefaultTask ─────────────────────── */
    if (g_req_pending != 0U) {
        InterpReq_t req = g_req;   /* copy before clearing flag */
        g_req_pending   = 0U;

        if (req.cmd == ICMD_STOP) {
            g_state      = INTERP_IDLE;
            g_active_cmd = ICMD_NONE;
            return;
        }

        /* Accept new command only when not MOVING */
        if (g_state != INTERP_MOVING) {
            switch (req.cmd) {
                case ICMD_JOINT_RAPID: rapid_plan(&req); break;
                case ICMD_JOINT_SYNC:  sync_plan(&req);  break;
                case ICMD_DWELL:       dwell_plan(&req); break;
                default: break;
            }
            /* Skip advance on the same tick as the new move start.
             * target_reached flags are stale until SOEM_PeriodicPoll() runs. */
            return;
        }
    }

    /* ── Advance active move ────────────────────────────────────────── */
    if (g_state != INTERP_MOVING) return;

    switch (g_active_cmd) {
        case ICMD_JOINT_RAPID: rapid_tick(); break;
        case ICMD_JOINT_SYNC:  sync_tick();  break;
        case ICMD_DWELL:       dwell_tick(); break;
        default:
            g_state = INTERP_IDLE;
            break;
    }
}

InterpState_t Interp_GetState(void)
{
    return (InterpState_t)g_state;
}

uint8_t Interp_IsBusy(void)
{
    return (g_state == INTERP_MOVING) ? 1U : 0U;
}

void Interp_AckDone(void)
{
    if (g_state == INTERP_DONE) {
        g_state      = INTERP_IDLE;
        g_active_cmd = ICMD_NONE;
    }
}

void Interp_Stop(void)
{
    g_req.cmd     = ICMD_STOP;
    __DMB();
    g_req_pending = 1U;
}

/* ── Move command helpers ─────────────────────────────────────────────────── */

static uint8_t post_cmd(const InterpReq_t *req)
{
    /* Pre-empt any running move so new commands are never rejected. */
    g_state      = INTERP_IDLE;
    g_active_cmd = ICMD_NONE;
    g_req        = *req;
    __DMB();
    g_req_pending = 1U;
    return 1U;
}

uint8_t Interp_JointRapid(float j1, float j2, float j3,
                           float j4, float j5, float j6)
{
    InterpReq_t req;
    req.cmd           = ICMD_JOINT_RAPID;
    req.target_deg[0] = j1;
    req.target_deg[1] = j2;
    req.target_deg[2] = j3;
    req.target_deg[3] = j4;
    req.target_deg[4] = j5;
    req.target_deg[5] = j6;
    req.duration_s    = 0.0f;
    return post_cmd(&req);
}

uint8_t Interp_JointSync(float j1, float j2, float j3,
                          float j4, float j5, float j6,
                          float duration_s)
{
    if (duration_s <= 0.0f) return 0U;

    InterpReq_t req;
    req.cmd           = ICMD_JOINT_SYNC;
    req.target_deg[0] = j1;
    req.target_deg[1] = j2;
    req.target_deg[2] = j3;
    req.target_deg[3] = j4;
    req.target_deg[4] = j5;
    req.target_deg[5] = j6;
    req.duration_s    = duration_s;
    req.dwell_ms      = 0U;
    return post_cmd(&req);
}

uint8_t Interp_Dwell(uint32_t ms)
{
    InterpReq_t req;
    req.cmd        = ICMD_DWELL;
    req.dwell_ms   = ms;
    req.duration_s = 0.0f;
    memset(req.target_deg, 0, sizeof(req.target_deg));
    return post_cmd(&req);
}

void Interp_SetSpeedOverride(uint8_t pct)
{
    if (pct < 1U)   pct = 1U;
    if (pct > 200U) pct = 200U;
    g_speed_override = pct;
}

uint8_t Interp_GetSpeedOverride(void)
{
    return (uint8_t)g_speed_override;
}
