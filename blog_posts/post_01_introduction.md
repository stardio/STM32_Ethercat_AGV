# Building a 3-Axis EtherCAT Robot Controller from Scratch — Series Introduction

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 1 of 20  
**Tags:** EtherCAT, STM32, Motion Control, Robot, Embedded Systems

---

## Why I Started This Project

I wanted to build a real industrial-grade 3-axis Cartesian robot controller — not a toy, not a CNC shield on an Arduino, but something that talks EtherCAT at 1 kHz, respects CiA402 servo profiles, runs a proper G-code interpreter, and has a live web dashboard. The goal was to understand every layer of the stack, from the physical EtherCAT frame all the way to the browser UI.

This series documents that journey: hardware selection, firmware architecture, protocol design, web HMI, and every embarrassing bug I hit along the way.

---

## What We Are Building

A complete 3-axis Cartesian robot controller with:

- **EtherCAT master** on an STM32 microcontroller running at 1 ms cycle time
- **CiA402 compliant** servo drive control (the international standard profile used by virtually all industrial servo drives)
- **G-code interpreter** — G00 (rapid), G01 (linear), G02/G03 (arc CW/CCW)
- **Binary UART protocol** with SLIP framing and CRC16 checksumming
- **Python WebSocket bridge** — serial port ↔ browser
- **Web HMI** — live position dashboard, parameter editor, G-code editor with 3D path preview

```
┌─────────────────┐
│  Web Browser    │  HTML + JavaScript
│  (index.html)   │  G-code editor, 3D path monitor, DRO
└────────┬────────┘
         │ WebSocket  ws://localhost:8765
┌────────▼────────┐
│  Python Bridge  │  bridge.py — asyncio, websockets
│  (bridge.py)    │  JSON ↔ binary packet translation
└────────┬────────┘
         │ UART SLIP  921600 baud, COM port
┌────────▼────────┐
│  STM32H753ZI    │  Cortex-M7 @ 480 MHz, FreeRTOS
│  NUCLEO-H753ZI  │  EtherCAT_Task (1ms) + DefaultTask (10ms)
└────────┬────────┘
         │ EtherCAT  100 Mbps
┌────────▼────────┐
│  Servo Drive ×3 │  X / Y / Z axes
│  CiA402 CSP     │  Cyclic Synchronous Position mode
└─────────────────┘
```

---

## The Hardware Stack

| Component | Part | Notes |
|-----------|------|-------|
| MCU Board | NUCLEO-H753ZI | STM32H753ZI, Cortex-M7 @ 480 MHz |
| EtherCAT | SOEM library | Simple Open EtherCAT Master, MIT license |
| Servo drives | 3× CiA402-compatible | Any EtherCAT servo with CSP support |
| UART to PC | On-board USB-UART | 921600 baud |

The STM32H753ZI has a hardware Ethernet MAC that SOEM uses directly — no separate EtherCAT ASIC needed on the master side. The slave drives do need an EtherCAT ASIC, but that's their problem.

---

## The Software Stack

**Firmware (C, STM32):**
- FreeRTOS — two tasks: `EtherCAT_Task` (1 ms, realtime), `DefaultTask` (10 ms, housekeeping)
- SOEM — EtherCAT process data (PDO) poll every 1 ms
- Custom modules: `soem_port`, `interpolator`, `uart_protocol`, `axis_config`, `settings_persistence`

**Host (Python, Browser):**
- `bridge.py` — asyncio, websockets library
- `slip_codec.py` — SLIP encode/decode
- `packet_defs.py` — binary packet builder/parser
- `index.html` — vanilla HTML/CSS/JS, no framework, no build step

---

## Series Outline

| Post | Topic |
|------|-------|
| 01 | Introduction (this post) |
| 02 | Hardware — STM32H753ZI and EtherCAT |
| 03 | EtherCAT Fundamentals |
| 04 | Integrating SOEM on STM32 |
| 05 | CiA402 State Machine |
| 06 | Cyclic Synchronous Position Mode |
| 07 | Multi-Axis API Design |
| 08 | JOG Operation |
| 09 | Binary UART Protocol Design |
| 10 | Python WebSocket Bridge |
| 11 | Web HMI — Real-Time Dashboard |
| 12 | G-code Interpreter: G00 Rapid Move |
| 13 | G-code Interpreter: G01 Linear Interpolation |
| 14 | G-code Interpreter: G02/G03 Arc Interpolation |
| 15 | Flash Parameter Storage |
| 16 | Software Limits |
| 17 | Multi-Block G-code Program Execution |
| 18 | Bug Fix: Initialization Order Disaster |
| 19 | Bug Fix: Arc Interpolation and the interp_active Flag |
| 20 | Bug Fix: The Full-Circle Arc Degeneracy |

---

The code is written in C11 for the firmware and Python 3.11 for the host side. All decisions were made with real production constraints in mind — no "just use a library" shortcuts where the underlying concept matters.

Let's build it.
