# JOG Operation — Interactive Axis Control

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 8 of 20  
**Tags:** JOG, Motion Control, Web HMI, UX, Embedded

---

## Two Kinds of JOG

A robot needs two JOG modes for manual positioning:

1. **Step JOG** — press a button, axis moves exactly N mm, stops
2. **Continuous JOG** — hold a button, axis moves at configured velocity; release, it stops immediately

Both are essential. Step JOG gives precise incremental positioning for machine setup. Continuous JOG gives natural "drive around the workspace" feel.

---

## Step JOG on the Firmware Side

Step JOG uses a `PKT_JOG` packet from the PC:

```c
typedef struct __attribute__((packed)) {
    uint8_t axis;        /* 0=X, 1=Y, 2=Z */
    int8_t  direction;   /* +1 or -1 */
    float   step_mm;     /* step size in mm */
} ProtoPktJog_t;
```

In the firmware dispatcher:

```c
case PROTO_PKT_JOG:
    float sign    = (cmd.direction > 0) ? 1.0f : -1.0f;
    float scale_f = (float)g_axis_param[cmd.axis].unit_scale;
    int32_t delta_hw = (int32_t)(sign * cmd.step_mm * scale_f);
    int32_t cur_hw   = SOEM_GetPositionHw((AxisId_t)cmd.axis);
    int32_t tgt_hw   = cur_hw + delta_hw;
    SOEM_SetTargetHw((AxisId_t)cmd.axis, tgt_hw);
    dispatch_ack(seq, PROTO_RESULT_OK);
    break;
```

`SOEM_SetTargetHw()` sets the final target. The velocity ramp generator moves `target_hw_out` toward it at `profile_velocity`. The move is complete when the ramp catches up and `target_reached` goes true.

Note the diagnostic log added for every JOG command:
```c
snprintf(dbg, sizeof(dbg),
    "[JOG] ax=%u dir=%d step=%.3f dHW=%ld curHW=%ld tgtHW=%ld scale=%ld vel=%ld",
    cmd.axis, cmd.direction, cmd.step_mm,
    delta_hw, cur_hw, tgt_hw,
    g_axis_param[cmd.axis].unit_scale,
    g_axis_param[cmd.axis].profile_velocity);
UartProto_SendLog(dbg);
```

This log saved debugging time repeatedly — whenever JOG behavior looked wrong, the first thing to check was `scale=` in the log. (More on scale bugs in Post 18.)

---

## Continuous JOG on the Firmware Side

Continuous JOG uses the `ramp_velocity` field. The Web HMI sends a `set_param` packet with `PARAM_RAMP_VEL` to start motion:

```c
/* JOG button pressed — set ramp velocity */
send({ cmd: 'set_param', axis, param_id: PARAM.RAMP_VEL, value: jogVel });
```

When `ramp_velocity > 0`, the `step_per_cycle()` function uses it instead of `profile_velocity`. Combined with the direction embedded in `target_hw` (set far out in the jog direction), the axis ramps up to `ramp_velocity` and holds it.

On button release:

```c
/* JOG button released — send JOG_STOP */
send({ cmd: 'jog_stop' });
```

`PKT_JOG_STOP` triggers `SOEM_JogStop()`:

```c
void SOEM_JogStop(void)
{
    for (uint8_t ax = 0U; ax < (uint8_t)AXIS_COUNT; ax++) {
        /* Snap target to current actual position — kills motion immediately */
        latch_target_to_actual(ax);
        g_rt[ax].ramp_velocity = 0U;
    }
}
```

`latch_target_to_actual()` sets `target_hw = actual_pos_hw`, so the ramp generator immediately sees `diff = 0` and stops commanding new positions. The servo decelerates at its internal rate (drive parameter) and stops.

---

## Velocity Scaling on the HMI Side

The Web HMI has a velocity multiplier selector: ×0.01, ×0.1, ×1, ×10. It multiplies the `profile_velocity` parameter to compute the JOG speed:

```javascript
const baseVel = pmLoaded[axis]?.profile_velocity ?? 10;
const jogVel  = Math.max(1, Math.round(baseVel * jogStep));
send({ cmd: 'set_param', axis, param_id: PARAM.RAMP_VEL, value: jogVel });
```

At ×10 with `profile_velocity = 100 mm/s`, JOG runs at 1000 mm/s. At ×0.01 it runs at 1 mm/s — useful for precise tool setup.

Why send `RAMP_VEL` instead of just using `profile_velocity`? Because `RAMP_VEL` is ephemeral — it's not saved to flash and resets to zero on JOG stop. `profile_velocity` is the persistent machine parameter. Keeping them separate means "fast JOG for positioning" doesn't permanently overwrite the carefully configured "G01 feed rate."

---

## 50 ms Hold Timer

The HMI uses a 50 ms repeat timer for continuous JOG. Each tick while the button is held:

```javascript
_jogTimer = setInterval(() => {
    send({ cmd: 'set_param', axis: _jogAxis,
           param_id: PARAM.RAMP_VEL, value: _jogVel });
}, 50);
```

Sending `RAMP_VEL` repeatedly rather than once ensures the firmware keeps the target far out in the jog direction. If the communication link drops for one packet, the next packet re-extends the target. If the link drops entirely, the last `RAMP_VEL` command was 50 ms ago — not ideal but acceptable for a local network.

A future improvement would use a timeout on the firmware side: if no JOG command arrives within 200 ms, automatically latch and stop.

---

## Step Size Presets

The HMI offers five step sizes: 0.001, 0.01, 0.1, 1, 10 mm. These map to:

| Step | Use Case |
|------|----------|
| 0.001 mm | Precision tool height setting |
| 0.01 mm | Fine positioning |
| 0.1 mm | Normal manual positioning |
| 1 mm | Coarse positioning |
| 10 mm | Large traverse |

The firmware handles fractional mm steps cleanly because the JOG calculation uses floating-point:
```c
int32_t delta_hw = (int32_t)(sign * cmd.step_mm * (float)unit_scale);
```

For a 0.001 mm step at unit_scale=4000: `delta_hw = 4` (4 encoder counts). Perfectly addressable.

---

## Next Post

JOG lets us move axes manually. The next post builds the communication layer that makes all of this possible: the **binary UART protocol** with SLIP framing and CRC16 checksumming — replacing the old ASCII command interface with something production-worthy.
