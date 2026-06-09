# G-code Interpreter Part 1 — G00 Rapid Move

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 12 of 20  
**Tags:** G-code, Interpolator, G00, FreeRTOS, Embedded C

---

## The Interpolator Architecture

All motion commands — G00, G01, G02, G03 — route through a single module: `interpolator.c`. This module:

- Accepts commands from `DefaultTask` (UART dispatch context)
- Executes motion in `EtherCAT_Task` (1 ms realtime context)
- Uses a lock-free request buffer with a `__DMB()` memory barrier for cross-task communication

The state machine:

```
IDLE ──→ MOVING ──→ DONE ──→ IDLE
                └──→ ERROR
```

- **IDLE**: no motion, accepts new commands
- **MOVING**: executing a move
- **DONE**: move complete — caller must call `Interp_AckDone()` to return to IDLE
- **ERROR**: planning failure (degenerate arc, etc.)

---

## Cross-Task Communication

The interpolator uses a single `g_req_pending` flag and a `g_req` struct — the volatile pending-request pattern:

```c
static InterpReq_t      g_req;
static volatile uint8_t g_req_pending = 0U;

/* Called from DefaultTask */
static uint8_t post_cmd(const InterpReq_t *req)
{
    /* Force IDLE so the new command is always accepted */
    g_state      = INTERP_IDLE;
    g_active_cmd = ICMD_NONE;
    g_req        = *req;
    __DMB();             /* data memory barrier: ensure g_req write is visible */
    g_req_pending = 1U;  /* signal EtherCAT_Task */
    return 1U;
}
```

`EtherCAT_Task` (in `Interp_Tick()`) polls `g_req_pending` every 1 ms:

```c
void Interp_Tick(void)
{
    if (g_req_pending != 0U) {
        InterpReq_t req = g_req;   /* copy before clearing */
        g_req_pending   = 0U;
        
        if (g_state != INTERP_MOVING) {
            switch (req.cmd) {
                case ICMD_G00: g00_plan(&req); break;
                case ICMD_G01: g01_plan(&req); break;
                case ICMD_G02: arc_plan(&req, 1U); break;
                case ICMD_G03: arc_plan(&req, 0U); break;
            }
        }
        return;   /* skip tick on the same cycle as plan */
    }
    
    /* Advance active move */
    if (g_state != INTERP_MOVING) return;
    switch (g_active_cmd) {
        case ICMD_G00: g00_tick(); break;
        case ICMD_G01: g01_tick(); break;
        case ICMD_G02:
        case ICMD_G03: arc_tick(); break;
    }
}
```

No RTOS semaphore, no mutex — just the `volatile` flag and a memory barrier. The invariant: `DefaultTask` writes `g_req` then sets the flag; `EtherCAT_Task` reads the flag, copies `g_req`, then clears the flag.

---

## G00 Planning

G00 (rapid move) is the simplest case. The target is set and the SOEM ramp generator does all the work:

```c
static void g00_plan(const InterpReq_t *req)
{
    for (uint8_t ax = 0; ax < AXIS_COUNT; ax++) {
        int32_t hw = (int32_t)mm_to_hw_f(ax, req->target_mm[ax]);
        SOEM_SetTargetHw((AxisId_t)ax, hw);
    }
    g_active_cmd = ICMD_G00;
    g_state      = INTERP_MOVING;
}
```

`SOEM_SetTargetHw()` sets `target_hw` and clears `interp_active`, enabling the velocity ramp generator. Each axis then independently ramps toward its target.

Note: G00 axes are NOT coordinated — X, Y, Z each finish at their own time, at their own `profile_velocity`. This is standard G-code behavior. G01/G02/G03 provide coordinated motion.

---

## G00 Completion Detection

Every 1 ms, `g00_tick()` checks whether all axes have reached their targets:

```c
static void g00_tick(void)
{
    for (uint8_t ax = 0; ax < AXIS_COUNT; ax++) {
        if (g_shadow[ax].target_reached == 0U) return;  /* still moving */
    }
    g_state = INTERP_DONE;
}
```

`target_reached` is set by `soem_update_target_output()` when the actual encoder position is within `ROBOT_TARGET_TOLERANCE_HW` (5 counts) of the final target.

---

## The DONE → IDLE Handshake

When `g_state == INTERP_DONE`, the STATUS telemetry carries `interp_state = DONE`. The HMI sees this and immediately sends `PKT_INTERP_ACK`:

```javascript
function checkInterpDone(msg) {
    if (msg.interp === 'DONE' && !_ackSent) {
        _ackSent = true;
        send({ cmd: 'interp_ack' });
    }
}
```

On the firmware side, `PKT_INTERP_ACK` calls `Interp_AckDone()`:

```c
void Interp_AckDone(void)
{
    if (g_state == INTERP_DONE) {
        g_state      = INTERP_IDLE;
        g_active_cmd = ICMD_NONE;
    }
}
```

**Critical timing rule** (learned the hard way): the ACK must be sent when DONE is *first detected*, not after waiting for IDLE. If you wait for the state to already be IDLE before sending the ACK, a race condition prevents the transition from ever happening — DONE never gets the ACK that causes it to go IDLE. This deadlock took an afternoon to find. (Full story in Post 19.)

---

## Sequential Execution of Multi-Block Programs

For a G-code program with multiple lines, the HMI runs a sequential executor:

```javascript
async function runGcodeProgram(lines) {
    for (const line of lines) {
        const cmd = parseGcode(line);
        if (!cmd) continue;
        
        // Wait until interpolator is IDLE
        await waitForInterp('IDLE');
        
        // Send the command and wait for ACK
        const seq = send(cmd);
        await waitForAck(seq);
        
        // Wait for the move to complete (DONE)
        await waitForInterp('DONE');
        
        // Acknowledge completion (→ IDLE)
        send({ cmd: 'interp_ack' });
    }
}
```

`waitForInterp()` and `waitForAck()` use Promise-based wrappers around the WebSocket `onmessage` event. The entire multi-block program runs asynchronously without blocking the browser's UI thread.

---

## G00 in Practice

For the test program:
```
G00 X-981 Y-354 Z-50   ; rapid to start position
G02 X-981 Y-354 I-83 J0 F100  ; draw circle
G00 X0 Y0 Z0           ; rapid home
```

The G00 at `unit_scale=4000` and `profile_velocity=100 mm/s`:
- X: -981 mm × 4000 = -3,924,000 counts — takes 9.81 seconds at 100 mm/s
- Y: -354 mm × 4000 = -1,416,000 counts — takes 3.54 seconds
- Z: -50 mm × 4000 = -200,000 counts — takes 0.5 seconds

All three ramp independently. The G00 is complete when the last axis (X) reaches its target.

---

## Next Post

G00 is rapid but uncoordinated. Post 13 adds **G01 linear interpolation** — all axes move simultaneously and arrive at the target at exactly the same time, tracing a mathematically straight line.
