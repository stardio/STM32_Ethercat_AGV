# Bug Fix #2 — Arc Interpolation and the `interp_active` Flag

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 19 of 20  
**Tags:** Bug Fix, Arc Interpolation, Soft Limits, Embedded C, Debugging

---

## The Symptom

After the velocity fix, arc interpolation was tested with a large radius: `G02 X0 Y0 I-250 J0 F100` (250 mm radius full circle). The robot drew a circle — but stopped partway through and reported a soft limit hit.

The soft limits were set correctly. The workspace was large enough for the circle. Why was the arc hitting a limit?

---

## First Hypothesis: Wrong Limit Values

The limits were printed to the log. They showed correct values. The circle (radius 250 mm, centered at X-250 mm from start) was geometrically within the allowed workspace. The limit values were not the problem.

---

## Second Hypothesis: Actual Encoder Value Exceeds Limit

During the arc, the actual encoder position was logged. It showed values within the workspace — not near the limits.

But `limits_blocked` was being set. The `axis_clamp_hw()` function was being called and returning a clamped value.

---

## Finding the Culprit: `soem_update_target_output()`

```c
static void soem_update_target_output(uint8_t ax)
{
    int32_t cmd    = g_rt[ax].target_hw;
    int32_t output = g_rt[ax].target_hw_out;
    int32_t actual = (int32_t)g_shadow[ax].pos_hw;

    int64_t err_actual = llabs((int64_t)cmd - (int64_t)actual);

    if (err_actual <= ROBOT_TARGET_TOLERANCE_HW) {
        /* BUG: clamp applied even during interpolation! */
        g_rt[ax].target_hw_out = axis_clamp_hw(ax, cmd);
        g_shadow[ax].target_reached = 1U;
        return;
    }
    /* ... ramp path ... */
}
```

In CSP mode, the servo tracks the commanded position very tightly — typically within 1–3 encoder counts. The tolerance is 5 counts. So for almost every tick during interpolation, `err_actual <= 5` was TRUE.

This means: nearly every cycle, the interpolation target was passed through `axis_clamp_hw()`. The arc commands positions outside the soft limit on purpose (to draw the circle), but the clamp was silently overwriting them with the limit boundary value.

The interpolator commanded position (7, 3, ...) mm from center. The ramp function clamped it to the soft limit boundary. The servo followed the clamped value. The circle became a straight line segment ending at the limit.

---

## The Fix: `interp_active` Flag

The solution: an `interp_active` flag on `AxisRuntime_t` that bypasses the soft limit clamp during interpolated moves.

**New field in `AxisRuntime_t`:**
```c
volatile uint8_t interp_active;   /* 1 while interpolator controls this axis */
```

**Set in `SOEM_SetInterpolatedTarget()`:**
```c
void SOEM_SetInterpolatedTarget(AxisId_t ax, int32_t hw)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_rt[ax].interp_active  = 1U;
    g_rt[ax].target_hw      = hw;
    g_rt[ax].target_hw_out  = hw;
}
```

**Cleared in `SOEM_SetTargetHw()` and `latch_target_to_actual()`:**
```c
void SOEM_SetTargetHw(AxisId_t ax, int32_t hw)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_rt[ax].interp_active = 0U;
    g_rt[ax].target_hw     = axis_clamp_hw((uint8_t)ax, hw);
}

static void latch_target_to_actual(uint8_t ax)
{
    int32_t actual = (int32_t)g_shadow[ax].pos_hw;
    g_rt[ax].interp_active = 0U;
    g_rt[ax].target_hw     = axis_clamp_hw(ax, actual);
    g_rt[ax].target_hw_out = g_rt[ax].target_hw;
}
```

**Updated `soem_update_target_output()`:**
```c
if (err_actual <= ROBOT_TARGET_TOLERANCE_HW) {
    g_rt[ax].target_hw_out = g_rt[ax].interp_active
                             ? cmd                   /* bypass clamp — arc in progress */
                             : axis_clamp_hw(ax, cmd); /* normal: apply limits */
    g_shadow[ax].target_reached = 1U;
    return;
}
```

---

## Why the Ramp Path Is Also Safe

In the ramp path (when `err_actual > TOLERANCE`):

```c
g_shadow[ax].target_reached = 0U;
if (output == cmd) return;   /* early return: output was set by SOEM_SetInterpolatedTarget */
/* ... clamp applied here ... */
g_rt[ax].target_hw_out = axis_clamp_hw(ax, output);
```

`SOEM_SetInterpolatedTarget()` sets BOTH `target_hw` AND `target_hw_out` to the same value (`hw`). So `output == cmd` is always true in the ramp path, and the function returns before reaching `axis_clamp_hw()`. The clamp in the ramp path is never reached for interpolated targets.

---

## After the Fix: Radius Reduction

With the clamp fix applied, the 250 mm arc worked — but hit the machine's physical limits (the workspace wasn't large enough). The test was repeated with 1/3 the radius: `I=-83 J=0` (83 mm radius). This became the new standard test circle.

This led to discovering Bug #3 (Post 20): the 83 mm full circle sometimes drew a straight line, while the 250 mm circle had always worked. Same bug, different behavior — because the root cause was probabilistic.

---

## Key Design Principle Reinforced

The `interp_active` flag is an example of a principle that came up repeatedly in this project: **separate the concept of "what the machine should be doing" from "what safety checks apply in that mode."**

During G00/JOG: the user is commanding arbitrary positions, so soft limits must always apply.

During G01/G02/G03: the trajectory planner has already computed a safe path within the workspace. Applying limits mid-trajectory corrupts the path. The planner owns the safety guarantee during motion; limits are enforced at the planning boundary, not during execution.

---

## Next Post

The final bug is the most subtle: a floating-point edge case in `normalise_arc_sweep()` that caused full-circle arcs to degenerate to zero length exactly 50% of the time.
