/**
 * @file    soem_port.c
 * @brief   AGV EtherCAT CSV master implementation (2-wheel differential drive)
 *
 * Architecture
 * ────────────
 *  EtherCAT_Task (1ms, Realtime)
 *    └─ SOEM_PeriodicPoll()
 *         ├─ ecx_send_processdata / ecx_receive_processdata
 *         └─ for each active axis:
 *              ├─ soem_cia402_step(ax)         CiA 402 FSM
 *              ├─ soem_apply_pending_sdo(ax)   SDO parameter writes
 *              ├─ soem_update_target_output(ax) ramp generator
 *              └─ soem_update_shadows(ax)       volatile g_shadow[]
 *
 *  DefaultTask (low priority)
 *    └─ SOEM_Set*(ax, …) APIs  → set pending flags / values
 *
 * Per-axis state is in g_rt[ax] (AxisRuntime_t).
 * Volatile read-only shadow is in g_shadow[ax] (axis_types.h).
 *
 * All SDO calls use ecx_SDOwrite/read with slave = ax + 1.
 */

#include "soem_port.h"

#ifdef SOEM_ENABLED

#include "soem/ec_main.h"
#include "soem/ec_type.h"
#include "axis_config.h"
#include "robot_config.h"
#include "stm32h7xx.h"
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ── Vendor SDO index defaults (override via compiler -D if drive differs) ── */
#ifndef SOEM_POSITION_GAIN1_INDEX
#define SOEM_POSITION_GAIN1_INDEX 0x2101U
#endif
#ifndef SOEM_POSITION_GAIN2_INDEX
#define SOEM_POSITION_GAIN2_INDEX 0x2105U
#endif

/* ── PDO structures ──────────────────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint16_t controlword;
    int32_t  target_velocity;   /* CSV: 0x60FF TargetVelocity [HW counts/s] */
} RxPDO_t;  /* Master → Slave (6 bytes) */

typedef struct __attribute__((packed)) {
    uint16_t statusword;
    int32_t  position_actual;
    int32_t  velocity_actual;
    int16_t  torque_actual;
} TxPDO_t;  /* Slave → Master (12 bytes) */

/* ── Per-axis runtime state ──────────────────────────────────────────────── */

typedef struct {
    /* PDO pointers (set after ec_config_map) */
    RxPDO_t  *rxpdo;
    TxPDO_t  *txpdo;

    /* Motion target */
    int32_t  target_hw;         /* position ramp destination (HW counts) — unused in CSV */
    int32_t  target_hw_out;     /* position ramp output — unused in CSV                  */
    volatile int32_t target_vel_hw;  /* CSV target velocity [HW counts/s]                */

    /* CiA 402 FSM */
    Cia402Stage_t cia402_stage;
    uint16_t cia402_hold_cnt;
    uint16_t cia402_timeout_cnt;
    uint8_t  fault_active;
    uint16_t last_controlword;
    uint16_t last_statusword;
    uint8_t  last_cia402_state;

    /* Run enable gate */
    volatile uint8_t run_enable;

    /* Motion profile (with SDO pending flags) */
    volatile int32_t  ramp_velocity;              /* local ramp, no SDO   */
    volatile int32_t  profile_velocity;
    volatile uint8_t  profile_velocity_pending;
    volatile int32_t  profile_accel_ms;
    volatile uint8_t  profile_accel_pending;
    volatile int32_t  profile_decel_ms;
    volatile uint8_t  profile_decel_pending;
    volatile uint16_t torque_limit;
    volatile uint8_t  torque_limit_pending;

    /* Unit scaling */
    volatile uint8_t  unit_scale_pending;

    /* Home offset */
    volatile uint8_t  home_offset_pending;

    /* Software limits */
    volatile uint8_t  limits_pending;

    /* Position gain */
    volatile int32_t  position_gain;
    volatile uint8_t  position_gain_pending;
    volatile uint8_t  position_gain_read_pending;
    volatile uint8_t  position_gain_read_done;
    volatile int32_t  position_gain_readback;
    volatile int32_t  position_gain_read_status;

    /* Parameter bulk read */
    volatile uint8_t  param_read_pending;
    volatile uint8_t  param_read_done;
    volatile int32_t  param_read_values[SOEM_PARAM_READ_COUNT];

    /* SDO stability gate counter */
    uint16_t stable_cycles;

    /* Bypass flag — suppresses soft-limit clamp when target is set directly */
    volatile uint8_t interp_active;

    /* External fault-reset request (set by DefaultTask, cleared by EtherCAT_Task) */
    volatile uint8_t fault_reset_pending;
} AxisRuntime_t;

/* ── Module globals ──────────────────────────────────────────────────────── */

ecx_contextt soem_context;

/* IOmap in non-cacheable AXI SRAM (ETH DMA coherency, MPU Region 0). */
static uint8_t soem_iomap[4096] __attribute__((section(".eth_dma"), aligned(32)));
static uint8_t soem_group = 0;

static uint8_t g_initialized  = 0U;
static uint8_t g_configured   = 0U;
static uint8_t g_active_axes  = 0U;  /* how many slaves found */

static AxisRuntime_t g_rt[AXIS_COUNT];

static void (*soem_log_fn)(const char *msg) = NULL;

/* ── Logging helpers ─────────────────────────────────────────────────────── */

static void soem_log(const char *msg)
{
    if (soem_log_fn == NULL) return;
    size_t n = strlen(msg);
    if ((n > 0U) && (msg[n - 1U] == '\n' || msg[n - 1U] == '\r')) {
        soem_log_fn(msg);
    } else {
        char buf[200];
        (void)snprintf(buf, sizeof(buf), "%s\r\n", msg);
        soem_log_fn(buf);
    }
}

static void soem_logf(const char *fmt, ...)
{
    if (soem_log_fn == NULL) return;
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    soem_log(buf);
}

/* ── SDO helpers (slave index = ax + 1) ─────────────────────────────────── */

static int ax_sdo_wr16(uint8_t ax, uint16_t idx, uint8_t sub, uint16_t val)
{
    return ecx_SDOwrite(&soem_context, ROBOT_SLAVE_IDX(ax),
                        idx, sub, FALSE, sizeof(val), &val, EC_TIMEOUTRXM);
}

static int ax_sdo_wr32(uint8_t ax, uint16_t idx, uint8_t sub, uint32_t val)
{
    return ecx_SDOwrite(&soem_context, ROBOT_SLAVE_IDX(ax),
                        idx, sub, FALSE, sizeof(val), &val, EC_TIMEOUTRXM);
}

static int ax_sdo_wri32(uint8_t ax, uint16_t idx, uint8_t sub, int32_t val)
{
    return ecx_SDOwrite(&soem_context, ROBOT_SLAVE_IDX(ax),
                        idx, sub, FALSE, sizeof(val), &val, EC_TIMEOUTRXM);
}

static int ax_sdo_rd16(uint8_t ax, uint16_t idx, uint8_t sub, uint16_t *out)
{
    int sz = (int)sizeof(*out);
    return ecx_SDOread(&soem_context, ROBOT_SLAVE_IDX(ax),
                       idx, sub, FALSE, &sz, out, EC_TIMEOUTRXM);
}

static int ax_sdo_rd32(uint8_t ax, uint16_t idx, uint8_t sub, uint32_t *out)
{
    int sz = (int)sizeof(*out);
    return ecx_SDOread(&soem_context, ROBOT_SLAVE_IDX(ax),
                       idx, sub, FALSE, &sz, out, EC_TIMEOUTRXM);
}

static int ax_sdo_rdi32(uint8_t ax, uint16_t idx, uint8_t sub, int32_t *out)
{
    int sz = (int)sizeof(*out);
    return ecx_SDOread(&soem_context, ROBOT_SLAVE_IDX(ax),
                       idx, sub, FALSE, &sz, out, EC_TIMEOUTRXM);
}

/* Position gain: try u32 first, fall back to u16. */
static int ax_write_gain(uint8_t ax, uint16_t idx, int32_t gain)
{
    uint32_t v32 = (uint32_t)gain;
    int wkc = ecx_SDOwrite(&soem_context, ROBOT_SLAVE_IDX(ax),
                            idx, 0, FALSE, sizeof(v32), &v32, EC_TIMEOUTRXM);
    if (wkc > 0) return wkc;
    uint16_t v16 = (gain > 65535) ? 65535U : (uint16_t)gain;
    return ecx_SDOwrite(&soem_context, ROBOT_SLAVE_IDX(ax),
                        idx, 0, FALSE, sizeof(v16), &v16, EC_TIMEOUTRXM);
}

static int ax_read_gain(uint8_t ax, uint16_t idx, int32_t *out)
{
    uint32_t v32 = 0;
    int sz = (int)sizeof(v32);
    int wkc = ecx_SDOread(&soem_context, ROBOT_SLAVE_IDX(ax),
                           idx, 0, FALSE, &sz, &v32, EC_TIMEOUTRXM);
    if (wkc > 0) {
        *out = (sz <= (int)sizeof(uint16_t))
               ? (int32_t)(uint16_t)v32 : (int32_t)v32;
        return wkc;
    }
    uint16_t v16 = 0;
    sz = (int)sizeof(v16);
    wkc = ecx_SDOread(&soem_context, ROBOT_SLAVE_IDX(ax),
                       idx, 0, FALSE, &sz, &v16, EC_TIMEOUTRXM);
    if (wkc > 0) *out = (int32_t)v16;
    return wkc;
}

/* ── CiA 402 state decoder ───────────────────────────────────────────────── */

static Cia402State_t decode_cia402(uint16_t sw)
{
    switch (sw & 0x006FU) {
        case 0x0000U: return CIA402_STATE_NOT_READY;
        case 0x0040U: return CIA402_STATE_SW_DISABLED;
        case 0x0021U: return CIA402_STATE_READY;
        case 0x0023U: return CIA402_STATE_SWITCHED_ON;
        case 0x0027U: return CIA402_STATE_OP_ENABLED;
        case 0x0007U: return CIA402_STATE_QUICK_STOP;
        case 0x000FU: return CIA402_STATE_FAULT_REACT;
        case 0x0008U: return CIA402_STATE_FAULT;
        default:      return CIA402_STATE_UNKNOWN;
    }
}

static void log_cia402_state(uint8_t ax, Cia402State_t st)
{
    static const char *names[] = {
        "NotReady", "SwDisabled", "Ready", "SwitchedOn",
        "OpEnabled", "QuickStop", "FaultReact", "Fault", "Unknown"
    };
    uint8_t idx = (st <= CIA402_STATE_FAULT) ? (uint8_t)st : 8U;
    soem_logf("[Ax%s] CIA402: %s", robot_axis_name(ax), names[idx]);
}

/* ── Soft-limit helpers ──────────────────────────────────────────────────── */

static void refresh_hw_limits(uint8_t ax)
{
    AxisParam_t *p = &g_axis_param[ax];

    /* Disable only when neither limit has been configured (both at default 0). */
    if (p->limit_plus_user == 0 && p->limit_minus_user == 0) {
        p->limit_plus_hw  = INT32_MAX;
        p->limit_minus_hw = INT32_MIN;
        p->limits_enabled = 0U;
        p->limits_blocked = 0U;
        return;
    }

    /* Always activate limits once any non-zero value is configured.
     * limit_hw values are already computed by the calling Set* function.
     * No "outside" trap: limits must not be silently disabled just because
     * the axis is currently outside the intended range.                   */
    p->limits_enabled = 1U;
    p->limits_blocked = 0U;
}

/* ── Ramp generator ──────────────────────────────────────────────────────── */

static int32_t step_per_cycle(uint8_t ax)
{
    /* velocity source: ramp_velocity overrides profile_velocity for JOG */
    int32_t vel = g_rt[ax].ramp_velocity;
    if (vel == 0) vel = g_rt[ax].profile_velocity;
    if (vel < 0)  vel = -vel;
    if (vel <= 0) vel = 1;

    int32_t scale = (g_axis_param[ax].unit_scale > 0)
                    ? g_axis_param[ax].unit_scale : 1;
    int64_t step = ((int64_t)vel * (int64_t)scale) / 1000LL; /* /1000 = per ms */
    if (step < 1LL)           step = 1LL;
    if (step > (int64_t)INT32_MAX) step = (int64_t)INT32_MAX;
    return (int32_t)step;
}

static void soem_update_target_output(uint8_t ax)
{
    int32_t cmd    = g_rt[ax].target_hw;
    int32_t output = g_rt[ax].target_hw_out;
    int32_t actual = (int32_t)g_shadow[ax].pos_hw;

    int64_t err_actual = (int64_t)cmd - (int64_t)actual;
    if (err_actual < 0) err_actual = -err_actual;

    if (err_actual <= (int64_t)ROBOT_TARGET_TOLERANCE_HW) {
        /* Skip soft-limit clamp when target was set via direct bypass */
        g_rt[ax].target_hw_out = g_rt[ax].interp_active
                                 ? cmd
                                 : axis_clamp_hw(ax, cmd);
        g_shadow[ax].target_reached = 1U;
        return;
    }

    g_shadow[ax].target_reached = 0U;

    if (output == cmd) return;

    const int32_t step = step_per_cycle(ax);
    const int64_t diff = (int64_t)cmd - (int64_t)output;

    if      (diff >  (int64_t)step) output += step;
    else if (diff < -(int64_t)step) output -= step;
    else                             output  = cmd;

    g_rt[ax].target_hw_out = axis_clamp_hw(ax, output);
}

/* ── SDO stability gate ──────────────────────────────────────────────────── */

static uint8_t allow_sdo(uint8_t ax)
{
    if (g_rt[ax].cia402_stage != CIA402_STAGE_OP_ENABLED) {
        g_rt[ax].stable_cycles = 0U;
        return 1U;  /* allow SDO during init/config phases */
    }

    int32_t vel = (int32_t)g_shadow[ax].velocity;
    if (vel < 0) vel = -vel;
    int64_t pos_err = (int64_t)g_rt[ax].target_hw_out
                      - (int64_t)g_shadow[ax].pos_hw;
    if (pos_err < 0) pos_err = -pos_err;

    if ((vel <= 1) && (pos_err <= (int64_t)ROBOT_TARGET_TOLERANCE_HW)) {
        if (g_rt[ax].stable_cycles < ROBOT_SDO_STABLE_CYCLES_MIN) {
            g_rt[ax].stable_cycles++;
            return 0U;
        }
        return 1U;
    }

    g_rt[ax].stable_cycles = 0U;
    return 0U;
}

/* ── Pending SDO writer ──────────────────────────────────────────────────── */

static void soem_apply_pending_sdo(uint8_t ax)
{
    uint8_t ok = allow_sdo(ax);

    /* Profile velocity → 0x6081 */
    if (ok && g_rt[ax].profile_velocity_pending) {
        uint32_t v = (uint32_t)((g_rt[ax].profile_velocity > 0)
                                ? g_rt[ax].profile_velocity : 1);
        if (ax_sdo_wr32(ax, 0x6081, 0, v) > 0)
            soem_logf("[Ax%s] vel=%lu", robot_axis_name(ax), (unsigned long)v);
        g_rt[ax].profile_velocity_pending = 0U;
    }

    /* Torque limit → 0x6072 */
    if (ok && g_rt[ax].torque_limit_pending) {
        uint16_t t = g_rt[ax].torque_limit;
        if (ax_sdo_wr16(ax, 0x6072, 0, t) > 0)
            soem_logf("[Ax%s] torque=%u", robot_axis_name(ax), t);
        g_rt[ax].torque_limit_pending = 0U;
    }

    /* Acceleration → 0x2301 (vendor-specific, u16) */
    if (ok && g_rt[ax].profile_accel_pending) {
        uint16_t a = (uint16_t)g_rt[ax].profile_accel_ms;
        uint16_t readback = 0U;
        if (ax_sdo_wr16(ax, 0x2301, 0, a) > 0) {
            (void)ax_sdo_rd16(ax, 0x2301, 0, &readback);
            g_rt[ax].profile_accel_ms = (int32_t)readback;
            soem_logf("[Ax%s] accel=%u", robot_axis_name(ax), readback);
        }
        g_rt[ax].profile_accel_pending = 0U;
    }

    /* Deceleration → 0x2302 */
    if (ok && g_rt[ax].profile_decel_pending) {
        uint16_t d = (uint16_t)g_rt[ax].profile_decel_ms;
        uint16_t readback = 0U;
        if (ax_sdo_wr16(ax, 0x2302, 0, d) > 0) {
            (void)ax_sdo_rd16(ax, 0x2302, 0, &readback);
            g_rt[ax].profile_decel_ms = (int32_t)readback;
            soem_logf("[Ax%s] decel=%u", robot_axis_name(ax), readback);
        }
        g_rt[ax].profile_decel_pending = 0U;
    }

    /* Software limits → 0x607D sub1 (min), sub2 (max), user units */
    if (ok && g_rt[ax].limits_pending) {
        AxisParam_t *p = &g_axis_param[ax];
        bool inv = (p->limit_plus_user <= p->limit_minus_user)
                || (p->limit_plus_user == 0 && p->limit_minus_user == 0);
        if (!inv) {
            (void)ax_sdo_wri32(ax, 0x607D, 1, p->limit_minus_user);
            (void)ax_sdo_wri32(ax, 0x607D, 2, p->limit_plus_user);
            soem_logf("[Ax%s] limits=[%ld,%ld]", robot_axis_name(ax),
                      (long)p->limit_minus_user, (long)p->limit_plus_user);
        }
        g_rt[ax].limits_pending = 0U;
    }

    /* Unit scale → 0x6092 sub1 (feed constant), sub2=1 */
    if (ok && g_rt[ax].unit_scale_pending) {
        int32_t sc = g_axis_param[ax].unit_scale;
        uint32_t feed = (uint32_t)((sc > 0) ? sc : 1);
        uint32_t revs = 1U;
        if (ax_sdo_wr32(ax, 0x6092, 1, feed) > 0 &&
            ax_sdo_wr32(ax, 0x6092, 2, revs) > 0)
            soem_logf("[Ax%s] unit_scale=%lu", robot_axis_name(ax), (unsigned long)feed);
        g_rt[ax].unit_scale_pending = 0U;
    }

    /* Home offset → 0x607C */
    if (ok && g_rt[ax].home_offset_pending) {
        int32_t ho = g_axis_param[ax].home_offset;
        if (ax_sdo_wri32(ax, 0x607C, 0, ho) > 0)
            soem_logf("[Ax%s] home_offset=%ld", robot_axis_name(ax), (long)ho);
        g_rt[ax].home_offset_pending = 0U;
    }

    /* Position gain → 0x2101 then 0x2105 */
    if (ok && g_rt[ax].position_gain_pending) {
        int32_t gain = g_rt[ax].position_gain;
        if (gain > 0) {
            int wkc1 = ax_write_gain(ax, SOEM_POSITION_GAIN1_INDEX, gain);
            int wkc2 = ax_write_gain(ax, SOEM_POSITION_GAIN2_INDEX, gain);
            soem_logf("[Ax%s] pos_gain=%ld [2101:%s 2105:%s]",
                      robot_axis_name(ax), (long)gain,
                      wkc1 > 0 ? "ok" : "fail", wkc2 > 0 ? "ok" : "fail");
        }
        g_rt[ax].position_gain_pending = 0U;
    }

    /* Position gain read */
    if (g_rt[ax].position_gain_read_pending) {
        int32_t gain = 0;
        int wkc = ax_read_gain(ax, SOEM_POSITION_GAIN1_INDEX, &gain);
        if (wkc <= 0) wkc = ax_read_gain(ax, SOEM_POSITION_GAIN2_INDEX, &gain);
        g_rt[ax].position_gain_read_pending = 0U;
        if (wkc > 0) {
            g_rt[ax].position_gain_readback   = gain;
            g_rt[ax].position_gain            = gain;
            g_rt[ax].position_gain_read_status = 20;
            g_rt[ax].position_gain_read_done   = 1U;
            soem_logf("[Ax%s] gain_read=%ld", robot_axis_name(ax), (long)gain);
        } else {
            g_rt[ax].position_gain_read_status = -30;
        }
    }

    /* Parameter bulk read */
    if (g_rt[ax].param_read_pending) {
        uint16_t acc = 0, dec = 0;
        uint32_t feed = 1, revs = 1;
        int32_t  lim_min = 0, lim_max = 0, gain = 0;

        int okA  = ax_sdo_rd16(ax, 0x2301, 0, &acc);
        int okD  = ax_sdo_rd16(ax, 0x2302, 0, &dec);
        int okF  = ax_sdo_rd32(ax, 0x6092, 1, &feed);
        int okR  = ax_sdo_rd32(ax, 0x6092, 2, &revs);
        int okLn = ax_sdo_rdi32(ax, 0x607D, 1, &lim_min);
        int okLp = ax_sdo_rdi32(ax, 0x607D, 2, &lim_max);
        int okG  = ax_read_gain(ax, SOEM_POSITION_GAIN1_INDEX, &gain);
        if (okG <= 0) okG = ax_read_gain(ax, SOEM_POSITION_GAIN2_INDEX, &gain);

        g_rt[ax].param_read_pending = 0U;

        if (okA > 0 && okD > 0 && okF > 0 && okLn > 0 && okLp > 0 && okG > 0) {
            int32_t scale = (okR > 0 && revs > 0U)
                            ? (int32_t)(feed / revs) : (int32_t)feed;
            if (scale <= 0) scale = 1;

            volatile int32_t *v = g_rt[ax].param_read_values;
            v[0] = 0;               /* jog speed placeholder */
            v[1] = (int32_t)acc;
            v[2] = (int32_t)dec;
            v[3] = lim_max;
            v[4] = lim_min;
            v[5] = scale;
            v[6] = g_axis_param[ax].home_offset / scale;
            v[7] = gain;
            g_rt[ax].param_read_done = 1U;
            soem_logf("[Ax%s] param_read ok", robot_axis_name(ax));
        } else {
            soem_logf("[Ax%s] param_read failed", robot_axis_name(ax));
        }
    }
}

/* ── CiA 402 FSM (per axis) ──────────────────────────────────────────────── */

static void log_fault_diag(uint8_t ax, uint16_t sw)
{
    uint16_t fc = 0; uint8_t fr = 0;
    (void)ax_sdo_rd16(ax, 0x603F, 0, &fc);
    ecx_SDOread(&soem_context, ROBOT_SLAVE_IDX(ax), 0x1001, 0, FALSE,
                (int[]){1}, &fr, EC_TIMEOUTRXM);
    soem_logf("[Ax%s] FAULT sw=0x%04X code=0x%04X reg=0x%02X pos=%ld",
              robot_axis_name(ax), sw, fc, fr,
              (long)g_shadow[ax].pos_hw);
}

static void latch_target_to_actual(uint8_t ax)
{
    int32_t actual = (int32_t)g_shadow[ax].pos_hw;
    g_rt[ax].interp_active  = 0U;
    g_rt[ax].target_hw      = axis_clamp_hw(ax, actual);
    g_rt[ax].target_hw_out  = g_rt[ax].target_hw;
    g_rt[ax].target_vel_hw  = 0;   /* CSV: stop wheel on any latch */
}

static void soem_cia402_step(uint8_t ax)
{
    if (g_rt[ax].rxpdo == NULL || g_rt[ax].txpdo == NULL) return;

    uint16_t sw  = g_rt[ax].txpdo->statusword;
    uint16_t sw_log = (uint16_t)(sw & (uint16_t)~0x0400U); /* ignore volatile TargetReached bit */
    uint16_t cw  = g_rt[ax].last_controlword;

    /* Log statusword transitions */
    if (sw_log != g_rt[ax].last_statusword) {
        g_rt[ax].last_statusword = sw_log;
        soem_logf("[Ax%s] SW=0x%04X", robot_axis_name(ax), sw);
        Cia402State_t st = decode_cia402(sw);
        if ((uint8_t)st != g_rt[ax].last_cia402_state) {
            g_rt[ax].last_cia402_state = (uint8_t)st;
            log_cia402_state(ax, st);
        }
        g_shadow[ax].cia402_state = (uint8_t)decode_cia402(sw);
    }

    /* Wait for first valid (non-zero) statusword */
    if (sw == 0U) {
        g_rt[ax].rxpdo->controlword      = 0U;
        g_rt[ax].rxpdo->target_velocity  = 0;
        return;
    }

    /* External fault-reset: clear fault state and restart enable sequence */
    if (g_rt[ax].fault_reset_pending) {
        g_rt[ax].fault_reset_pending = 0U;
        g_rt[ax].fault_active        = 0U;
        latch_target_to_actual(ax);
        g_rt[ax].cia402_stage       = CIA402_STAGE_SEND_SHUTDOWN;
        g_rt[ax].cia402_hold_cnt    = 0U;
        g_rt[ax].cia402_timeout_cnt = ROBOT_CIA402_TIMEOUT_CYCLES;
        soem_logf("[Ax%s] FaultReset", robot_axis_name(ax));
    }

    /* RUN gate: run_enable=0 → hold at Shutdown, never advance to OP_ENABLED */
    if (g_rt[ax].run_enable == 0U) {
        cw = 0x0006U;
        g_rt[ax].target_vel_hw      = 0;
        g_rt[ax].cia402_stage       = CIA402_STAGE_WAIT_READY;
        g_rt[ax].cia402_hold_cnt    = 0U;
        g_rt[ax].cia402_timeout_cnt = ROBOT_CIA402_TIMEOUT_CYCLES;
        goto write_pdo;
    }

    /* Fault recovery (independent per axis) */
    if ((sw & 0x0008U) != 0U) {
        if (!g_rt[ax].fault_active) {
            g_rt[ax].fault_active = 1U;
            log_fault_diag(ax, sw);
        }
        latch_target_to_actual(ax);
        cw = 0x0080U;
        g_rt[ax].cia402_stage       = CIA402_STAGE_SEND_SHUTDOWN;
        g_rt[ax].cia402_hold_cnt    = 0U;
        g_rt[ax].cia402_timeout_cnt = 0U;
        goto write_pdo;
    }

    g_rt[ax].fault_active = 0U;

    switch (g_rt[ax].cia402_stage) {
        case CIA402_STAGE_SEND_SHUTDOWN:
            cw = 0x0006U;
            g_rt[ax].cia402_hold_cnt    = ROBOT_CIA402_HOLD_CYCLES;
            g_rt[ax].cia402_timeout_cnt = ROBOT_CIA402_TIMEOUT_CYCLES;
            g_rt[ax].cia402_stage       = CIA402_STAGE_WAIT_READY;
            soem_logf("[Ax%s] Shutdown(0x0006)", robot_axis_name(ax));
            break;

        case CIA402_STAGE_WAIT_READY:
            cw = 0x0006U;
            if (g_rt[ax].cia402_hold_cnt > 0U) {
                g_rt[ax].cia402_hold_cnt--;
            } else if ((sw & 0x006FU) == 0x0021U) {
                g_rt[ax].cia402_stage = CIA402_STAGE_SEND_SWITCH_ON;
            } else if (g_rt[ax].cia402_timeout_cnt > 0U) {
                g_rt[ax].cia402_timeout_cnt--;
            } else {
                soem_logf("[Ax%s] timeout waiting Ready", robot_axis_name(ax));
                g_rt[ax].cia402_stage = CIA402_STAGE_SEND_SHUTDOWN;
            }
            break;

        case CIA402_STAGE_SEND_SWITCH_ON:
            cw = 0x0007U;
            g_rt[ax].cia402_hold_cnt    = ROBOT_CIA402_HOLD_CYCLES;
            g_rt[ax].cia402_timeout_cnt = ROBOT_CIA402_TIMEOUT_CYCLES;
            g_rt[ax].cia402_stage       = CIA402_STAGE_WAIT_SWITCHED_ON;
            soem_logf("[Ax%s] SwitchOn(0x0007)", robot_axis_name(ax));
            break;

        case CIA402_STAGE_WAIT_SWITCHED_ON:
            cw = 0x0007U;
            if (g_rt[ax].cia402_hold_cnt > 0U) {
                g_rt[ax].cia402_hold_cnt--;
            } else if ((sw & 0x006FU) == 0x0023U) {
                g_rt[ax].cia402_stage = CIA402_STAGE_SEND_ENABLE_OP;
            } else if (g_rt[ax].cia402_timeout_cnt > 0U) {
                g_rt[ax].cia402_timeout_cnt--;
            } else {
                soem_logf("[Ax%s] timeout waiting SwitchedOn", robot_axis_name(ax));
                g_rt[ax].cia402_stage = CIA402_STAGE_SEND_SWITCH_ON;
            }
            break;

        case CIA402_STAGE_SEND_ENABLE_OP:
            latch_target_to_actual(ax);  /* prevent following error on first PDO */
            cw = 0x000FU;
            g_rt[ax].cia402_hold_cnt    = ROBOT_CIA402_HOLD_CYCLES;
            g_rt[ax].cia402_timeout_cnt = ROBOT_CIA402_TIMEOUT_CYCLES;
            g_rt[ax].cia402_stage       = CIA402_STAGE_WAIT_OP_ENABLED;
            soem_logf("[Ax%s] EnableOp(0x000F)", robot_axis_name(ax));
            break;

        case CIA402_STAGE_WAIT_OP_ENABLED:
            cw = 0x000FU;
            if (g_rt[ax].cia402_hold_cnt > 0U) {
                g_rt[ax].cia402_hold_cnt--;
            } else if ((sw & 0x006FU) == 0x0027U) {
                g_rt[ax].cia402_stage = CIA402_STAGE_OP_ENABLED;
                soem_logf("[Ax%s] OP_ENABLED", robot_axis_name(ax));
            } else if (g_rt[ax].cia402_timeout_cnt > 0U) {
                g_rt[ax].cia402_timeout_cnt--;
            } else {
                soem_logf("[Ax%s] timeout waiting OpEnabled", robot_axis_name(ax));
                g_rt[ax].cia402_stage = CIA402_STAGE_SEND_ENABLE_OP;
            }
            break;

        case CIA402_STAGE_OP_ENABLED:
        default:
            cw = 0x000FU;
            break;
    }

write_pdo:
    if (cw != g_rt[ax].last_controlword) {
        g_rt[ax].last_controlword = cw;
        soem_logf("[Ax%s] CW=0x%04X", robot_axis_name(ax), cw);
    }
    g_rt[ax].rxpdo->controlword     = cw;
    g_rt[ax].rxpdo->target_velocity = g_rt[ax].target_vel_hw;
}

/* ── Shadow update (called after receive_processdata) ────────────────────── */

static void soem_update_shadows(uint8_t ax)
{
    if (g_rt[ax].txpdo == NULL) return;
    int32_t hw = g_rt[ax].txpdo->position_actual;
    g_shadow[ax].pos_hw     = hw;
    g_shadow[ax].pos_centi  = axis_hw_to_centi(ax, hw);
    g_shadow[ax].velocity   = g_rt[ax].txpdo->velocity_actual;
    g_shadow[ax].torque     = g_rt[ax].txpdo->torque_actual;
    g_shadow[ax].statusword = g_rt[ax].txpdo->statusword;
    g_shadow[ax].pdo_ready  = 1U;
    g_shadow[ax].run_enable = g_rt[ax].run_enable;
}

/* ── PDO mapping (applied once per slave in PRE_OP) ──────────────────────── */

static int apply_pdo_mapping(uint8_t ax)
{
    uint16_t sl = ROBOT_SLAVE_IDX(ax);
    uint8_t  u8;
    uint16_t u16;
    uint32_t u32;
    int wkc;
    char msg[80];

    /* Clear SM2/SM3 PDO assignment lists */
    u8 = 0;
    wkc  = ecx_SDOwrite(&soem_context, sl, 0x1C12, 0, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);
    wkc += ecx_SDOwrite(&soem_context, sl, 0x1C13, 0, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);
    if (wkc < 2) {
        soem_logf("[Ax%s] PDO SM clear fail", robot_axis_name(ax));
        return 0;
    }
    for (volatile int d = 0; d < 500000; d++) {}

    /* Step 1: Check CSV (mode=9) support via 0x6502 Supported Drive Modes */
    {
        uint32_t sup = 0;
        int ckwkc = ax_sdo_rd32(ax, 0x6502, 0, &sup);
        if (ckwkc > 0) {
            soem_logf("[Ax%s] SupportedModes=0x%08lX CSV=%s PV=%s CSP=%s",
                      robot_axis_name(ax), (unsigned long)sup,
                      (sup & (1U<<8)) ? "yes" : "NO",
                      (sup & (1U<<2)) ? "yes" : "no",
                      (sup & (1U<<7)) ? "yes" : "no");
            if (!(sup & (1U<<8))) {
                soem_logf("[Ax%s] WARNING: drive may not support CSV(mode=9)!", robot_axis_name(ax));
            }
        } else {
            soem_logf("[Ax%s] 0x6502 read failed — CSV support unknown", robot_axis_name(ax));
        }
    }

    /* RxPDO 0x1600: Controlword(16) + TargetVelocity(32) = 6 bytes [CSV] */
    u8 = 0;
    ecx_SDOwrite(&soem_context, sl, 0x1600, 0, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);
    u32 = 0x60400010U; ecx_SDOwrite(&soem_context, sl, 0x1600, 1, FALSE, sizeof(u32), &u32, EC_TIMEOUTRXM);
    u32 = 0x60FF0020U; ecx_SDOwrite(&soem_context, sl, 0x1600, 2, FALSE, sizeof(u32), &u32, EC_TIMEOUTRXM);
    u8 = 2;
    wkc = ecx_SDOwrite(&soem_context, sl, 0x1600, 0, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);
    if (wkc <= 0) { soem_logf("[Ax%s] RxPDO 0x1600 fail", robot_axis_name(ax)); return 0; }

    /* TxPDO 0x1A00: Statusword(16) + PosActual(32) + VelActual(32) + Torque(16) = 12 bytes */
    u8 = 0;
    ecx_SDOwrite(&soem_context, sl, 0x1A00, 0, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);
    u32 = 0x60410010U; ecx_SDOwrite(&soem_context, sl, 0x1A00, 1, FALSE, sizeof(u32), &u32, EC_TIMEOUTRXM);
    u32 = 0x60640020U; ecx_SDOwrite(&soem_context, sl, 0x1A00, 2, FALSE, sizeof(u32), &u32, EC_TIMEOUTRXM);
    u32 = 0x606C0020U; ecx_SDOwrite(&soem_context, sl, 0x1A00, 3, FALSE, sizeof(u32), &u32, EC_TIMEOUTRXM);
    u32 = 0x60770010U; ecx_SDOwrite(&soem_context, sl, 0x1A00, 4, FALSE, sizeof(u32), &u32, EC_TIMEOUTRXM);
    u8 = 4;
    wkc = ecx_SDOwrite(&soem_context, sl, 0x1A00, 0, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);
    if (wkc <= 0) { soem_logf("[Ax%s] TxPDO 0x1A00 fail", robot_axis_name(ax)); return 0; }

    /* Assign PDOs to SM2/SM3 */
    u16 = 0x1600U; ecx_SDOwrite(&soem_context, sl, 0x1C12, 1, FALSE, sizeof(u16), &u16, EC_TIMEOUTRXM);
    u16 = 0x1A00U; ecx_SDOwrite(&soem_context, sl, 0x1C13, 1, FALSE, sizeof(u16), &u16, EC_TIMEOUTRXM);
    for (volatile int d = 0; d < 500000; d++) {}
    u8 = 1;
    ecx_SDOwrite(&soem_context, sl, 0x1C12, 0, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);
    ecx_SDOwrite(&soem_context, sl, 0x1C13, 0, FALSE, sizeof(u8), &u8, EC_TIMEOUTRXM);

    /* Set CSV mode (Cyclic Synchronous Velocity) */
    int8_t mode = 9;
    ecx_SDOwrite(&soem_context, sl, 0x6060, 0, FALSE, sizeof(mode), &mode, EC_TIMEOUTRXM);

    (void)snprintf(msg, sizeof(msg), "[Ax%s] PDO mapping ok (Rx=6B Tx=12B CSV)", robot_axis_name(ax));
    soem_log(msg);
    return 1;
}

/* ── Bind PDO pointers after ec_config_map_group ─────────────────────────── */

static void bind_pdo_pointers(void)
{
    for (uint8_t ax = 0; ax < g_active_axes; ax++) {
        uint16_t sl = ROBOT_SLAVE_IDX(ax);
        ec_slavet *s = &soem_context.slavelist[sl];

        if (s->Obytes >= sizeof(RxPDO_t) && s->outputs != NULL)
            g_rt[ax].rxpdo = (RxPDO_t *)s->outputs;
        else
            g_rt[ax].rxpdo = NULL;

        if (s->Ibytes >= sizeof(TxPDO_t) && s->inputs != NULL)
            g_rt[ax].txpdo = (TxPDO_t *)s->inputs;
        else
            g_rt[ax].txpdo = NULL;

        soem_logf("[Ax%s] PDO ptrs rx=%s tx=%s O=%luB I=%luB",
                  robot_axis_name(ax),
                  g_rt[ax].rxpdo ? "ok" : "NULL",
                  g_rt[ax].txpdo ? "ok" : "NULL",
                  (unsigned long)s->Obytes,
                  (unsigned long)s->Ibytes);
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Public API — lifecycle
 * ══════════════════════════════════════════════════════════════════════════ */

void SOEM_PortSetLog(void (*fn)(const char *msg)) { soem_log_fn = fn; }

void SOEM_PortInit(void)
{
    if (g_initialized) return;
    memset(g_rt, 0, sizeof(g_rt));
    for (uint8_t ax = 0; ax < AXIS_COUNT; ax++) {
        g_rt[ax].cia402_stage      = CIA402_STAGE_SEND_SHUTDOWN;
        g_rt[ax].last_controlword  = 0xFFFFU;
        g_rt[ax].last_statusword   = 0xFFFFU;
        g_rt[ax].last_cia402_state = 0xFFU;
        g_rt[ax].ramp_velocity     = g_axis_param[ax].profile_velocity;
        g_rt[ax].profile_velocity  = g_axis_param[ax].profile_velocity;
        g_rt[ax].profile_accel_ms  = g_axis_param[ax].profile_accel_ms;
        g_rt[ax].profile_decel_ms  = g_axis_param[ax].profile_decel_ms;
        g_rt[ax].torque_limit      = g_axis_param[ax].torque_limit;
    }
    if (ecx_init(&soem_context, SOEM_IFNAME) > 0) {
        g_initialized = 1U;
        soem_log("SOEM: init ok (AGV 2-wheel differential drive)");
    } else {
        soem_log("SOEM: init failed");
    }
}

/* ── Configuration state machine (runs until all slaves reach OPERATIONAL) ─ */

void SOEM_PeriodicPoll(void)
{
    if (!g_initialized) return;

    /* ── CONFIGURATION PHASE ─────────────────────────────────────────────── */
    if (!g_configured) {
        static uint8_t  cfg_phase   = 0;
        static uint16_t cfg_counter = 0;

        switch (cfg_phase) {

        case 0: { /* Discover slaves, apply PDO mapping, request SAFE_OP */
            int found = ecx_config_init(&soem_context);
            if (found <= 0) { soem_log("SOEM: no slaves found"); return; }

            g_active_axes = (found < (int)AXIS_COUNT) ? (uint8_t)found : AXIS_COUNT;
            soem_logf("SOEM: found %d slave(s), activating %u axis(es)",
                      found, g_active_axes);

            /* Force INIT → PRE_OP for all slaves */
            soem_context.slavelist[0].state = EC_STATE_PRE_OP;
            ecx_writestate(&soem_context, 0);
            for (int w = 0; w < 20; w++) {
                for (volatile int d = 0; d < 1000000; d++) {}
                ecx_statecheck(&soem_context, 0, EC_STATE_PRE_OP, 100000);
                ecx_readstate(&soem_context);
                uint8_t all_preop = 1U;
                for (uint8_t ax = 0; ax < g_active_axes; ax++) {
                    if ((soem_context.slavelist[ROBOT_SLAVE_IDX(ax)].state & 0x0FU)
                        < EC_STATE_PRE_OP)
                    { all_preop = 0U; break; }
                }
                if (all_preop) break;
            }

            /* Apply PDO mapping to each slave */
            for (uint8_t ax = 0; ax < g_active_axes; ax++) {
                /* ACK error flag if present */
                uint16_t sl = ROBOT_SLAVE_IDX(ax);
                if (soem_context.slavelist[sl].state & 0x10U) {
                    soem_context.slavelist[sl].state = EC_STATE_PRE_OP | EC_STATE_ERROR;
                    ecx_writestate(&soem_context, sl);
                    for (volatile int d = 0; d < 500000; d++) {}
                    soem_context.slavelist[sl].state = EC_STATE_PRE_OP;
                    ecx_writestate(&soem_context, sl);
                    for (volatile int d = 0; d < 300000; d++) {}
                }
                if (!apply_pdo_mapping(ax)) {
                    soem_logf("SOEM: PDO mapping failed for Ax%s", robot_axis_name(ax));
                    return;
                }
            }

            ecx_config_map_group(&soem_context, soem_iomap, soem_group);
            bind_pdo_pointers();

            soem_context.slavelist[0].state = EC_STATE_SAFE_OP;
            ecx_writestate(&soem_context, 0);

            cfg_phase = 1; cfg_counter = 0;
            return;
        }

        case 1: { /* Wait SAFE_OP */
            (void)ecx_send_processdata(&soem_context);
            (void)ecx_receive_processdata(&soem_context, EC_TIMEOUTRET);
            ecx_readstate(&soem_context);
            cfg_counter++;

            uint8_t all_safeop = 1U;
            for (uint8_t ax = 0; ax < g_active_axes; ax++) {
                if ((soem_context.slavelist[ROBOT_SLAVE_IDX(ax)].state & 0x0FU)
                    < EC_STATE_SAFE_OP)
                { all_safeop = 0U; break; }
            }

            if (all_safeop) {
                soem_logf("SOEM: all SAFE_OP (%u cycles) — PDO burst", cfg_counter);
                for (int burst = 0; burst < 200; burst++) {
                    (void)ecx_send_processdata(&soem_context);
                    (void)ecx_receive_processdata(&soem_context, EC_TIMEOUTRET);
                    for (volatile int d = 0; d < 100000; d++) {}
                    if (burst == 50) {
                        soem_log("SOEM: requesting OPERATIONAL");
                        soem_context.slavelist[0].state = EC_STATE_OPERATIONAL;
                        ecx_writestate(&soem_context, 0);
                    }
                    if (burst > 50 && (burst % 10) == 0) {
                        ecx_readstate(&soem_context);
                        uint8_t all_op = 1U;
                        for (uint8_t ax = 0; ax < g_active_axes; ax++) {
                            if (soem_context.slavelist[ROBOT_SLAVE_IDX(ax)].state
                                != EC_STATE_OPERATIONAL)
                            { all_op = 0U; break; }
                        }
                        if (all_op) {
                            g_configured = 1U;
                            soem_log("SOEM: all axes OPERATIONAL (burst)");
                            return;
                        }
                    }
                }
                cfg_phase = 2; cfg_counter = 0;
            } else if ((cfg_counter % 10U) == 0U) {
                soem_context.slavelist[0].state = EC_STATE_SAFE_OP;
                ecx_writestate(&soem_context, 0);
            }
            if (cfg_counter > 100U) { soem_log("SOEM: SAFE_OP timeout"); cfg_phase = 0; cfg_counter = 0; }
            return;
        }

        case 2: { /* Wait OPERATIONAL */
            (void)ecx_send_processdata(&soem_context);
            (void)ecx_receive_processdata(&soem_context, EC_TIMEOUTRET);
            ecx_readstate(&soem_context);
            cfg_counter++;

            uint8_t all_op = 1U;
            for (uint8_t ax = 0; ax < g_active_axes; ax++) {
                if (soem_context.slavelist[ROBOT_SLAVE_IDX(ax)].state
                    != EC_STATE_OPERATIONAL)
                { all_op = 0U; break; }
            }

            if (all_op) {
                g_configured = 1U;
                soem_log("SOEM: all axes OPERATIONAL");
            } else if ((cfg_counter % 10U) == 0U) {
                soem_context.slavelist[0].state = EC_STATE_OPERATIONAL;
                ecx_writestate(&soem_context, 0);
            }
            if (cfg_counter > 100U) { soem_log("SOEM: OP timeout"); cfg_phase = 0; cfg_counter = 0; }
            return;
        }

        default:
            cfg_phase = 0;
            return;
        }
    }

    /* ── STEADY-STATE: 1ms cyclic PDO loop ───────────────────────────────── */

    (void)ecx_send_processdata(&soem_context);
    (void)ecx_receive_processdata(&soem_context, EC_TIMEOUTRET);

    for (uint8_t ax = 0; ax < g_active_axes; ax++) {
        soem_update_shadows(ax);
        soem_apply_pending_sdo(ax);
        soem_update_target_output(ax);
        soem_cia402_step(ax);
    }
}

uint8_t SOEM_GetActiveAxes(void) { return g_active_axes; }

/* ════════════════════════════════════════════════════════════════════════════
 *  Public API — data reads (from volatile shadow)
 * ══════════════════════════════════════════════════════════════════════════ */

int32_t  SOEM_GetPositionHw(AxisId_t ax)     { return axis_valid((uint8_t)ax) ? (int32_t)g_shadow[ax].pos_hw : 0; }
int32_t  SOEM_GetPositionUser(AxisId_t ax)   { return axis_valid((uint8_t)ax) ? axis_hw_to_user((uint8_t)ax, (int32_t)g_shadow[ax].pos_hw) : 0; }
int32_t  SOEM_GetPositionCenti(AxisId_t ax)  { return axis_valid((uint8_t)ax) ? (int32_t)g_shadow[ax].pos_centi : 0; }
int32_t  SOEM_GetVelocity(AxisId_t ax)       { return axis_valid((uint8_t)ax) ? (int32_t)g_shadow[ax].velocity : 0; }
int16_t  SOEM_GetTorque(AxisId_t ax)         { return axis_valid((uint8_t)ax) ? (int16_t)g_shadow[ax].torque : 0; }
uint16_t SOEM_GetStatusword(AxisId_t ax)     { return axis_valid((uint8_t)ax) ? (uint16_t)g_shadow[ax].statusword : 0; }
uint8_t  SOEM_GetCia402State(AxisId_t ax)    { return axis_valid((uint8_t)ax) ? g_shadow[ax].cia402_state : 0; }
uint8_t  SOEM_GetPdoReady(AxisId_t ax)       { return axis_valid((uint8_t)ax) ? g_shadow[ax].pdo_ready : 0; }
uint8_t  SOEM_GetRunEnable(AxisId_t ax)      { return axis_valid((uint8_t)ax) ? g_rt[ax].run_enable : 0; }
uint8_t  SOEM_IsTargetReached(AxisId_t ax)   { return axis_valid((uint8_t)ax) ? g_shadow[ax].target_reached : 0; }

void SOEM_GetStatusPkt(AxisId_t ax, AxisStatusPkt_t *pkt)
{
    if (!axis_valid((uint8_t)ax) || pkt == NULL) return;
    pkt->pos_centi  = (int32_t)g_shadow[ax].pos_centi;
    int32_t vel     = (int32_t)g_shadow[ax].velocity;
    pkt->velocity   = (int16_t)((vel > 32767) ? 32767 : (vel < -32768) ? -32768 : vel);
    pkt->torque     = (int16_t)g_shadow[ax].torque;
    pkt->statusword = (uint16_t)g_shadow[ax].statusword;
    pkt->cia402_state = g_shadow[ax].cia402_state;
    pkt->flags = (uint8_t)((g_shadow[ax].pdo_ready       ? 0x01U : 0U) |
                            (g_shadow[ax].target_reached   ? 0x02U : 0U) |
                            (g_shadow[ax].run_enable        ? 0x04U : 0U));
}

uint8_t SOEM_AllAxesReady(void)
{
    for (uint8_t ax = 0; ax < g_active_axes; ax++)
        if (!g_shadow[ax].pdo_ready) return 0U;
    return (g_active_axes > 0) ? 1U : 0U;
}

uint8_t SOEM_AllTargetsReached(void)
{
    for (uint8_t ax = 0; ax < g_active_axes; ax++)
        if (!g_shadow[ax].target_reached) return 0U;
    return (g_active_axes > 0) ? 1U : 0U;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Public API — motion commands
 * ══════════════════════════════════════════════════════════════════════════ */

void SOEM_FaultReset(AxisId_t ax)
{
    if ((uint8_t)ax == (uint8_t)AXIS_ALL) {
        for (uint8_t i = 0U; i < g_active_axes; i++)
            g_rt[i].fault_reset_pending = 1U;
    } else if (axis_valid((uint8_t)ax)) {
        g_rt[ax].fault_reset_pending = 1U;
    }
}

void SOEM_SetRunEnable(AxisId_t ax, uint8_t en)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_rt[ax].run_enable = en ? 1U : 0U;
}

void SOEM_SetAllRunEnable(uint8_t en)
{
    for (uint8_t ax = 0; ax < g_active_axes; ax++) g_rt[ax].run_enable = en ? 1U : 0U;
}

void SOEM_SetTargetHw(AxisId_t ax, int32_t hw)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_rt[ax].interp_active = 0U;
    g_rt[ax].target_hw = axis_clamp_hw((uint8_t)ax, hw);
}

void SOEM_SetTargetUser(AxisId_t ax, int32_t user)
{
    if (!axis_valid((uint8_t)ax)) return;
    int32_t hw = axis_user_to_hw((uint8_t)ax, user);
    g_rt[ax].interp_active = 0U;
    g_rt[ax].target_hw = axis_clamp_hw((uint8_t)ax, hw);
}

void SOEM_SetTargetDelta(AxisId_t ax, int32_t delta)
{
    if (!axis_valid((uint8_t)ax)) return;
    int32_t cur_user = axis_hw_to_user((uint8_t)ax, g_rt[ax].target_hw);
    SOEM_SetTargetUser(ax, cur_user + delta);
}

void SOEM_SetTargetVelocity(AxisId_t ax, int32_t vel_hw)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_rt[ax].target_vel_hw = vel_hw;
}

void SOEM_SetInterpolatedTarget(AxisId_t ax, int32_t hw)
{
    if (!axis_valid((uint8_t)ax)) return;
    /* Bypass the ramp generator: set both target and output to the same
     * value so soem_update_target_output() sees diff=0 and passes it
     * straight through to the PDO.                                  */
    g_rt[ax].interp_active = 1U;
    g_rt[ax].target_hw     = hw;
    g_rt[ax].target_hw_out = hw;
}

void SOEM_SetRampVelocity(AxisId_t ax, int32_t vel)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_rt[ax].ramp_velocity = (vel < 0) ? -vel : vel;
}

void SOEM_SyncRtFromAxisParam(void)
{
    /* Called after RobotFlash_Load() to propagate flash-restored values into
     * g_rt[], which was initialised from g_axis_param[] defaults BEFORE flash
     * was loaded.  Without this, step_per_cycle() uses default velocity. */
    for (uint8_t ax = 0U; ax < (uint8_t)AXIS_COUNT; ax++) {
        g_rt[ax].profile_velocity = g_axis_param[ax].profile_velocity;
        g_rt[ax].ramp_velocity    = g_axis_param[ax].profile_velocity;
        g_rt[ax].profile_accel_ms = g_axis_param[ax].profile_accel_ms;
        g_rt[ax].profile_decel_ms = g_axis_param[ax].profile_decel_ms;
        g_rt[ax].torque_limit     = g_axis_param[ax].torque_limit;
    }
}

void SOEM_LatchAllTargetToActual(void)
{
    for (uint8_t ax = 0U; ax < g_active_axes; ax++)
        latch_target_to_actual(ax);
}

void SOEM_SetProfileVelocity(AxisId_t ax, int32_t vel)
{
    if (!axis_valid((uint8_t)ax)) return;
    int32_t v = (vel < 0) ? -vel : vel;
    g_axis_param[ax].profile_velocity  = v;
    g_rt[ax].profile_velocity          = v;
    g_rt[ax].ramp_velocity             = v;   /* keep ramp in sync with profile */
    g_rt[ax].profile_velocity_pending  = 1U;
}

void SOEM_SetProfileAccel(AxisId_t ax, int32_t ms)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_axis_param[ax].profile_accel_ms  = ms;
    g_rt[ax].profile_accel_ms          = ms;
    g_rt[ax].profile_accel_pending     = 1U;
}

void SOEM_SetProfileDecel(AxisId_t ax, int32_t ms)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_axis_param[ax].profile_decel_ms  = ms;
    g_rt[ax].profile_decel_ms          = ms;
    g_rt[ax].profile_decel_pending     = 1U;
}

void SOEM_SetTorqueLimit(AxisId_t ax, uint16_t permille)
{
    if (!axis_valid((uint8_t)ax)) return;
    if (permille > 1000U) permille = 1000U;
    g_axis_param[ax].torque_limit      = permille;
    g_rt[ax].torque_limit              = permille;
    g_rt[ax].torque_limit_pending      = 1U;
}

/* ── Unit scaling ─────────────────────────────────────────────────────────  */

void SOEM_SetUnitScale(AxisId_t ax, int32_t scale)
{
    if (!axis_valid((uint8_t)ax) || scale <= 0) return;
    g_axis_param[ax].unit_scale    = scale;
    g_rt[ax].unit_scale_pending    = 1U;
    g_rt[ax].profile_velocity_pending = 1U;  /* unit_scale 변경 시 0x6081도 재전송 */
}

/* ── Homing ───────────────────────────────────────────────────────────────  */

void SOEM_SetHomePosition(AxisId_t ax)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_axis_param[ax].home_offset  = (int32_t)g_shadow[ax].pos_hw;
    g_rt[ax].home_offset_pending  = 1U;
    soem_logf("[Ax%s] home set hw=%ld", robot_axis_name((uint8_t)ax),
              (long)g_axis_param[ax].home_offset);

    /* limit_hw stores absolute encoder counts — physically unchanged after a
     * home change.  Recompute limit_user so the param panel always shows mm
     * relative to the new home rather than the old one.                      */
    if (g_axis_param[ax].limits_enabled) {
        g_axis_param[ax].limit_plus_user  =
            axis_hw_to_user((uint8_t)ax, g_axis_param[ax].limit_plus_hw);
        g_axis_param[ax].limit_minus_user =
            axis_hw_to_user((uint8_t)ax, g_axis_param[ax].limit_minus_hw);
    }
}

void SOEM_LoadHomeHwOffset(AxisId_t ax, int32_t hw_offset)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_axis_param[ax].home_offset  = hw_offset;
    g_rt[ax].home_offset_pending  = 1U;
}

int32_t SOEM_GetHomeOffset(AxisId_t ax)
{
    return axis_valid((uint8_t)ax) ? g_axis_param[ax].home_offset : 0;
}

/* ── Software limits ──────────────────────────────────────────────────────  */

void SOEM_SetLimitPlusUser(AxisId_t ax, int32_t lim)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_axis_param[ax].limit_plus_user = lim;
    g_axis_param[ax].limit_plus_hw   = axis_user_to_hw((uint8_t)ax, lim);
    refresh_hw_limits((uint8_t)ax);
    g_rt[ax].limits_pending = 1U;
}

void SOEM_SetLimitMinusUser(AxisId_t ax, int32_t lim)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_axis_param[ax].limit_minus_user = lim;
    g_axis_param[ax].limit_minus_hw   = axis_user_to_hw((uint8_t)ax, lim);
    refresh_hw_limits((uint8_t)ax);
    g_rt[ax].limits_pending = 1U;
}

void SOEM_CaptureLimitPlusHere(AxisId_t ax)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_axis_param[ax].limit_plus_hw   = (int32_t)g_shadow[ax].pos_hw;
    g_axis_param[ax].limit_plus_user = axis_hw_to_user((uint8_t)ax, (int32_t)g_shadow[ax].pos_hw);
    refresh_hw_limits((uint8_t)ax);
    g_rt[ax].limits_pending = 1U;
}

void SOEM_CaptureLimitMinusHere(AxisId_t ax)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_axis_param[ax].limit_minus_hw   = (int32_t)g_shadow[ax].pos_hw;
    g_axis_param[ax].limit_minus_user = axis_hw_to_user((uint8_t)ax, (int32_t)g_shadow[ax].pos_hw);
    refresh_hw_limits((uint8_t)ax);
    g_rt[ax].limits_pending = 1U;
}

void SOEM_SetLimitPlusHw(AxisId_t ax, int32_t hw)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_axis_param[ax].limit_plus_hw   = hw;
    g_axis_param[ax].limit_plus_user = axis_hw_to_user((uint8_t)ax, hw);
    refresh_hw_limits((uint8_t)ax);
}

void SOEM_SetLimitMinusHw(AxisId_t ax, int32_t hw)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_axis_param[ax].limit_minus_hw   = hw;
    g_axis_param[ax].limit_minus_user = axis_hw_to_user((uint8_t)ax, hw);
    refresh_hw_limits((uint8_t)ax);
}

int32_t SOEM_GetLimitPlusHw(AxisId_t ax)  { return axis_valid((uint8_t)ax) ? g_axis_param[ax].limit_plus_hw  : INT32_MAX; }
int32_t SOEM_GetLimitMinusHw(AxisId_t ax) { return axis_valid((uint8_t)ax) ? g_axis_param[ax].limit_minus_hw : INT32_MIN; }

void SOEM_RefreshAllLimits(void)
{
    for (uint8_t ax = 0U; ax < (uint8_t)AXIS_COUNT; ax++)
        refresh_hw_limits(ax);
}

/* ── Position gain ────────────────────────────────────────────────────────  */

void SOEM_SetPositionGain(AxisId_t ax, int32_t gain)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_axis_param[ax].position_gain     = gain;
    g_rt[ax].position_gain             = gain;
    g_rt[ax].position_gain_pending     = (gain > 0) ? 1U : 0U;
}

void SOEM_RequestGainRead(AxisId_t ax)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_rt[ax].position_gain_read_pending = 1U;
    g_rt[ax].position_gain_read_done    = 0U;
}

uint8_t SOEM_FetchGainRead(AxisId_t ax, int32_t *gain)
{
    if (!axis_valid((uint8_t)ax) || !g_rt[ax].position_gain_read_done) return 0U;
    if (gain) *gain = (int32_t)g_rt[ax].position_gain_readback;
    g_rt[ax].position_gain_read_done = 0U;
    return 1U;
}

int32_t SOEM_GetGainReadStatus(AxisId_t ax)
{
    return axis_valid((uint8_t)ax) ? (int32_t)g_rt[ax].position_gain_read_status : -1;
}

/* ── Parameter bulk read ──────────────────────────────────────────────────  */

void SOEM_RequestParamRead(AxisId_t ax)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_rt[ax].param_read_pending = 1U;
    g_rt[ax].param_read_done    = 0U;
}

uint8_t SOEM_FetchParamRead(AxisId_t ax, int32_t *values, uint8_t count)
{
    if (!axis_valid((uint8_t)ax) || !g_rt[ax].param_read_done) return 0U;
    if (values && count > 0U) {
        uint8_t n = (count > SOEM_PARAM_READ_COUNT) ? SOEM_PARAM_READ_COUNT : count;
        for (uint8_t i = 0; i < n; i++)
            values[i] = (int32_t)g_rt[ax].param_read_values[i];
    }
    g_rt[ax].param_read_done = 0U;
    return 1U;
}

#endif /* SOEM_ENABLED */
