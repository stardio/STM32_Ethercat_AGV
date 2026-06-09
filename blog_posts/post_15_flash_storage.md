# Flash Parameter Storage — Non-Volatile Settings on STM32

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 15 of 20  
**Tags:** Flash, STM32H7, Non-Volatile Storage, Embedded C, Parameters

---

## The Problem: Parameters Don't Survive Power Cycles

Machine parameters — encoder resolution, home offset, velocity limits, soft limits — must survive a power cycle. Without persistence, every boot requires manual re-entry of all settings.

Options on STM32:
1. **Internal Flash** — built-in, no extra hardware, but has erase granularity and endurance limits
2. **External SPI Flash/EEPROM** — unlimited rewrite cycles, but adds hardware
3. **Battery-backed SRAM** — volatile but keeps data while power is present

For this project, internal Flash is the right choice. Parameters change rarely (only when configuring the machine, not during normal operation), so Flash endurance (typically 10,000 erase cycles per sector) is not a concern.

---

## STM32H753 Flash Layout

The STM32H753ZI has 2 MB dual-bank Flash:

```
Bank 1: Sector 0–7 (1 MB) — Firmware code
Bank 2: Sector 0–7 (1 MB) — Available for data

  Bank 2 Sector 7: offset 0x1FF0000, size 128 KB
  → Used for robot parameters
```

Bank 2 is separate from the firmware bank. This allows firmware updates without erasing saved parameters (assuming the bootloader writes to Bank 1 only).

---

## Data Structure

```c
typedef struct {
    AxisParam_t axes[AXIS_COUNT];   /* 3 × ~48 bytes = ~144 bytes */
    uint32_t    magic;              /* 0xBEEFCAFE — validity check */
    uint32_t    crc32;              /* CRC of everything except crc32 field */
} RobotFlashData_t;
```

**Magic word**: `0xBEEFCAFE` — if this doesn't match (e.g., first boot after firmware flash), the defaults are used instead of corrupted data.

**CRC32**: covers the `axes` array and `magic` field. Detects bit-flip corruption from Flash programming errors or power loss during write.

---

## Writing to Flash

Flash on STM32H7 requires:
1. Unlock the Flash control register
2. Erase the target sector(s)
3. Write data in 256-bit (32-byte) words — the H7 Flash has a 32-byte write granularity
4. Lock the Flash control register

```c
uint8_t RobotFlash_Save(void)
{
    RobotFlashData_t data;
    memcpy(&data.axes, g_axis_param, sizeof(g_axis_param));
    data.magic = FLASH_MAGIC;
    data.crc32 = compute_crc32((uint8_t *)&data,
                               offsetof(RobotFlashData_t, crc32));

    HAL_FLASH_Unlock();

    /* Erase sector 7 of Bank 2 */
    FLASH_EraseInitTypeDef erase = {
        .TypeErase    = FLASH_TYPEERASE_SECTORS,
        .Banks        = FLASH_BANK_2,
        .Sector       = FLASH_SECTOR_7,
        .NbSectors    = 1,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,
    };
    uint32_t sector_error;
    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK) {
        HAL_FLASH_Lock();
        return 0U;   /* FAIL */
    }

    /* Write in 32-byte chunks */
    const uint8_t *src = (const uint8_t *)&data;
    uint32_t dst = FLASH_PARAM_BASE_ADDR;
    size_t   remaining = sizeof(data);

    while (remaining > 0) {
        uint8_t chunk[32] = {0xFF};
        size_t  n = (remaining >= 32) ? 32 : remaining;
        memcpy(chunk, src, n);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, dst,
                              (uint32_t)chunk) != HAL_OK) {
            HAL_FLASH_Lock();
            return 0U;
        }
        src       += n;
        dst       += 32;
        remaining -= n;
    }

    HAL_FLASH_Lock();
    return 1U;   /* OK */
}
```

---

## Reading from Flash

Flash is memory-mapped — reading is just a pointer dereference:

```c
uint8_t RobotFlash_Load(void)
{
    const RobotFlashData_t *stored =
        (const RobotFlashData_t *)FLASH_PARAM_BASE_ADDR;

    /* Validate magic */
    if (stored->magic != FLASH_MAGIC)
        return 0U;

    /* Validate CRC */
    uint32_t expected_crc = compute_crc32((uint8_t *)stored,
                                          offsetof(RobotFlashData_t, crc32));
    if (stored->crc32 != expected_crc)
        return 0U;

    /* Load into RAM */
    memcpy(g_axis_param, &stored->axes, sizeof(g_axis_param));
    return 1U;
}
```

If either check fails, `g_axis_param` keeps its compile-time defaults and the function returns 0 (caller logs a warning).

---

## Deferred Flash Save

Flash erasing takes ~1–2 ms and is blocking. You cannot call `RobotFlash_Save()` from `EtherCAT_Task` — it would miss a PDO cycle and potentially fault the drives.

Solution: a pending flag that `DefaultTask` checks after each 10 ms loop iteration:

```c
/* Set by UART command handler (DefaultTask context) */
extern volatile uint8_t g_flashSavePending;

/* In UART dispatcher: */
case PROTO_PKT_SAVE_FLASH:
    g_flashSavePending |= FLASH_SAVE_PARAM;
    dispatch_ack(seq, PROTO_RESULT_OK);
    break;

/* In DefaultTask main loop: */
if (g_flashSavePending & FLASH_SAVE_PARAM) {
    g_flashSavePending &= ~FLASH_SAVE_PARAM;
    uint8_t ok = RobotFlash_Save();
    /* Log result */
}
```

The Flash write happens during `DefaultTask` time, which doesn't affect the real-time EtherCAT loop.

---

## Boot Sequence

```c
/* In EtherCAT_Task, after SOEM_PortInit() */
void Pc_CommandInit(void)
{
    AxisConfig_InitDefaults();     /* set compile-time defaults */

    if (RobotFlash_Load() != 0U) {
        /* Flash had valid data — apply home offsets */
        for (ax = 0; ax < AXIS_COUNT; ax++)
            SOEM_LoadHomeHwOffset((AxisId_t)ax, g_axis_param[ax].home_offset);
    }

    /* Propagate velocity/accel/decel into g_rt[] */
    SOEM_SyncRtFromAxisParam();

    /* Re-derive soft limit active flags */
    SOEM_RefreshAllLimits();
}
```

`SOEM_SyncRtFromAxisParam()` was added to fix an initialization order bug — see Post 18 for the full story.

---

## Web HMI "Save" Button

The HMI's "Save to Flash" button:
```javascript
document.getElementById('btn-save-flash').addEventListener('click', () => {
    send({ cmd: 'save_flash' });
});
```

After saving, the HMI re-reads parameters via `param_read_req` to confirm the saved values match what was sent. This catches any firmware-side clamping or rejection.

---

## Next Post

With parameters persisted, the next challenge: **software limits** — preventing the robot from driving into its mechanical stops by clamping commanded positions to safe bounds.
