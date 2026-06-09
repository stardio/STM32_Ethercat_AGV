# Binary UART Protocol Design — SLIP Framing and CRC16

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 9 of 20  
**Tags:** UART, SLIP, CRC16, Protocol Design, Embedded

---

## Why Not ASCII?

The original firmware used an ASCII command interface inherited from an earlier single-axis prototype. Commands looked like:
```
SET_VEL=100\r\n
TARGET=50000\r\n
```

This works for quick testing but falls apart in production:

- **No framing** — if a byte is dropped, the receiver has to guess where the next command starts
- **No integrity check** — a corrupted byte is silently accepted
- **No binary payloads** — floating-point values need text serialization (lossy for arc parameters)
- **Slow** — "100.0" is 5 bytes; a 32-bit float is 4 bytes

The replacement: a **binary framed protocol** with CRC16 checksumming.

---

## Frame Format

```
[0xC0] [pkt_type:1B] [seq:1B] [payload:N B] [crc16_lo:1B] [crc16_hi:1B] [0xC0]
```

- **0xC0** — SLIP END byte (frame boundary marker)
- **pkt_type** — identifies the packet type (command, status, ack, etc.)
- **seq** — incrementing sequence number for ACK matching
- **payload** — type-specific binary data
- **crc16** — CRC16-CCITT covering `[pkt_type + seq + payload]`, little-endian

SLIP (Serial Line IP, RFC 1055) provides framing: the special bytes 0xC0 and 0xDB are escaped inside the payload. If a byte is dropped mid-frame, the receiver waits for the next 0xC0 and starts fresh — no complex resync logic needed.

---

## SLIP Encoding

```c
static uint16_t slip_encode(const uint8_t *in, uint16_t in_len,
                            uint8_t *out, uint16_t out_max)
{
    uint16_t n = 0;
    out[n++] = SLIP_END;  /* 0xC0 — leading frame marker */

    for (uint16_t i = 0; i < in_len; i++) {
        if (in[i] == SLIP_END) {          /* 0xC0 → DB DC */
            out[n++] = SLIP_ESC;
            out[n++] = SLIP_ESC_END;
        } else if (in[i] == SLIP_ESC) {   /* 0xDB → DB DD */
            out[n++] = SLIP_ESC;
            out[n++] = SLIP_ESC_ESC;
        } else {
            out[n++] = in[i];
        }
    }

    out[n++] = SLIP_END;  /* trailing frame marker */
    return n;
}
```

Worst-case output size: `2 + 2 * in_len`. A 30-byte arc packet (all bytes 0xC0): 62 bytes of SLIP output. At 921600 baud: 62 * 10 bits / 921600 = 0.67 ms. Well within the 10 ms DefaultTask budget.

---

## CRC16-CCITT

```c
static uint16_t crc16_ccitt(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)(data[i] << 8);
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}
```

CRC16-CCITT (polynomial 0x1021, init 0xFFFF, no final XOR) gives a Hamming distance of 4 for frames up to 32751 bits — it detects all single and double-bit errors, all odd-bit errors, and all burst errors up to 16 bits long. Adequate for a 921600-baud UART over a short cable.

---

## Packet Types

```c
/* STM32 → PC */
#define PROTO_PKT_STATUS        0x01   /* 38 bytes, 10 ms period */
#define PROTO_PKT_PARAM_REPORT  0x02   /* 132 bytes, on request */
#define PROTO_PKT_ACK           0x03   /* 2 bytes: seq + result */
#define PROTO_PKT_LOG           0x04   /* variable: UTF-8 string */

/* PC → STM32 */
#define PROTO_PKT_JOG           0x10   /* 6 bytes */
#define PROTO_PKT_MOVE_G00      0x11   /* 12 bytes */
#define PROTO_PKT_MOVE_G01      0x12   /* 16 bytes */
#define PROTO_PKT_MOVE_ARC      0x13   /* 30 bytes */
#define PROTO_PKT_HOME          0x14   /* 2 bytes */
#define PROTO_PKT_SET_PARAM     0x15   /* 8 bytes */
#define PROTO_PKT_SAVE_FLASH    0x16   /* 0 bytes */
#define PROTO_PKT_STOP          0x17   /* 0 bytes */
#define PROTO_PKT_RUN_ENABLE    0x18   /* 2 bytes */
#define PROTO_PKT_PARAM_READ_REQ 0x19  /* 0 bytes */
#define PROTO_PKT_INTERP_ACK    0x1A   /* 0 bytes */
#define PROTO_PKT_JOG_STOP      0x1B   /* 0 bytes */
```

The ACK result codes:
```c
#define PROTO_RESULT_OK         0x00
#define PROTO_RESULT_BUSY       0x01
#define PROTO_RESULT_BAD_PARAM  0x02
#define PROTO_RESULT_NOT_READY  0x03
#define PROTO_RESULT_FLASH_ERR  0x04
#define PROTO_RESULT_BAD_PKT    0xFF
```

---

## The STATUS Packet (Most Frequent)

Sent every 10 ms automatically (100 Hz telemetry):

```c
typedef struct __attribute__((packed)) {
    int32_t  pos_centi;     /* position in 0.01 mm units */
    int16_t  velocity;      /* mm/s (clamped to int16) */
    int16_t  torque;        /* per-mille of rated torque */
    uint16_t statusword;    /* raw CiA402 statusword */
    uint8_t  cia402_state;  /* Cia402State_t enum */
    uint8_t  flags;         /* bit0=pdo_ready, bit1=target_reached, bit2=run_enable */
} AxisStatusPkt_t;          /* 12 bytes */

typedef struct __attribute__((packed)) {
    AxisStatusPkt_t axis[3];   /* 36 bytes */
    uint8_t interp_state;      /* InterpState_t: IDLE/MOVING/DONE/ERROR */
    uint8_t sys_flags;         /* bit0=all_ready, bit1=all_targets_reached */
} ProtoPktStatus_t;            /* 38 bytes */
```

Position is stored in 0.01 mm units (centimillimeters) to fit in a 32-bit integer while covering ±21 km of travel — far more than any robot needs. This avoids floating point in the STATUS path.

---

## ACK / Request Flow

Commands that affect motion or parameters wait for an ACK before the HMI sends the next command. The sequence number allows matching ACK to request:

```
PC:  [SEQ=5] MOVE_G00 X=100 Y=200 Z=-50
MCU: [SEQ=5] ACK OK
PC:  (wait for interp DONE, then send INTERP_ACK)
PC:  [SEQ=6] INTERP_ACK
PC:  (wait for IDLE status)
PC:  [SEQ=7] MOVE_ARC ...
```

If the MCU is busy (interpolator running), it returns `RESULT_BUSY` and the HMI must retry. In practice, the HMI always waits for DONE before sending the next move, so BUSY never occurs during normal G-code execution.

---

## RX Ring Buffer and ISR

The UART RX path uses a lock-free single-producer/single-consumer ring buffer:

```c
static volatile uint8_t  g_proto_rx_ring[PROTO_RX_RING_SIZE];
static volatile uint16_t g_proto_rx_head = 0;   /* written by ISR */
static volatile uint16_t g_proto_rx_tail = 0;   /* read by DefaultTask */

/* Called from UART RX interrupt */
void UartProto_FeedRxByte(uint8_t b)
{
    uint16_t next = (g_proto_rx_head + 1) % PROTO_RX_RING_SIZE;
    if (next != g_proto_rx_tail) {   /* not full */
        g_proto_rx_ring[g_proto_rx_head] = b;
        g_proto_rx_head = next;
    }
}
```

`DefaultTask` calls `UartProto_PollRx()` every 10 ms, which drains the ring, runs the SLIP decoder, and calls `dispatch_packet()` on each complete frame.

---

## Next Post

With the protocol in place on the firmware side, the next post covers the Python bridge — an asyncio program that translates between the WebSocket JSON API (browser-friendly) and the binary SLIP frames (firmware-friendly).
