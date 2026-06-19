/**
 * @file    ui_flash_storage.c
 * @brief   AGV wheel axis — flash persistence implementation
 *
 * Storage  : Bank 2 Sector 7  (FLASH_SETTINGS_ADDR = 0x081E0000)
 * Word size: 256 bits = 32 bytes  (STM32H753ZI HAL requirement)
 *
 * One RobotFlashData_t record (128 bytes = 4 flash words) covers all
 * two wheel axes.  CRC32 is computed over the axis[] payload only.
 *
 * Write sequence
 *   1. Unlock flash
 *   2. Erase Sector 7 (128 KB)
 *   3. Program 4 × 32-byte words
 *   4. Lock flash
 *   5. Invalidate D-cache, verify by memcmp
 */
#include "ui_flash_storage.h"
#include "axis_config.h"   /* g_axis_param[]              */
#include "main.h"          /* FLASH_SETTINGS_ADDR/BANK/SECTOR */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Build-time layout checks — fail fast if AxisParam_t is resized. */
_Static_assert(sizeof(AxisParam_t)      == 48U,  "AxisParam_t size changed — update RobotFlashData_t._pad");
_Static_assert(sizeof(RobotFlashData_t) == 128U, "RobotFlashData_t must be exactly 128 bytes (4 × 32B flash words)");
_Static_assert((sizeof(RobotFlashData_t) % 32U) == 0U, "Flash layout must be a multiple of 32 bytes");

/* ── Internal helpers ────────────────────────────────────────────────────── */

static uint32_t RobotFlash_Crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;

    for (i = 0U; i < len; i++)
    {
        uint8_t  byte = data[i];
        uint8_t  bit;
        crc ^= (uint32_t)byte;
        for (bit = 0U; bit < 8U; bit++)
        {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1UL));
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }

    return ~crc;
}

static void RobotFlash_InvalidateDCache(const void *addr, uint32_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    const uint32_t line  = 32U;
    uint32_t       start;
    uint32_t       end;

    if (addr == NULL || size == 0U) { return; }

    start = (uint32_t)addr & ~(line - 1U);
    end   = ((uint32_t)addr + size + line - 1U) & ~(line - 1U);

    SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
    __DSB();
    __ISB();
#else
    (void)addr;
    (void)size;
#endif
}

/* ── Public API ──────────────────────────────────────────────────────────── */

uint8_t RobotFlash_Save(void)
{
    RobotFlashData_t    img;
    FLASH_EraseInitTypeDef erase;
    uint32_t            sector_err = 0U;
    HAL_StatusTypeDef   st;
    uint32_t            off;

    /* Marshal --------------------------------------------------------------- */
    memset(&img, 0, sizeof(img));
    img.magic      = ROBOT_FLASH_MAGIC;
    img.version    = ROBOT_FLASH_VERSION;
    img.axis_count = (uint8_t)AXIS_COUNT;
    img._rsvd      = 0U;
    memcpy(img.axis, g_axis_param, sizeof(g_axis_param));
    img.crc32      = RobotFlash_Crc32((const uint8_t *)img.axis, sizeof(img.axis));
    /* _pad already zeroed by memset */

    /* Unlock ---------------------------------------------------------------- */
    st = HAL_FLASH_Unlock();
    if (st != HAL_OK) { return 0U; }

    /* Erase sector ---------------------------------------------------------- */
    memset(&erase, 0, sizeof(erase));
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Banks     = FLASH_SETTINGS_BANK;
    erase.Sector    = FLASH_SETTINGS_SECTOR;
    erase.NbSectors = 1U;

    st = HAL_FLASHEx_Erase(&erase, &sector_err);
    if (st != HAL_OK || sector_err != 0xFFFFFFFFUL)
    {
        (void)HAL_FLASH_Lock();
        return 0U;
    }

    /* Program 32-byte flash words ------------------------------------------- */
    for (off = 0U; off < sizeof(img); off += 32U)
    {
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,
                               FLASH_SETTINGS_ADDR + off,
                               (uint32_t)((const uint8_t *)&img + off));
        if (st != HAL_OK)
        {
            (void)HAL_FLASH_Lock();
            return 0U;
        }
    }

    (void)HAL_FLASH_Lock();

    /* Verify ---------------------------------------------------------------- */
    RobotFlash_InvalidateDCache((const void *)FLASH_SETTINGS_ADDR, sizeof(img));

    return (memcmp((const void *)FLASH_SETTINGS_ADDR, &img, sizeof(img)) == 0) ? 1U : 0U;
}

uint8_t RobotFlash_Load(void)
{
    RobotFlashData_t img;
    uint32_t         computed_crc;

    RobotFlash_InvalidateDCache((const void *)FLASH_SETTINGS_ADDR, sizeof(img));
    memcpy(&img, (const void *)FLASH_SETTINGS_ADDR, sizeof(img));

    /* Validate header ------------------------------------------------------- */
    if (img.magic      != ROBOT_FLASH_MAGIC)   { return 0U; }
    if (img.version    != ROBOT_FLASH_VERSION) { return 0U; }
    if (img.axis_count != (uint8_t)AXIS_COUNT) { return 0U; }

    /* Validate CRC ---------------------------------------------------------- */
    computed_crc = RobotFlash_Crc32((const uint8_t *)img.axis, sizeof(img.axis));
    if (img.crc32 != computed_crc)             { return 0U; }

    /* Restore --------------------------------------------------------------- */
    memcpy(g_axis_param, img.axis, sizeof(g_axis_param));
    return 1U;
}

/* ── Legacy home-offset shims ───────────────────────────────────────────── */
/*
 * These functions exist only for main.c call sites that have not yet been
 * migrated to the new web-protocol command layer (Phase 4).
 *
 * SaveHome  — updates AXIS_J1 home offset and writes all 6 axes to flash.
 * LoadHome  — returns the AXIS_J1 home offset that was restored by
 *             RobotFlash_Load() at boot.  If RobotFlash_Load() has not
 *             been called yet this returns the AxisConfig_InitDefaults()
 *             value (0), which is safe.
 */
uint8_t UiFlashStorage_SaveHome(int32_t hw_offset)
{
    g_axis_param[AXIS_J1].home_offset = hw_offset;
    return RobotFlash_Save();
}

uint8_t UiFlashStorage_LoadHome(int32_t *hw_offset)
{
    if (hw_offset == NULL) { return 0U; }
    *hw_offset = g_axis_param[AXIS_J1].home_offset;
    return 1U;
}
