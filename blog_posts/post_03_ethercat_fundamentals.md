# EtherCAT Fundamentals — Why EtherCAT for Motion Control

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 3 of 20  
**Tags:** EtherCAT, Protocol, Real-Time, Motion Control

---

## The Problem with Standard Ethernet for Motion Control

Standard Ethernet (IEEE 802.3) was designed for bursty, best-effort data transfer. It has:

- **Variable latency** — switches buffer frames, introducing unpredictable delays
- **No synchronization** — each node has its own clock
- **High overhead** — a 4-byte payload still requires a 64-byte minimum frame

For controlling servo drives you need the opposite: deterministic, synchronized, low-overhead communication at cycle times of 1–4 ms. This is where EtherCAT (Ethernet for Control Automation Technology) comes in.

---

## How EtherCAT Actually Works

EtherCAT reuses the Ethernet physical layer (same cables, same connectors, same 100 Mbps) but completely redesigns the data link layer.

**The key insight:** instead of addressed packets routed through switches, a single large Ethernet frame passes through every slave on the network in sequence — like a telegraph wire. Each slave reads its input data and writes its output data *as the frame passes through*, without buffering the entire frame first.

```
Master → [Slave 1] → [Slave 2] → [Slave 3] → (back to Master)
           ↑ reads/writes       ↑ reads/writes
           its slice             its slice
```

Round-trip time for a 3-node network: roughly 3 µs for the Ethernet propagation delay plus 1–2 µs of processing in each slave's EtherCAT ASIC. The entire network settles in a few microseconds.

---

## Process Data Objects (PDOs)

The data exchanged each cycle is called the **process image**. It is organized as **PDOs** — Process Data Objects.

- **RxPDO** (Receive PDO): data the master writes *to* the slave (commands)
- **TxPDO** (Transmit PDO): data the slave writes *to* the master (status)

For a CiA402 servo drive in CSP mode, a minimal PDO mapping might look like:

```
RxPDO (master → drive):
  0x607A [int32]  Target Position        (encoder counts)
  0x6040 [uint16] Controlword            (state machine transitions)

TxPDO (drive → master):
  0x6064 [int32]  Actual Position        (encoder counts)
  0x6041 [uint16] Statusword             (drive state)
  0x6077 [int16]  Actual Torque          (per-mille of rated)
```

SOEM maps these into a contiguous memory buffer (`ec_slave[n].inputs` / `ec_slave[n].outputs`). You read and write C structs directly — no serialization needed.

---

## Distributed Clock Synchronization

EtherCAT includes a hardware mechanism called **Distributed Clocks (DC)**. The master sends a broadcast timestamp; each slave measures the propagation delay and locks its internal clock to the master with sub-microsecond precision.

This means all drives on the network update their position reference at exactly the same moment — essential for multi-axis coordination. Without DC, drive A might execute its position update 50 µs before drive B, causing visible path errors.

For this project DC is enabled on all three drives. The 1 ms `EtherCAT_Task` fires the PDO exchange and all three drives latch their new target simultaneously.

---

## SOEM — Simple Open EtherCAT Master

**SOEM** (Simple Open EtherCAT Master) is an open-source (MIT license) EtherCAT master stack written in C. It handles:

- Slave enumeration and auto-configuration
- PDO exchange (`ec_send_processdata()` / `ec_receive_processdata()`)
- SDO reads and writes for configuration
- Distributed Clock setup
- State machine transitions (INIT → PREOP → SAFEOP → OP)

SOEM was originally designed for Linux with raw socket access to the Ethernet driver. For STM32 it needs a **port layer** — a small C module that maps SOEM's Ethernet primitives (`ec_send()`, `ec_recv()`) to the HAL Ethernet driver and its DMA descriptors.

That port layer, `soem_port.c`, is where most of the interesting firmware-level decisions happen, and it's the subject of the next two posts.

---

## EtherCAT Network States

Every EtherCAT slave progresses through four states at startup:

| State | Meaning |
|-------|---------|
| **INIT** | Just powered on. SDO access not yet available. |
| **PREOP** | SDO access available. Use this to configure PDO mapping. |
| **SAFEOP** | TxPDOs active (slave sends data). RxPDOs not yet consumed by drive. |
| **OP** | Fully operational. RxPDOs consumed, all PDOs active. |

SOEM drives all slaves through this sequence automatically with `ec_config()` and `ec_writestate()`. Once in OP state, `ec_send_processdata()` / `ec_receive_processdata()` maintains the 1 ms exchange loop.

---

## Why 1 ms Cycle Time?

The servo drives internal current and velocity control loops typically run at 4–16 kHz. The position loop (which we are closing on the MCU side) runs at 1 kHz — giving 1000 position updates per second. This is well within the bandwidth needed for the trajectory speeds used in a Cartesian robot (10–1000 mm/s axis travel).

1 ms also maps cleanly to FreeRTOS tick resolution and integer millisecond G-code feed rate math.

---

## Next Post

Now that the theory is clear, the next post covers the actual SOEM port layer implementation on STM32 — specifically how to connect SOEM's platform-agnostic API to the STM32 HAL Ethernet driver with DMA descriptors.
