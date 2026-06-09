# Cyclic Synchronous Position Mode — Real-Time Position Control

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 6 of 20  
**Tags:** CSP, Motion Control, Velocity Ramp, EtherCAT, Real-Time

---

## What CSP Mode Means

In **Cyclic Synchronous Position (CSP)** mode, the EtherCAT master is responsible for generating position setpoints at every PDO cycle. The servo drive's job is simply to follow: its internal control loop (current → velocity → position) tracks the commanded position as fast as physics allows.

This is fundamentally different from **Profile Position mode** where you give the drive a target and it generates its own motion profile internally. In CSP:

- The master generates the trajectory
- The drive provides position feedback (encoder)
- The drive's position controller minimizes the following error

The advantage: the master has full control over the path. You can do multi-axis coordinated motion, arbitrary velocity profiles, and real-time trajectory modifications. The drive is just a "smart amplifier."

---

## The Position Ramp Generator

Since the drive only follows (it doesn't limit acceleration itself in CSP mode), we must ensure we never command an impossible velocity jump. A sudden step in commanded position would demand infinite acceleration.

The software ramp generator runs inside `soem_update_target_output()`, called every 1 ms:

```c
static void soem_update_target_output(uint8_t ax)
{
    int32_t cmd    = g_rt[ax].target_hw;      /* desired final position */
    int32_t output = g_rt[ax].target_hw_out;  /* current commanded position */
    int32_t actual = (int32_t)g_shadow[ax].pos_hw;

    /* How far is the servo from the desired final position? */
    int64_t err_actual = llabs((int64_t)cmd - (int64_t)actual);

    if (err_actual <= ROBOT_TARGET_TOLERANCE_HW) {
        /* Close enough — snap to target and declare reached */
        g_rt[ax].target_hw_out   = interp_active ? cmd : axis_clamp_hw(ax, cmd);
        g_shadow[ax].target_reached = 1U;
        return;
    }

    g_shadow[ax].target_reached = 0U;

    if (output == cmd) return;   /* Already commanding target — nothing to ramp */

    /* Step output toward target by at most step_per_cycle() counts */
    const int32_t step = step_per_cycle(ax);
    const int64_t diff = (int64_t)cmd - (int64_t)output;

    if      (diff >  step) output += step;
    else if (diff < -step) output -= step;
    else                   output  = cmd;   /* last step: snap exactly */

    g_rt[ax].target_hw_out = axis_clamp_hw(ax, output);
}
```

`target_hw_out` is what actually gets written to the PDO every cycle. `target_hw` is where we want to end up. The ramp moves `target_hw_out` toward `target_hw` at a rate limited by `step_per_cycle()`.

---

## Calculating Step Size

```c
static int32_t step_per_cycle(uint8_t ax)
{
    /* Which velocity to use: ramp_velocity (if set by JOG) else profile_velocity */
    int32_t vel_mm_s = (g_rt[ax].ramp_velocity > 0)
                       ? g_rt[ax].ramp_velocity
                       : g_rt[ax].profile_velocity;

    if (vel_mm_s <= 0) return 0;

    /* counts/ms = mm/s * counts/mm / 1000 ms/s */
    int64_t step = (int64_t)vel_mm_s
                 * (int64_t)g_axis_param[ax].unit_scale
                 / 1000LL;

    if (step < 1)             step = 1;
    if (step > INT32_MAX)     step = INT32_MAX;
    return (int32_t)step;
}
```

Example: at `profile_velocity = 100 mm/s` and `unit_scale = 4000 counts/mm`:
```
step = 100 * 4000 / 1000 = 400 counts/ms
```

So the commanded position advances by 400 encoder counts (= 0.1 mm) per 1 ms cycle, giving a constant velocity of 100 mm/s.

---

## Target Reached Detection

`target_reached` is set when the *actual servo position* is within `ROBOT_TARGET_TOLERANCE_HW` (5 encoder counts ≈ 1.25 µm) of the commanded final target. This drives:

- G00 completion detection (all axes target_reached → interpolator DONE)
- JOG status reporting to the Web HMI
- SDO gate (allow parameter writes only when settled)

The tolerance of 5 counts was chosen empirically — tight enough to give accurate "arrived" feedback, loose enough that normal servo oscillation doesn't cause repeated arrived/not-arrived toggling.

---

## Unit Scale — The Key Parameter

`unit_scale` (encoder counts per millimeter) is the most important axis parameter. All mm↔HW conversions use it:

```c
static inline int32_t mm_to_hw(uint8_t ax, int32_t mm)
{
    return mm * (int32_t)g_axis_param[ax].unit_scale
               + (int32_t)g_axis_param[ax].home_offset;
}

static inline int32_t hw_to_mm(uint8_t ax, int32_t hw)
{
    int32_t scale = (int32_t)g_axis_param[ax].unit_scale;
    if (scale == 0) return 0;
    return (hw - (int32_t)g_axis_param[ax].home_offset) / scale;
}
```

For this machine: `unit_scale = 4000` means the encoder resolves 4000 counts per mm of linear travel.

---

## Home Offset

`home_offset` is the absolute encoder count at the user-defined zero position. When the user commands "set home here", the firmware records the current encoder reading:

```c
void SOEM_SetHomePosition(AxisId_t ax)
{
    g_axis_param[ax].home_offset = (int32_t)g_shadow[ax].pos_hw;
    /* Limits stay in absolute HW counts — no need to recalculate */
}
```

User-space coordinates (`mm`) are always relative to the home offset. Hardware coordinates (PDO, limits) are always absolute encoder counts. The conversion functions bridge the two worlds.

---

## Two Coordinate Systems

Always keeping these separate prevents a whole class of bugs:

| System | Unit | Used For |
|--------|------|----------|
| **User space** | mm | Web HMI display, G-code commands, JOG input |
| **Hardware space** | encoder counts | PDO target/actual, soft limits, flash storage |

The conversion functions `mm_to_hw_f()` (float version for interpolator) and `mm_to_hw()` (integer version for everything else) are called at the boundary — never inside the hot path of the 1 ms loop.

---

## Next Post

With one axis moving under velocity-ramped CSP control, the next step is obvious: scale to three axes. Post 7 covers the multi-axis API design — runtime state, shadow mirrors, and the concurrency model between the 1 ms EtherCAT task and the 10 ms housekeeping task.
