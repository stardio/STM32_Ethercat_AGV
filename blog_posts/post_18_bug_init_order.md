# Bug Fix #1 — Initialization Order: When Flash Load Comes Too Late

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 18 of 20  
**Tags:** Bug Fix, Initialization, FreeRTOS, Embedded C, Debugging

---

## The Symptom

After implementing Flash parameter storage, a strange behavior appeared: **JOG ran at the correct speed from the web HMI parameter settings, but G00 (rapid move) ran at a much slower, default speed**.

The HMI showed `profile_velocity = 200 mm/s` in the parameter panel. JOG correctly moved at 200 mm/s (or a multiple thereof). But when executing `G00 X100`, the axis crept along at what looked like the default 100 mm/s.

Adjusting the parameter, saving to flash, and rebooting made no difference. G00 stubbornly ran at 100 mm/s.

---

## Gathering Evidence

The JOG diagnostic log (added to every JOG command) showed:
```
[JOG] ax=0 dir=+1 step=1.000 dHW=4000 curHW=-3924000 tgtHW=-3920000 scale=4000 vel=200
```

Velocity 200 mm/s — correct. The JOG path was using the right value.

For G00, there was no similar log. I added one to `g00_plan()`:
```
[G00] ax=0 target=-981mm hw=-3924000 vel=100 accel=500 scale=4000
```

Velocity 100 mm/s — the compiled-in default, not the flash-saved 200 mm/s.

So `g_rt[ax].profile_velocity` was 100, but `g_axis_param[ax].profile_velocity` was 200. Somehow the runtime struct had stale values.

---

## The Root Cause: Initialization Order

Looking at the `EtherCAT_Task` startup sequence:

```c
void EtherCAT_Task(void *arg)
{
    /* Step 1: Initialize SOEM and configure drives */
    SOEM_PortInit();       /* ← sets g_rt[ax] from g_axis_param[] defaults */

    /* ... wait for EtherCAT OP state ... */

    /* Step 2: Load flash parameters (also called from DefaultTask indirectly) */
    Pc_CommandInit();
        AxisConfig_InitDefaults();   /* resets g_axis_param to compile defaults */
        RobotFlash_Load();           /* overwrites g_axis_param with FLASH values */
        SOEM_SyncRtFromAxisParam();  /* ← MISSING! Added in the fix */
        SOEM_RefreshAllLimits();

    /* Step 3: Enter 1ms loop */
    for (;;) {
        Interp_Tick();
        SOEM_PeriodicPoll();    /* uses g_rt[ax].profile_velocity */
        osDelay(1);
    }
}
```

The problem:

1. `SOEM_PortInit()` initializes `g_rt[ax]` by copying from `g_axis_param[]`
2. At this point, `g_axis_param[]` has compile-time defaults (100 mm/s)
3. `Pc_CommandInit()` then loads flash → `g_axis_param[ax].profile_velocity = 200`
4. **But `g_rt[ax].profile_velocity` was never updated after the flash load!**

`SOEM_PeriodicPoll()` uses `g_rt[ax].profile_velocity` in `step_per_cycle()`. `g_axis_param[ax].profile_velocity` is what the HMI reads and what flash stores. They were silently out of sync.

---

## Why JOG Was Not Affected

JOG sends a `SET_PARAM` command with `PARAM_RAMP_VEL`:

```javascript
/* HMI computes JOG velocity from pmLoaded (flash-loaded param_report) */
const baseVel = pmLoaded[axis]?.profile_velocity ?? 10;  // = 200
const jogVel  = Math.round(baseVel * jogStep);           // = 200 * 1 = 200
send({ cmd: 'set_param', axis, param_id: PARAM.RAMP_VEL, value: 200 });
```

The firmware handler for `PARAM_RAMP_VEL`:
```c
case PROTO_PARAM_RAMP_VEL:
    SOEM_SetRampVelocity((AxisId_t)ax, value);  /* directly sets g_rt[ax].ramp_velocity */
    break;
```

`SOEM_SetRampVelocity()` writes directly to `g_rt[ax].ramp_velocity`, bypassing the stale `g_rt[ax].profile_velocity`. And in `step_per_cycle()`:

```c
int32_t vel_mm_s = (g_rt[ax].ramp_velocity > 0)
                   ? g_rt[ax].ramp_velocity      /* JOG uses this */
                   : g_rt[ax].profile_velocity;  /* G00 uses this */
```

JOG always sets `ramp_velocity` explicitly → correct speed.
After JOG stop: `ramp_velocity = 0` → falls back to `profile_velocity` → stale default.
G00 never sets `ramp_velocity` → uses `profile_velocity` → always stale.

---

## The Fix: `SOEM_SyncRtFromAxisParam()`

The fix is a single new function that copies flash-loaded values from `g_axis_param[]` into `g_rt[]`:

```c
void SOEM_SyncRtFromAxisParam(void)
{
    for (uint8_t ax = 0U; ax < (uint8_t)AXIS_COUNT; ax++) {
        g_rt[ax].profile_velocity = g_axis_param[ax].profile_velocity;
        g_rt[ax].ramp_velocity    = g_axis_param[ax].profile_velocity;
        g_rt[ax].profile_accel_ms = g_axis_param[ax].profile_accel_ms;
        g_rt[ax].profile_decel_ms = g_axis_param[ax].profile_decel_ms;
        g_rt[ax].torque_limit     = g_axis_param[ax].torque_limit;
    }
}
```

Called in `Pc_CommandInit()` immediately after `RobotFlash_Load()`:

```c
void Pc_CommandInit(void)
{
    AxisConfig_InitDefaults();

    if (RobotFlash_Load() != 0U) {
        for (ax = 0; ax < AXIS_COUNT; ax++)
            SOEM_LoadHomeHwOffset((AxisId_t)ax, g_axis_param[ax].home_offset);
    }

    SOEM_SyncRtFromAxisParam();   /* ← THE FIX */
    SOEM_RefreshAllLimits();
}
```

After this fix, G00 runs at the flash-saved velocity from the first motion command after boot.

---

## Lessons Learned

**1. Two structs for the same concept is a maintenance trap.**

Having `g_axis_param[ax].profile_velocity` and `g_rt[ax].profile_velocity` as separate fields with the same meaning was the root cause. A single source of truth would have prevented this. The separation exists for thread-safety reasons (DefaultTask writes params, EtherCAT_Task reads rt), but the synchronization step must be explicit.

**2. Init order bugs are hard to see without diagnostic logging.**

The JOG diagnostic log (`[JOG] vel=...`) immediately showed which struct was being read. Without it, we'd still be staring at the wrong part of the code.

**3. The sequence matters: `PortInit → FlashLoad → Sync → Run`.**

Write it as a comment in the source. If someone reorders these calls in a future refactor, the comment will at least give them pause.

---

## Next Post

With speed fixed, the arc seemed to work — but only sometimes. Post 19 covers a much subtler bug: the arc interpolator was being silently clamped by the soft-limit system mid-circle, turning arcs into straight lines.
