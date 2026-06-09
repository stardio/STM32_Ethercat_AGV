# Hardware Selection — STM32H753ZI, NUCLEO Board, and Servo Drives

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 2 of 20  
**Tags:** STM32H753, EtherCAT, Hardware, NUCLEO

---

## The Central Question: Which MCU?

EtherCAT is demanding. The master needs to send and receive PDO frames every 1 ms with microsecond-level jitter. That means:

1. A CPU fast enough to run SOEM's Ethernet polling plus your control loop in well under 1 ms
2. A hardware Ethernet MAC (software bit-banging Ethernet doesn't work for EtherCAT)
3. Enough RAM to hold PDO buffers, SOEM internal state, FreeRTOS stacks, and your application data

The **STM32H753ZI** hits all three:

| Spec | Value |
|------|-------|
| Core | ARM Cortex-M7 |
| Clock | 480 MHz (with FPU, DSP extensions) |
| Flash | 2 MB (dual bank) |
| RAM | 1 MB (DTCM + AXI SRAM + SRAM1/2/3/4) |
| Ethernet | 10/100 Mbps MAC with DMA |
| FPU | Double-precision hardware FPU (fpv5-d16) |

The hardware FPU is especially important. The arc interpolator computes `cosf()`/`sinf()` once at planning time, and does matrix-multiply operations every millisecond. Software float would eat the entire cycle budget.

---

## The NUCLEO-H753ZI Development Board

Rather than designing a custom PCB for a proof-of-concept, I used the **STM32 NUCLEO-H753ZI**. It exposes:
- The Ethernet interface (via RJ45 with built-in magnetics)
- All GPIO headers (Arduino Uno R3 + ST Morpho connectors)
- On-board ST-LINK V3 for programming and SWD debug
- USB-UART bridge (USART3 → virtual COM port) — used for the host protocol

The Ethernet port connects directly to the first EtherCAT slave's IN port. Slaves are daisy-chained: each has an IN and an OUT port. No Ethernet switch is needed or wanted — EtherCAT works on a dedicated segment.

---

## EtherCAT Slave Selection

Almost any industrial servo drive sold in the last decade supports EtherCAT, but for clean integration you want:

- **CiA402 profile** — the standard motion control object dictionary
- **CSP (Cyclic Synchronous Position) mode** — the drive accepts a position setpoint each cycle and handles its own inner control loop
- Configurable PDO mapping (so you can trim the process data to exactly what you need)

The drives I used expose these objects in the default PDO mapping:

**RxPDO (master → drive, written each cycle):**
- `0x607A` — Target Position (int32)
- `0x6040` — Controlword (uint16)
- `0x60FF` — Target Velocity (int32) — used for CSP feed-forward
- `0x6071` — Target Torque (int16)

**TxPDO (drive → master, read each cycle):**
- `0x6064` — Actual Position (int32)
- `0x6041` — Statusword (uint16)
- `0x606C` — Actual Velocity (int32)
- `0x6077` — Actual Torque (int16)

---

## Memory Layout Strategy

The STM32H753 has several RAM regions with different access characteristics:

```
DTCM (128 KB)   — Tightly coupled to M7 core, zero wait state
                  → FreeRTOS stacks, time-critical variables
AXI SRAM (512 KB) → SOEM working memory, PDO buffers
SRAM1 (128 KB)   → application data, g_axis_param[], g_rt[], g_shadow[]
SRAM2 (128 KB)   → UART ring buffers, interpolator state
```

The linker script places the EtherCAT PDO input/output buffers in AXI SRAM where the Ethernet DMA can reach them, and the FreeRTOS TCBs/stacks in DTCM for minimum context-switch overhead.

---

## Clocking

The system clock is configured at 480 MHz using the internal PLL. The Ethernet peripheral requires a separate 25 MHz reference (from an external crystal on the NUCLEO board) for the PHY.

FreeRTOS uses the SysTick timer at 1 kHz for task scheduling. The EtherCAT 1 ms task uses a software timer derived from SysTick — calling `ec_send_processdata()` and `ec_receive_processdata()` inside the task ensures PDO exchange happens at exactly the right time.

---

## Why Not Use an Industrial EtherCAT ASIC on the Master?

Many commercial EtherCAT master implementations use an LAN9252 or similar ASIC to offload EtherCAT DLL processing. With SOEM on a fast MCU you don't need this — SOEM implements the EtherCAT master stack entirely in software on top of a standard Ethernet MAC. The result is simpler hardware and full control over the timing.

The tradeoff: SOEM handles the MAC in polling mode (not interrupt-driven), so you burn some CPU cycles in `ec_receive_processdata()`. On a 480 MHz Cortex-M7 that cost is negligible for a 3-axis machine.

---

## Next Post

With hardware selected, the next post digs into EtherCAT itself — the protocol structure, why it is so much better suited to real-time motion control than standard Ethernet, and how SOEM maps the standard to an API you can call from embedded C.
