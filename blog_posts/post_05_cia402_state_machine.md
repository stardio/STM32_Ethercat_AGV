# CiA402 State Machine — Bringing Servo Drives Online

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 5 of 20  
**Tags:** CiA402, Servo, State Machine, EtherCAT, Motion Control

---

## What is CiA402?

**CiA402** (CANopen device profile for drives and motion control) is the international standard that defines how servo drives expose their interface over any fieldbus — including EtherCAT. It specifies:

- A **state machine** with defined transitions controlled by a `Controlword` (0x6040)
- A **status reporting** mechanism via `Statusword` (0x6041)
- A set of **operating modes**: Profile Position, Profile Velocity, Profile Torque, Cyclic Synchronous Position (CSP), and more

Because CiA402 is vendor-neutral, the same drive engagement sequence works with Panasonic, Yaskawa, Beckhoff, Omron, Delta, and every other compliant brand.

---

## The State Machine

```
POWER ON
    │
    ▼
NOT READY TO SWITCH ON  (drive initializing)
    │
    ▼
SWITCH ON DISABLED      (init complete, waiting for enable)
    │  Controlword bit 3 = 0, bit 2 = 1  (Quick Stop)
    ▼
READY TO SWITCH ON      (safe to enable)
    │  Controlword 0x0006  (Switch On)
    ▼
SWITCHED ON             (power stage on, no motion)
    │  Controlword 0x000F  (Enable Operation)
    ▼
OPERATION ENABLED  ←────── normal operating state
    │  (fault occurs)
    ▼
FAULT REACTION ACTIVE
    │
    ▼
FAULT               ←──────────── recover with 0x0080 (Fault Reset)
```

The state is encoded in specific bits of the Statusword. The transitions are triggered by writing specific bit patterns to the Controlword.

---

## Reading the Current State

```c
typedef enum {
    CIA402_NOT_READY    = 0,
    CIA402_SW_DISABLED  = 1,
    CIA402_READY        = 2,
    CIA402_SWITCHED_ON  = 3,
    CIA402_OP_ENABLED   = 4,
    CIA402_QUICK_STOP   = 5,
    CIA402_FAULT_REACT  = 6,
    CIA402_FAULT        = 7,
    CIA402_UNKNOWN      = 255,
} Cia402State_t;

static Cia402State_t decode_cia402(uint16_t sw)
{
    /* CiA402 Table 30 — Statusword bit pattern decoding */
    if      ((sw & 0x004F) == 0x0000) return CIA402_NOT_READY;
    else if ((sw & 0x004F) == 0x0040) return CIA402_SW_DISABLED;
    else if ((sw & 0x006F) == 0x0021) return CIA402_READY;
    else if ((sw & 0x006F) == 0x0023) return CIA402_SWITCHED_ON;
    else if ((sw & 0x006F) == 0x0027) return CIA402_OP_ENABLED;
    else if ((sw & 0x006F) == 0x0007) return CIA402_QUICK_STOP;
    else if ((sw & 0x004F) == 0x000F) return CIA402_FAULT_REACT;
    else if ((sw & 0x004F) == 0x0008) return CIA402_FAULT;
    return CIA402_UNKNOWN;
}
```

---

## The Firmware State Machine

We run a software FSM that drives the CiA402 Controlword each 1 ms cycle. The transitions respect minimum dwell times to avoid overwhelming the drive with rapid commands.

```c
typedef enum {
    CIA402_STAGE_INIT        = 0,
    CIA402_STAGE_RESET_FAULT = 1,
    CIA402_STAGE_ENABLE      = 2,
    CIA402_STAGE_OP_ENABLED  = 3,
    CIA402_STAGE_DISABLED    = 4,
} Cia402Stage_t;

static void cia402_fsm(uint8_t ax)
{
    uint16_t sw    = g_shadow[ax].statusword;
    Cia402State_t state = decode_cia402(sw);

    switch (g_rt[ax].cia402_stage) {

    case CIA402_STAGE_INIT:
        if (state == CIA402_FAULT)
            g_rt[ax].cia402_stage = CIA402_STAGE_RESET_FAULT;
        else if (state == CIA402_SW_DISABLED || state == CIA402_READY
              || state == CIA402_SWITCHED_ON)
            g_rt[ax].cia402_stage = CIA402_STAGE_ENABLE;
        break;

    case CIA402_STAGE_RESET_FAULT:
        /* Write Fault Reset bit, then clear it */
        g_rt[ax].controlword = 0x0080U;
        if (state != CIA402_FAULT)
            g_rt[ax].cia402_stage = CIA402_STAGE_ENABLE;
        break;

    case CIA402_STAGE_ENABLE:
        switch (state) {
        case CIA402_SW_DISABLED:
            g_rt[ax].controlword = 0x0006U;   /* Shutdown */
            break;
        case CIA402_READY:
            g_rt[ax].controlword = 0x0007U;   /* Switch On */
            break;
        case CIA402_SWITCHED_ON:
            g_rt[ax].controlword = 0x000FU;   /* Enable Operation */
            break;
        case CIA402_OP_ENABLED:
            g_rt[ax].cia402_stage = CIA402_STAGE_OP_ENABLED;
            latch_target_to_actual(ax);        /* Freeze target at current pos */
            break;
        default:
            break;
        }
        break;

    case CIA402_STAGE_OP_ENABLED:
        if (state == CIA402_FAULT || state == CIA402_FAULT_REACT)
            g_rt[ax].cia402_stage = CIA402_STAGE_RESET_FAULT;
        /* Normal operation — controlword keeps bit3=1 (Enable Operation) */
        /* Target position is updated by SOEM_SetTargetHw() / Interp_Tick() */
        break;

    case CIA402_STAGE_DISABLED:
        g_rt[ax].controlword = 0x0006U;   /* Shutdown */
        break;
    }
}
```

The key detail: when we first transition to `OP_ENABLED`, we call `latch_target_to_actual()`. This sets the commanded target position to the *current actual position* so the first PDO write doesn't command a sudden jump to 0 or some stale value.

---

## Fault Recovery

Drives fault for many reasons: over-current, over-temperature, position following error, encoder error. The recovery sequence is:

1. Detect `CIA402_FAULT` in Statusword
2. Write `0x0080` to Controlword (Fault Reset bit — rising edge triggered)
3. Clear Controlword back to `0x0000` after one cycle
4. Re-run the enable sequence

This happens automatically in the firmware FSM without any user action, unless the fault recurs.

---

## Run Enable from the HMI

The Web HMI has a Run Enable toggle button that sends a `PKT_RUN_ENABLE` packet. In firmware:

```c
case PROTO_PKT_RUN_ENABLE:
    if (cmd.enable) {
        g_rt[ax].cia402_stage = CIA402_STAGE_ENABLE;
    } else {
        g_rt[ax].cia402_stage = CIA402_STAGE_DISABLED;
        latch_target_to_actual(ax);
    }
    break;
```

Disabling latches the target to prevent a surprise move when re-enabling.

---

## SDO Gate During Operation

Once in `OP_ENABLED`, we limit SDO writes to windows when the axis is stationary. Sending an SDO while the drive is moving can cause a brief pause in PDO exchange, which the drive interprets as a communication error and triggers a fault.

```c
static uint8_t allow_sdo(uint8_t ax)
{
    /* Only allow SDO when velocity is near zero AND position is settled */
    int32_t vel = abs((int32_t)g_shadow[ax].velocity);
    int64_t err = abs((int64_t)g_rt[ax].target_hw_out
                    - (int64_t)g_shadow[ax].pos_hw);

    if (vel <= 1 && err <= ROBOT_TARGET_TOLERANCE_HW) {
        if (++g_rt[ax].stable_cycles >= ROBOT_SDO_STABLE_CYCLES_MIN)
            return 1U;
    } else {
        g_rt[ax].stable_cycles = 0U;
    }
    return 0U;
}
```

SDO writes for parameter updates (position gain, velocity limit, etc.) queue up and execute the next time `allow_sdo()` returns true.

---

## Next Post

With drives online and the state machine running, the next post covers the actual motion control: **Cyclic Synchronous Position mode** — how we generate position setpoints every 1 ms and the software velocity ramp that limits acceleration.
