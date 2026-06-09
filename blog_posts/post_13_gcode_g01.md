# G-code Interpreter Part 2 — G01 Linear Interpolation

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 13 of 20  
**Tags:** G-code, Interpolation, G01, Linear, Embedded C

---

## What Makes G01 Different from G00

G00 moves each axis at its own maximum speed — axes arrive independently. For a diagonal move from (0,0) to (100,100), X and Y both ramp to their full speeds and each stops when it reaches its target. The path is a rough L-shape, not a diagonal line.

G01 (linear feed) coordinates all axes so they arrive simultaneously. The tool traces a geometrically straight line in 3D space at a specified feed rate. For (0,0) to (100,100) at 100 mm/s: X and Y both move at 70.7 mm/s (= 100/√2), arriving at (100,100) at exactly the same time.

---

## The Progress Variable Approach

Instead of computing per-axis velocities and managing synchronization separately, G01 uses a single scalar `progress` that advances from 0.0 to 1.0 over the duration of the move:

```c
typedef struct {
    float start_mm[AXIS_COUNT];
    float delta_mm[AXIS_COUNT];
    float progress;        /* 0.0 → 1.0 */
    float d_progress;      /* increment per 1 ms tick */
    float target_mm[AXIS_COUNT];
} LinearState_t;
```

Each tick: `progress += d_progress`, then `pos[ax] = start[ax] + progress * delta[ax]`.

This is elegant because:
- Synchronization is automatic — all axes share the same `progress`
- The path is mathematically exact — no accumulated floating-point error
- Feed rate is trivially controlled — just change `d_progress`

---

## G01 Planning

```c
static void g01_plan(const InterpReq_t *req)
{
    float delta_sq = 0.0f;
    
    for (uint8_t ax = 0; ax < AXIS_COUNT; ax++) {
        g_lin.start_mm[ax]  = hw_to_mm_f(ax, (int32_t)g_shadow[ax].pos_hw);
        g_lin.target_mm[ax] = req->target_mm[ax];
        g_lin.delta_mm[ax]  = req->target_mm[ax] - g_lin.start_mm[ax];
        delta_sq += g_lin.delta_mm[ax] * g_lin.delta_mm[ax];
    }
    
    float L = sqrtf(delta_sq);   /* path length in mm */
    
    if (L < INTERP_MIN_LENGTH_MM) {
        /* Already at target */
        for (uint8_t ax = 0; ax < AXIS_COUNT; ax++) {
            SOEM_SetInterpolatedTarget((AxisId_t)ax,
                (int32_t)mm_to_hw_f(ax, req->target_mm[ax]));
        }
        g_state = INTERP_DONE;
        return;
    }
    
    float feed = (req->feed_mm_s > INTERP_MIN_FEED_MM_S)
                 ? req->feed_mm_s : INTERP_MIN_FEED_MM_S;
    
    /* d_progress = how much path is covered per 1 ms tick
     * = feed_mm_s / (L_mm * 1000_ms_per_s) */
    g_lin.d_progress = feed / (L * 1000.0f);
    g_lin.progress   = 0.0f;
    
    g_active_cmd = ICMD_G01;
    g_state      = INTERP_MOVING;
}
```

Example: move from (0,0,0) to (100,100,0) at 100 mm/s:
- L = √(100² + 100²) = 141.42 mm
- d_progress = 100 / (141.42 × 1000) = 0.000707 per tick
- Total ticks: 1 / 0.000707 = 1414 ticks = 1.414 seconds

---

## G01 Tick

```c
static void g01_tick(void)
{
    g_lin.progress += g_lin.d_progress;
    
    if (g_lin.progress >= 1.0f) {
        /* Snap to exact target — eliminates float accumulation error */
        for (uint8_t ax = 0; ax < AXIS_COUNT; ax++) {
            int32_t hw = (int32_t)mm_to_hw_f(ax, g_lin.target_mm[ax]);
            SOEM_SetInterpolatedTarget((AxisId_t)ax, hw);
        }
        g_state = INTERP_DONE;
        return;
    }
    
    for (uint8_t ax = 0; ax < AXIS_COUNT; ax++) {
        float   mm = g_lin.start_mm[ax] + g_lin.progress * g_lin.delta_mm[ax];
        int32_t hw = (int32_t)mm_to_hw_f(ax, mm);
        SOEM_SetInterpolatedTarget((AxisId_t)ax, hw);
    }
}
```

The "snap to exact target" on the last tick is important. After 1414 ticks of floating-point accumulation, `progress` might be 1.0000001 or 0.9999999. Without the snap, the final position could be off by ±1 encoder count. With it, the endpoint is always exact.

---

## `SOEM_SetInterpolatedTarget()` — Bypassing the Ramp

G01 (and G02/G03) use `SOEM_SetInterpolatedTarget()` instead of `SOEM_SetTargetHw()`. The difference:

```c
/* For G00 / JOG — ramp generator controls output */
void SOEM_SetTargetHw(AxisId_t ax, int32_t hw)
{
    g_rt[ax].interp_active = 0U;
    g_rt[ax].target_hw     = axis_clamp_hw(ax, hw);
    /* target_hw_out is NOT changed — ramp will move toward target_hw */
}

/* For G01/G02/G03 — interpolator directly controls output */
void SOEM_SetInterpolatedTarget(AxisId_t ax, int32_t hw)
{
    g_rt[ax].interp_active  = 1U;
    g_rt[ax].target_hw      = hw;
    g_rt[ax].target_hw_out  = hw;   /* immediate — no ramp delay */
}
```

With `interp_active = 1`, the ramp generator in `soem_update_target_output()` sees `output == cmd` and returns immediately — the interpolator's position is used directly as the PDO output without any velocity filtering.

This is correct for G01/G02/G03: the *interpolator* is providing the smooth trajectory. The ramp would just add unnecessary lag.

---

## Feed Rate Units

The G-code feed rate (F word) is interpreted as **mm/s** throughout this implementation. Standard CNC G-code uses mm/min, so `F100` here means 100 mm/s = 6000 mm/min. For this robot with typical axis speeds of 10–500 mm/s, mm/s is more natural and avoids large F numbers.

If you want standard CNC compatibility, divide F by 60 in the parser:
```javascript
const f = (words['F'] ?? 50.0) / 60.0;  // convert mm/min to mm/s
```

---

## Float Precision

The STM32H753 has a hardware double-precision FPU. The interpolator uses single-precision float (`float`) throughout. At `unit_scale = 4000 counts/mm` and workspace of ±2000 mm:

- Maximum HW coordinate: 2000 × 4000 = 8,000,000 counts
- Float mantissa: 23 bits = 2^23 = 8,388,608 representable integers
- Precision at 8M counts: 1 count (exact representation possible)

So single-precision float is adequate for this machine. Double would be needed for larger-travel machines with higher resolution encoders.

---

## Next Post

Linear interpolation draws straight lines. The next post adds **G02/G03 arc interpolation** using a rotation matrix approach that requires no trigonometry per tick — just two multiplications and two additions.
