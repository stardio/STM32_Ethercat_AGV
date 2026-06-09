# G-code Interpreter Part 3 — G02/G03 Arc Interpolation

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 14 of 20  
**Tags:** G-code, Arc Interpolation, G02, G03, Rotation Matrix, Embedded C

---

## The Challenge of Arc Interpolation

A circle of radius R centered at (cx, cy) sweeps from angle θ_start to θ_end. Computing the position at angle θ:

```
x = cx + R·cos(θ)
y = cy + R·sin(θ)
```

The naive approach: call `cosf()` and `sinf()` every 1 ms tick. For radius 83 mm at feed 100 mm/s:
- 5215 ticks for a full circle
- 5215 × `cosf()` + 5215 × `sinf()` per revolution

On a Cortex-M7 with hardware FPU, each `cosf()` takes roughly 20–30 clock cycles. At 5215 calls per second: 5215 × 30 / 480,000,000 = 0.33 µs — negligible. The naive approach would work fine.

But there's a more elegant solution that also avoids accumulating floating-point angle error: the **rotation matrix method**.

---

## Rotation Matrix Method

Instead of computing the angle each tick, we rotate the current position vector by a fixed small angle `d_theta` each tick.

```
[new_a]   [cos(d_θ)  -sin(d_θ)] [rel_a]
[new_b] = [sin(d_θ)   cos(d_θ)] [rel_b]
```

Pre-compute `cos_dt = cos(d_theta)` and `sin_dt = sin(d_theta)` once at planning time. Each tick:

```c
float new_a = g_arc.rel_a * g_arc.cos_dt - g_arc.rel_b * g_arc.sin_dt;
float new_b = g_arc.rel_a * g_arc.sin_dt + g_arc.rel_b * g_arc.cos_dt;
g_arc.rel_a = new_a;
g_arc.rel_b = new_b;
```

Just 4 multiplications and 2 additions per tick — no trig in the hot path. The only trig cost is the two `cosf()`/`sinf()` calls at planning time.

---

## Arc State

```c
typedef struct {
    uint8_t  ax_a;        /* first plane axis (e.g., X=0) */
    uint8_t  ax_b;        /* second plane axis (e.g., Y=1) */
    uint8_t  ax_c;        /* passthrough axis (e.g., Z=2) */
    float    cx_mm;       /* arc center, axis A */
    float    cy_mm;       /* arc center, axis B */
    float    rel_a;       /* current position vector from center, A component */
    float    rel_b;       /* current position vector from center, B component */
    float    cos_dt;      /* rotation matrix element */
    float    sin_dt;      /* rotation matrix element */
    float    start_c_mm;  /* passthrough axis start */
    float    delta_c_mm;  /* passthrough axis total delta */
    float    end_a_mm;    /* exact end, axis A (for snap) */
    float    end_b_mm;    /* exact end, axis B (for snap) */
    float    end_c_mm;    /* exact end, axis C (for snap) */
    uint32_t ticks_total;
    uint32_t ticks_done;
} ArcState_t;
```

---

## Arc Planning

```c
static void arc_plan(const InterpReq_t *req, uint8_t cw)
{
    float start_mm[AXIS_COUNT];
    for (uint8_t ax = 0; ax < AXIS_COUNT; ax++)
        start_mm[ax] = hw_to_mm_f(ax, (int32_t)g_shadow[ax].pos_hw);

    uint8_t pa, pb, pc;
    plane_axes(req->plane, &pa, &pb, &pc);  /* XY, XZ, or YZ plane */

    /* Center = start + I/J/K offset */
    g_arc.cx_mm = start_mm[pa] + req->ijk_mm[pa];
    g_arc.cy_mm = start_mm[pb] + req->ijk_mm[pb];

    /* Start vector from center */
    g_arc.rel_a = start_mm[pa] - g_arc.cx_mm;   /* = -I */
    g_arc.rel_b = start_mm[pb] - g_arc.cy_mm;   /* = -J */

    float radius = sqrtf(g_arc.rel_a * g_arc.rel_a
                       + g_arc.rel_b * g_arc.rel_b);
    if (radius < INTERP_MIN_RADIUS_MM) { g_state = INTERP_ERROR; return; }

    /* End vector from center */
    float end_a_rel = req->target_mm[pa] - g_arc.cx_mm;
    float end_b_rel = req->target_mm[pb] - g_arc.cy_mm;

    float theta_start = atan2f(g_arc.rel_b, g_arc.rel_a);
    float theta_end   = atan2f(end_b_rel, end_a_rel);
    float sweep       = normalise_arc_sweep(theta_start, theta_end, cw);

    float feed       = (req->feed_mm_s > INTERP_MIN_FEED_MM_S)
                       ? req->feed_mm_s : INTERP_MIN_FEED_MM_S;
    float arc_len_mm = radius * fabsf(sweep);
    float ticks_f    = (arc_len_mm / feed) * 1000.0f;

    g_arc.ticks_total = (ticks_f >= 1.0f) ? (uint32_t)ticks_f : 1U;
    g_arc.ticks_done  = 0U;

    float d_theta   = sweep / (float)g_arc.ticks_total;
    g_arc.cos_dt    = cosf(d_theta);
    g_arc.sin_dt    = sinf(d_theta);

    /* Passthrough axis */
    g_arc.start_c_mm = start_mm[pc];
    g_arc.delta_c_mm = req->target_mm[pc] - start_mm[pc];

    /* Snap targets */
    g_arc.end_a_mm = req->target_mm[pa];
    g_arc.end_b_mm = req->target_mm[pb];
    g_arc.end_c_mm = req->target_mm[pc];
    g_arc.ax_a = pa; g_arc.ax_b = pb; g_arc.ax_c = pc;

    g_active_cmd = cw ? ICMD_G02 : ICMD_G03;
    g_state      = INTERP_MOVING;
}
```

---

## Arc Sweep Normalisation

For G02 (CW), the sweep must be negative (going from `theta_start` toward smaller angles). For G03 (CCW), it must be positive.

```c
static float normalise_arc_sweep(float start, float end, uint8_t cw)
{
    float diff = end - start;
    if (cw) {
        /* Guard: tiny negative diff from position noise looks like full circle */
        while (diff >= -0.001f) diff -= INTERP_TWO_PI;
    } else {
        while (diff <=  0.001f) diff += INTERP_TWO_PI;
    }
    return diff;
}
```

The `>= -0.001f` guard (instead of `>= 0.0f`) is critical for full-circle arcs (start == end). See Post 20 for the full story — this single epsilon value was responsible for two debugging sessions.

---

## Arc Tick

```c
static void arc_tick(void)
{
    /* Rotate position vector by d_theta */
    float new_a = g_arc.rel_a * g_arc.cos_dt - g_arc.rel_b * g_arc.sin_dt;
    float new_b = g_arc.rel_a * g_arc.sin_dt + g_arc.rel_b * g_arc.cos_dt;
    g_arc.rel_a = new_a;
    g_arc.rel_b = new_b;
    g_arc.ticks_done++;

    float frac = (float)g_arc.ticks_done / (float)g_arc.ticks_total;
    float c_mm = g_arc.start_c_mm + frac * g_arc.delta_c_mm;

    if (g_arc.ticks_done >= g_arc.ticks_total) {
        /* Last tick: snap to exact endpoint */
        SOEM_SetInterpolatedTarget(g_arc.ax_a, (int32_t)mm_to_hw_f(g_arc.ax_a, g_arc.end_a_mm));
        SOEM_SetInterpolatedTarget(g_arc.ax_b, (int32_t)mm_to_hw_f(g_arc.ax_b, g_arc.end_b_mm));
        SOEM_SetInterpolatedTarget(g_arc.ax_c, (int32_t)mm_to_hw_f(g_arc.ax_c, g_arc.end_c_mm));
        g_state = INTERP_DONE;
        return;
    }

    float a_mm = g_arc.cx_mm + g_arc.rel_a;
    float b_mm = g_arc.cy_mm + g_arc.rel_b;

    SOEM_SetInterpolatedTarget(g_arc.ax_a, (int32_t)mm_to_hw_f(g_arc.ax_a, a_mm));
    SOEM_SetInterpolatedTarget(g_arc.ax_b, (int32_t)mm_to_hw_f(g_arc.ax_b, b_mm));
    SOEM_SetInterpolatedTarget(g_arc.ax_c, (int32_t)mm_to_hw_f(g_arc.ax_c, c_mm));
}
```

---

## Example: Full Circle at 83 mm Radius

G-code: `G02 X-981 Y-354 I-83 J0 F100`

Start: `(-981, -354, -50)`, End: same (full circle)
Center offset: `I=-83, J=0` → Center: `(-1064, -354)`

Planning:
- `rel_a = 83`, `rel_b = 0` (start vector from center)
- `radius = 83 mm`
- `theta_start = atan2(0, 83) = 0 rad`
- `theta_end = atan2(0, 83) = 0 rad`
- `sweep = normalise_arc_sweep(0, 0, CW) = -2π`
- `arc_len = 83 × 2π = 521.5 mm`
- `ticks_total = 521.5 / 100 × 1000 = 5215 ticks`
- `d_theta = -2π / 5215 = -0.001205 rad`
- `cos_dt = cos(-0.001205) ≈ 0.9999993`
- `sin_dt = sin(-0.001205) ≈ -0.001205`

After tick 1:
- `new_a = 83 × 0.9999993 - 0 × (-0.001205) ≈ 82.99994`
- `new_b = 83 × (-0.001205) + 0 × 0.9999993 = -0.10002`
- Position: `(-1064 + 82.99994, -354 + (-0.10002)) = (-981.00006, -354.10002)` ✓

The robot traces a 521.5 mm circle in 5.215 seconds.

---

## Plane Selection

Arcs can operate in three planes:

| Plane | `pa` | `pb` | `pc` (passthrough) |
|-------|------|------|--------------------|
| XY (default) | X=0 | Y=1 | Z=2 |
| XZ | X=0 | Z=2 | Y=1 |
| YZ | Y=1 | Z=2 | X=0 |

The Web HMI currently always uses XY (`plane=0`). XZ and YZ are available for 3D arc paths.

---

## Next Post

With the interpolator complete, the next post covers persistent storage: **Flash parameter saving** — storing machine configuration in internal Flash so it survives power cycles, using STM32's dual-bank Flash for safe writes.
