# Software Limits — Protecting the Machine from Itself

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 16 of 20  
**Tags:** Soft Limits, Safety, Motion Control, Embedded C

---

## Why Software Limits Matter

Hardware end-stop switches are the last line of defense. Software limits should stop motion long before the machine reaches the physical switch. Without them, a programming mistake or a bad G-code value sends the axis crashing into the frame at full speed.

Good software limits have two properties:
1. **Absolute** — expressed in encoder counts, independent of user-defined home position
2. **Automatic** — active immediately after boot, without any manual configuration step

This is trickier than it sounds.

---

## Two Coordinate Systems, Two Limit Types

```c
typedef struct {
    /* Hardware limits — absolute encoder counts, never change with home */
    int32_t limit_plus_hw;     /* maximum allowed encoder position */
    int32_t limit_minus_hw;    /* minimum allowed encoder position */

    /* User limits — mm from home, for display and user input */
    int32_t limit_plus_user;   /* = (limit_plus_hw - home_offset) / unit_scale */
    int32_t limit_minus_user;  /* = (limit_minus_hw - home_offset) / unit_scale */

    uint8_t limits_enabled;    /* 1 if both HW limits are non-zero */
    uint8_t limits_blocked;    /* 1 if last move was stopped by a limit */
} AxisParam_t;
```

The HW limits are stored in flash and survive home position changes. When the user redefines home (sets a new zero point), the HW limits stay fixed — only the user-space mm display values (`limit_plus_user`, `limit_minus_user`) are recalculated.

---

## The Clamping Function

Every target position passes through `axis_clamp_hw()` before being written to the PDO:

```c
static int32_t axis_clamp_hw(uint8_t ax, int32_t hw)
{
    if (!g_axis_param[ax].limits_enabled) return hw;

    if (hw > g_axis_param[ax].limit_plus_hw) {
        g_axis_param[ax].limits_blocked = 1U;
        return g_axis_param[ax].limit_plus_hw;
    }
    if (hw < g_axis_param[ax].limit_minus_hw) {
        g_axis_param[ax].limits_blocked = 1U;
        return g_axis_param[ax].limit_minus_hw;
    }
    g_axis_param[ax].limits_blocked = 0U;
    return hw;
}
```

This is applied in:
- `SOEM_SetTargetHw()` — JOG, G00 step commands
- `soem_update_target_output()` — ramp generator output, for non-interpolated moves
- `latch_target_to_actual()` — when the drive is disabled or faulted

---

## Limit Refresh After Home Changes

When the user changes the home position, `home_offset` changes. The mm-space limit values must be recalculated:

```c
void SOEM_RefreshAllLimits(void)
{
    for (uint8_t ax = 0; ax < AXIS_COUNT; ax++) {
        refresh_hw_limits(ax);
    }
}

static void refresh_hw_limits(uint8_t ax)
{
    /* limits_enabled if both user limits are non-zero */
    int32_t pu = g_axis_param[ax].limit_plus_user;
    int32_t mu = g_axis_param[ax].limit_minus_user;

    g_axis_param[ax].limits_enabled = (pu != 0 || mu != 0) ? 1U : 0U;

    if (!g_axis_param[ax].limits_enabled) return;

    int32_t scale  = g_axis_param[ax].unit_scale;
    int32_t origin = g_axis_param[ax].home_offset;

    /* HW = user_mm * scale + home_offset */
    g_axis_param[ax].limit_plus_hw  = pu * scale + origin;
    g_axis_param[ax].limit_minus_hw = mu * scale + origin;
}
```

When the user calls "Set Home Here", `home_offset` is updated and `SOEM_RefreshAllLimits()` is called immediately to keep the HW limits consistent.

---

## Limits During Arc Interpolation — The Subtlety

Here is where things get interesting. Arc interpolation (G02/G03) uses `SOEM_SetInterpolatedTarget()` to bypass the ramp generator and set `target_hw_out` directly. But there's a branch in `soem_update_target_output()` that also applies `axis_clamp_hw()`:

```c
if (err_actual <= ROBOT_TARGET_TOLERANCE_HW) {
    g_rt[ax].target_hw_out = g_rt[ax].interp_active
                             ? cmd          /* bypass clamp during interpolation */
                             : axis_clamp_hw(ax, cmd);
    g_shadow[ax].target_reached = 1U;
    return;
}
```

Without the `interp_active` check (the original code), the clamp was applied even during arc moves. The result: the arc would draw a straight line segment to the limit boundary and stop there. See Post 19 for the full bug story.

---

## Setting Limits from the HMI

The parameter editor shows `limit_plus_user` and `limit_minus_user` in mm. When the user enters values:

```javascript
// Save soft limits
send({ cmd: 'set_param', axis: 0, param_id: PARAM.LIMIT_PLUS_USER,  value: 1000 });
send({ cmd: 'set_param', axis: 0, param_id: PARAM.LIMIT_MINUS_USER, value: -1000 });
send({ cmd: 'save_flash' });
```

In the firmware, receiving `PARAM_LIMIT_PLUS_USER`:
```c
case PROTO_PARAM_LIMIT_PLUS_USER:
    g_axis_param[ax].limit_plus_user = value;
    refresh_hw_limits(ax);   /* recalculate HW limit from new user value */
    break;
```

Setting both limits to 0 disables the limit for that axis: `limits_enabled = 0`.

---

## Boot Activation

At boot, after `RobotFlash_Load()`, the limits are immediately activated:

```c
/* In Pc_CommandInit() */
SOEM_RefreshAllLimits();
```

This call recalculates `limit_plus_hw` and `limit_minus_hw` from the flash-loaded `limit_plus_user`, `limit_minus_user`, `home_offset`, and `unit_scale`. From the very first PDO cycle, any commanded position outside the limits is clamped.

---

## What Happens When a Limit Is Hit

`limits_blocked` is set to 1U in `axis_clamp_hw()` when the target is out of range. This propagates to the STATUS packet's flags field. The HMI shows the affected axis in a different color and displays a "LIMIT" badge.

The motor does not fault — it simply holds position at the limit boundary. The user can still jog away from the limit in the opposite direction. This behavior is preferred over a fault (which requires a fault-reset sequence to recover).

---

## Next Post

With limits protecting the machine, the next post tackles multi-block G-code program execution — how the HMI sequences through a list of G-code lines, waits for each move to complete, and handles errors mid-program.
