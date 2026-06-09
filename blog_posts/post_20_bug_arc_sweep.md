# Bug Fix #3 — The Full-Circle Arc Degeneracy

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 20 of 20  
**Tags:** Bug Fix, Arc Interpolation, Floating Point, Edge Case, Debugging

---

## The Symptom

After the `interp_active` fix (Post 19), the 250 mm radius full circle worked reliably. But when the test radius was reduced to 83 mm with the same G-code pattern (`G02 X<same> Y<same> I-83 J0 F100`), the robot drew a **straight line instead of a circle**.

More precisely: it barely moved, then instantly snapped to the end position. From the operator's view — watching the axes — it looked like a very short straight-line motion.

The G-code was correct. The `interp_active` fix was in place. Reducing the radius should not have changed anything fundamental.

---

## Analysis: What's Different Between 250 mm and 83 mm?

For a full circle: start point == end point. The G-code `G02 X-981 Y-354 I-83 J0 F100` starts from `(-981, -354, -50)` and ends at the same coordinates.

In `arc_plan()`, the sweep angle is computed by `normalise_arc_sweep(theta_start, theta_end, cw)`.

With exact start == end: `theta_start == theta_end == 0`, `diff = 0`:
- `while (diff >= 0.0f)`: condition TRUE (0 >= 0) → `diff -= 2π` → `diff = -2π` ✓

This looked correct. So where does the failure come from?

---

## The Position Error Problem

`arc_plan()` reads the actual servo position from `g_shadow[ax].pos_hw`:

```c
start_mm[ax] = hw_to_mm_f(ax, (int32_t)g_shadow[ax].pos_hw);
```

This is the *actual* encoder position, not the commanded position. In CSP mode, the servo follows the commanded target very closely — typically within 1–5 encoder counts of the commanded value. That's a maximum of **5/4000 = 0.00125 mm** of position error.

The arc is commanded to end at `(X=-981, Y=-354)`. After G00 completes, the actual Y position might be `-353.9999` mm (0.0001 mm above the commanded position).

Now `arc_plan()` computes:

```
cy_mm = actual_y + J = -353.9999 + 0 = -353.9999
end_b_rel = req->target_mm[pb] - cy_mm = -354 - (-353.9999) = -0.0001
```

`theta_end = atan2(-0.0001, 83) ≈ -0.0000012 rad` (tiny NEGATIVE value)

`normalise_arc_sweep(0, -0.0000012, CW)`:

```c
float diff = end - start;   // = -0.0000012 - 0 = -0.0000012
if (cw) {
    while (diff >= 0.0f) diff -= INTERP_TWO_PI;  // -0.0000012 >= 0? NO → loop skips!
}
return diff;   // = -0.0000012
```

The `while` condition `diff >= 0.0f` is **FALSE** because `-0.0000012 < 0`. The loop doesn't execute. The sweep is returned as `-0.0000012 rad` — not `-2π`!

```
arc_len = 83 * 0.0000012 = 0.0001 mm
ticks = 0.0001 / 100 * 1000 = 0.001 → rounded up to 1
```

**The arc executes in exactly 1 tick and snaps to the end position.** No visible movement.

---

## Why Did 250 mm Work and 83 mm Fail?

The servo position error is independent of radius. At radius 83 mm, a 0.0001 mm error in Y corresponds to:

```
theta_error = 0.0001 / 83 = 0.0000012 rad
```

At radius 250 mm, the same 0.0001 mm error gives:

```
theta_error = 0.0001 / 250 = 0.0000004 rad
```

The *angle* error is smaller at larger radius. But more importantly: **the sign of the error depends on which direction the servo overshoots**. If the actual Y position is below -354 (error negative): `end_b_rel = -354 - (something more negative) = positive` → `theta_end > 0` → `diff > 0` → the while loop fires, sweep = -2π ✓.

If actual Y is above -354: `end_b_rel` is negative → sweep degenerates.

This is a 50% probability failure — literally coin-flip whether the arc works or degenerates, depending on which way the servo happens to overshoot.

The 250 mm test had been run repeatedly and happened to always work. At 83 mm, the failure manifested more visibly (a 1-tick snap is clearly wrong). After understanding this, the same bug was confirmed to be latent in the 250 mm case too.

---

## The Fix: Epsilon Guard in `normalise_arc_sweep()`

The fix uses an epsilon that is larger than the maximum position noise but smaller than any intentional arc:

```c
static float normalise_arc_sweep(float start, float end, uint8_t cw)
{
    /* For CW (G02): sweep must be negative (end < start).
     * For CCW (G03): sweep must be positive (end > start).
     *
     * Epsilon guard: arc_plan() reads the ACTUAL servo position as the arc
     * start point.  When start == end is commanded (full circle), the actual
     * position may be a few encoder counts ahead of the commanded end point,
     * making (end - start) a tiny negative value instead of 0.  Without the
     * epsilon the while-loop would not fire and sweep ≈ 0 → 1-tick arc that
     * draws nothing.  0.001 rad ≈ 0.06° is well below any intentional arc
     * yet 66× larger than the worst-case position noise at unit_scale=4000.  */
    float diff = end - start;
    if (cw) {
        while (diff >= -0.001f) diff -= INTERP_TWO_PI;
    } else {
        while (diff <=  0.001f) diff += INTERP_TWO_PI;
    }
    return diff;
}
```

Change: `while (diff >= 0.0f)` → `while (diff >= -0.001f)`

Verification of the epsilon choice:
- Max position error at `unit_scale=4000`, `TOLERANCE=5 counts`: `5/4000 = 0.00125 mm`
- At radius 83 mm: `theta_error = 0.00125/83 ≈ 1.5e-5 rad`
- Epsilon `0.001 rad` is **66× larger** than this — comfortably catches all noise
- Epsilon `0.001 rad ≈ 0.06°` corresponds to an arc of `0.001 × 83 = 0.083 mm`
- No real robot program commands a 0.083 mm arc deliberately
- Even at `unit_scale=40` counts/mm (extreme low resolution): max error = `5/40/83 ≈ 0.0015 rad` — still within the epsilon

---

## Before / After

| Scenario | Before Fix | After Fix |
|----------|-----------|-----------|
| Full circle, actual Y exactly at target | ✓ worked | ✓ works |
| Full circle, actual Y 0.001mm above target | ✗ 1-tick snap | ✓ full circle |
| Full circle, actual Y 0.001mm below target | ✓ worked (by luck) | ✓ full circle |
| Intentional 1° arc (0.017 rad) | ✓ worked | ✓ works (0.017 > 0.001) |
| Intentional 0.01° arc (0.00017 rad) | ✓ worked | ✗ treated as full circle |

The 0.01° case is a theoretically breaking change, but no practical G-code program for this robot would command an arc that small (it's 0.014 mm at 83 mm radius — smaller than a single encoder count).

---

## Reflection: The Shape of This Bug Class

This bug is representative of a whole family of floating-point edge cases in computational geometry:

> **"Exact" algorithms fail when the inputs have small errors.**

The `normalise_arc_sweep()` algorithm is *exactly correct* for exact inputs. But real-world inputs (servo encoder positions) are never exact — they have bounded noise. The algorithm must be designed to handle the noise, or the call site must sanitize inputs before calling it.

We chose to fix it at the call site would have required detecting "start ≈ end" before calling the normalization function, which is doable but more complex. Fixing it inside the normalizer with an epsilon is simpler and localized.

---

## Current Status and What's Next

After these three fixes, the system runs reliably:

- **G00**: runs at flash-saved velocity ✓
- **G01**: linear paths at correct feed rate ✓
- **G02/G03**: circles and arcs draw correctly ✓
- **Soft limits**: active during JOG/G00, bypassed during interpolated arcs ✓

**Remaining work:**

1. **G28 — Automatic homing sequence** — currently the user manually jogs to the home position and presses "Set Home Here". A proper G28 would drive toward home-sensor input and zero automatically.

2. **Alarm panel** — fault events, limit violations, and communication errors currently appear only in the debug log. A dedicated panel with timestamps and severity levels would improve operator feedback.

3. **EtherCAT slave board** — developing a custom compact servo drive (LAN9252 EtherCAT ASIC + STM32G4 running FOC) to replace the third-party drives.

---

## Series Summary

Twenty posts, one complete system. The full stack spans from EtherCAT physical frames at 100 Mbps → CiA402 state machine → CSP position control → velocity ramp → G-code interpreter → SLIP binary protocol → asyncio Python bridge → WebSocket JSON → vanilla JavaScript HMI.

Every layer was written from scratch or minimally adapted. The result is a system where every decision is traceable to a reason — not "because the library does it that way."

The code is messy in places, the bug history is embarrassing in places, and there are features that could be done better. That's what a real development log looks like.

---

*Thank you for following along. If you're building something similar and hit a wall, the bug posts (18, 19, 20) are probably the most useful — those problems will find you eventually.*
