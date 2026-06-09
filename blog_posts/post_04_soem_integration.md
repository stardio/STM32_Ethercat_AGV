# Integrating SOEM on STM32 — The Port Layer

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 4 of 20  
**Tags:** SOEM, STM32, EtherCAT, Embedded C, HAL

---

## What a "Port Layer" Means

SOEM is designed to be platform-independent. Its core (`ethercatmain.c`, `ethercatcoe.c`, etc.) makes no OS or hardware calls directly. Instead it calls a handful of functions that *you* must implement for your target platform:

```c
// SOEM platform interface — you implement these:
int     ecx_setupnic(ecx_portt *port, const char *ifname, int secondary);
int     ecx_closenic(ecx_portt *port);
int     ecx_getindex(ecx_portt *port);
void    ecx_setbufstat(ecx_portt *port, int idx, int bufstat);
int     ecx_outframe(ecx_portt *port, int idx, int stacknumber);
int     ecx_inframe(ecx_portt *port, int idx, int stacknumber);
int     ecx_waitinframe(ecx_portt *port, int idx, osal_timert *timer);
```

For a Linux PC these call `libpcap` or raw sockets. For STM32 they call `HAL_ETH_*` and manipulate DMA descriptors directly.

---

## The STM32 HAL Ethernet DMA Model

The STM32H7 Ethernet peripheral uses two descriptor rings:

- **TX descriptor ring** — DMA reads descriptors and sends frames to the PHY
- **RX descriptor ring** — DMA writes incoming frames into buffers and sets descriptor flags

Each descriptor points to a memory buffer (typically 1524 bytes for an Ethernet MTU frame).

```c
// Simplified TX descriptor layout
typedef struct {
    volatile uint32_t DESC0;   // status flags + OWN bit
    volatile uint32_t DESC1;   // buffer length
    volatile uint32_t BADDR1;  // buffer address
    volatile uint32_t BADDR2;  // next descriptor / second buffer
} ETH_DMADescTypeDef;
```

The OWN bit is the handshake: when set, the DMA owns the descriptor (don't touch it); when clear, the CPU owns it.

---

## Key Port Functions Implemented

### `ec_outframe()` — Send One EtherCAT Frame

```c
int ec_outframe(ecx_portt *port, int idx, int stacknumber)
{
    // Get the pre-built frame from SOEM's TX buffer
    uint8_t *frame   = port->txbuf[idx];
    uint16_t frame_len = port->txbuflength[idx];

    // Wait for a free TX descriptor
    ETH_DMADescTypeDef *dma_tx = get_free_tx_desc();

    // Copy frame into DMA-accessible buffer
    memcpy((void *)dma_tx->BADDR1, frame, frame_len);

    // Set length and give OWN bit to DMA
    dma_tx->DESC1 = frame_len;
    dma_tx->DESC0 = ETH_DMATXDESC_OWN | ETH_DMATXDESC_LS | ETH_DMATXDESC_FS;

    // Trigger DMA
    ETH->DMATPDR = 0;
    return 0;
}
```

### `ec_inframe()` — Check if a Frame Arrived

```c
int ec_inframe(ecx_portt *port, int idx, int stacknumber)
{
    ETH_DMADescTypeDef *dma_rx = get_next_rx_desc();
    if (dma_rx->DESC0 & ETH_DMARXDESC_OWN)
        return EC_NOFRAME;   // DMA still owns it — no data yet

    uint16_t len = (dma_rx->DESC0 >> 16) & 0x3FFF;
    memcpy(port->rxbuf[idx], (void *)dma_rx->BADDR1, len);

    // Return descriptor to DMA
    dma_rx->DESC0 = ETH_DMARXDESC_OWN;
    ETH->DMARPDR  = 0;

    return len;
}
```

---

## The SOEM Initialization Sequence

Called once at startup, inside `EtherCAT_Task` before the 1 ms loop begins:

```c
void SOEM_PortInit(void)
{
    // 1. Initialize SOEM context
    ecx_contextt *ctx = &ecx_context;
    memset(&ecx_port, 0, sizeof(ecx_port));
    ctx->port = &ecx_port;

    // 2. Open NIC — calls our ec_setupnic() port function
    if (ecx_init(ctx, "eth0") == 0)   // "eth0" is just a label here
        Error_Handler();

    // 3. Auto-enumerate slaves and push PREOP
    int slave_count = ecx_config_init(ctx, FALSE);
    if (slave_count < AXIS_COUNT)
        Error_Handler();   // expected 3 slaves

    // 4. Map PDOs into contiguous process image
    ecx_config_map_group(ctx, &IOmap, 0);

    // 5. Enable Distributed Clock
    ecx_configdc(ctx);

    // 6. Push all slaves to OP state
    ecx_writestate(ctx, 0);
    ecx_statecheck(ctx, 0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE);

    // 7. Start the process data exchange loop
    g_soem_ready = 1U;
}
```

---

## The 1 ms Exchange Loop

Once initialized, every 1 ms the `EtherCAT_Task` calls:

```c
void SOEM_PeriodicPoll(void)
{
    // Send output PDO (target positions, controlwords)
    ec_send_processdata();

    // Receive input PDO (actual positions, statuswords)
    ec_receive_processdata(EC_TIMEOUTRET);

    // Copy received data into our shadow mirror
    for (uint8_t ax = 0; ax < AXIS_COUNT; ax++) {
        soem_read_pdo_inputs(ax);
        soem_update_target_output(ax);
        soem_write_pdo_outputs(ax);
    }
}
```

The PDO data lives at `ec_slave[n].inputs` and `ec_slave[n].outputs` — byte pointers into the process image buffer. We cast them to our typed structs:

```c
typedef struct __attribute__((packed)) {
    int32_t  actual_position;   // 0x6064
    uint16_t statusword;        // 0x6041
    int32_t  actual_velocity;   // 0x606C
    int16_t  actual_torque;     // 0x6077
} SlaveInputPDO_t;

typedef struct __attribute__((packed)) {
    int32_t  target_position;   // 0x607A
    uint16_t controlword;       // 0x6040
    int32_t  target_velocity;   // 0x60FF
    int16_t  target_torque;     // 0x6071
} SlaveOutputPDO_t;
```

The `__attribute__((packed))` is critical — without it, GCC would add padding bytes that misalign the fields relative to the actual PDO mapping.

---

## SDO Configuration

Some parameters must be set via SDO (Service Data Object) before the first OP cycle — things like encoder resolution, control loop gains, and operating mode. SOEM provides:

```c
// Write a 32-bit SDO to slave n, object 0x6098, subindex 0
ecx_SDOwrite(&ecx_context, slave_n, 0x6098, 0x00, FALSE,
             sizeof(int32_t), &value, EC_TIMEOUTRXM);
```

SDO writes are blocking and can take several milliseconds — they must happen during the init phase, not in the 1 ms loop. Once in OP mode, SDO access is gated by a stability check to avoid disrupting the real-time exchange.

---

## Timing Considerations

The `EtherCAT_Task` runs with the highest FreeRTOS priority. The task body:

```
t=0ms:    ec_send_processdata()        ~10 µs
t=0.1ms:  <servo drives process data>
t=0.2ms:  ec_receive_processdata()     ~15 µs
t=0.2ms:  Interp_Tick()               ~5 µs (motion planning)
t=0.25ms: soem_update_target_output()  ~3 µs per axis
          soem_write_pdo_outputs()      ~2 µs per axis
t=1ms:    (task sleeps, FreeRTOS yields)
```

Total CPU load: roughly 50 µs out of 1000 µs = 5%. The M7 at 480 MHz has plenty of headroom.

---

## Next Post

With the transport layer working, the next post tackles the **CiA402 state machine** — the protocol that takes a servo from "just powered on" to "running at commanded speed" without triggering a drive fault.
