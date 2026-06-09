# Multi-Axis API Design — Runtime State, Shadows, and Concurrency

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 7 of 20  
**Tags:** Multi-Axis, Concurrency, FreeRTOS, Architecture, Embedded C

---

## The Challenge of Two Tasks

The firmware runs two FreeRTOS tasks that both need axis data:

- **`EtherCAT_Task`** (1 ms, highest priority) — reads encoder positions, writes PDO targets, runs the CiA402 FSM and interpolator
- **`DefaultTask`** (10 ms, low priority) — receives UART commands, sends status telemetry, manages flash saves

These tasks share three distinct data structures per axis. Getting the ownership model right prevents data corruption without expensive locking.

---

## Three Layers of Axis Data

### Layer 1: `AxisParam_t` — Configuration Parameters

```c
typedef struct {
    int32_t  unit_scale;         /* encoder counts / mm */
    int32_t  home_offset;        /* encoder count at user zero */
    int32_t  limit_plus_hw;      /* soft limit + (abs encoder counts) */
    int32_t  limit_minus_hw;     /* soft limit − (abs encoder counts) */
    int32_t  limit_plus_user;    /* soft limit + (mm, cached) */
    int32_t  limit_minus_user;   /* soft limit − (mm, cached) */
    int32_t  profile_velocity;   /* mm/s */
    int32_t  profile_accel_ms;   /* ms from 0 to full velocity */
    int32_t  profile_decel_ms;   /* ms from full velocity to 0 */
    int16_t  torque_limit;       /* per-mille of rated torque */
    int32_t  position_gain;      /* Kp for position loop (SDO) */
    uint8_t  limits_enabled;
    uint8_t  limits_blocked;
} AxisParam_t;

extern AxisParam_t g_axis_param[AXIS_COUNT];
```

Ownership: **DefaultTask** reads/writes via UART commands. **EtherCAT_Task** reads (never writes). No lock needed because DefaultTask writes are 32-bit aligned and the EtherCAT_Task reads are tolerant of one-cycle-stale values.

### Layer 2: `AxisRuntime_t` — EtherCAT_Task Private State

```c
typedef struct {
    int32_t       target_hw;        /* desired final position (HW counts) */
    int32_t       target_hw_out;    /* currently commanded position (ramped) */
    int32_t       profile_velocity; /* copy from g_axis_param, in mm/s */
    int32_t       ramp_velocity;    /* override: JOG velocity (0 = use profile) */
    int32_t       profile_accel_ms;
    int32_t       profile_decel_ms;
    int16_t       torque_limit;
    uint16_t      controlword;
    Cia402Stage_t cia402_stage;
    uint16_t      stable_cycles;
    volatile uint8_t interp_active; /* 1 while interpolator controls this axis */
} AxisRuntime_t;

static AxisRuntime_t g_rt[AXIS_COUNT];
```

Ownership: **EtherCAT_Task only**. DefaultTask never writes to `g_rt` directly — it writes to `g_axis_param` and calls `SOEM_Set*()` functions which run in the EtherCAT_Task context on the next cycle.

### Layer 3: `AxisShadow_t` — Read-Only Mirror for DefaultTask

```c
typedef struct {
    volatile int32_t  pos_hw;          /* actual encoder position */
    volatile int32_t  velocity;        /* actual velocity (user units/s) */
    volatile int16_t  torque;          /* actual torque (per-mille) */
    volatile uint16_t statusword;      /* raw CiA402 statusword */
    volatile uint8_t  cia402_state;    /* decoded state enum */
    volatile uint8_t  target_reached;  /* 1 when within tolerance */
    volatile uint8_t  pdo_ready;       /* 1 when drive is in OP */
    volatile uint8_t  run_enable;      /* 1 when enabled by user */
} AxisShadow_t;

volatile AxisShadow_t g_shadow[AXIS_COUNT];
```

Ownership: **EtherCAT_Task writes, DefaultTask reads**. All fields are `volatile`. DefaultTask reads are non-atomic but individual 32-bit reads are atomic on Cortex-M7 with word-aligned access, and one-cycle-stale status data is acceptable for telemetry.

---

## Public API: The `SOEM_Set*()` Functions

DefaultTask commands motion by calling `SOEM_Set*()` functions. These write to `g_rt[]` fields that EtherCAT_Task reads — a one-writer/one-reader pattern that is safe without a mutex for the specific access patterns used:

```c
void SOEM_SetTargetHw(AxisId_t ax, int32_t hw)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_rt[ax].interp_active = 0U;
    g_rt[ax].target_hw     = axis_clamp_hw((uint8_t)ax, hw);
}

void SOEM_SetRampVelocity(AxisId_t ax, int32_t vel_mm_s)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_rt[ax].ramp_velocity    = vel_mm_s;
    g_axis_param[ax].profile_velocity = vel_mm_s;  /* keep param in sync */
}

void SOEM_SetInterpolatedTarget(AxisId_t ax, int32_t hw)
{
    if (!axis_valid((uint8_t)ax)) return;
    g_rt[ax].interp_active  = 1U;
    g_rt[ax].target_hw      = hw;
    g_rt[ax].target_hw_out  = hw;  /* bypass ramp */
}
```

`interp_active` distinguishes between two operation modes:
- `interp_active = 0` → ramp generator controls `target_hw_out` (for G00, JOG)
- `interp_active = 1` → interpolator directly sets `target_hw_out` (for G01/G02/G03)

---

## Why No Mutex?

FreeRTOS mutexes on Cortex-M7 are inexpensive (a few hundred nanoseconds), but taking a mutex in the 1 ms EtherCAT_Task creates priority inversion risk if DefaultTask holds the mutex during a long flash write. Instead:

1. `g_shadow[]` — EtherCAT_Task is the sole writer; DefaultTask reads are naturally race-tolerant (status display, not control)
2. `g_rt[]` — EtherCAT_Task is the sole reader of the fields that matter for real-time control; DefaultTask writes are one-shot (single store instruction for int32_t on a word-aligned address)
3. `g_axis_param[]` — Mutually exclusive write windows: DefaultTask writes during UART dispatch, EtherCAT_Task reads during SDO application (which has a stability gate)

For the few cases where a 32-bit value must be read-modify-written from DefaultTask (e.g., flash save trigger flag), we use `volatile uint8_t` with a single-byte atomic write.

---

## The `AXIS_COUNT` Constant

Everything is parameterized by `AXIS_COUNT = 3`. Adding a fourth axis means changing one constant and adding a slave to the EtherCAT chain — no algorithm changes needed.

```c
typedef enum {
    AXIS_X = 0,
    AXIS_Y = 1,
    AXIS_Z = 2,
    AXIS_COUNT = 3,
} AxisId_t;

static inline uint8_t axis_valid(uint8_t ax)
{
    return (ax < (uint8_t)AXIS_COUNT);
}
```

---

## Next Post

With the architecture in place, the next post builds the first user-facing feature: **JOG operation** — moving an axis by a fixed step or holding a button for continuous motion at a configurable speed multiplier.
