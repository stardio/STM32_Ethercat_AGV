/**
 * @file    ui_flash_storage.h
 * @brief   AGV wheel axis — non-volatile parameter storage
 *
 * Flash target : Bank 2 Sector 7  (0x081E0000, 128 KB)
 * Flash word   : 256 bits = 32 bytes  (STM32H753ZI requirement)
 *
 * All per-axis motion parameters (velocity, accel, limits, home offset,
 * unit scale, position gain) are packed into one RobotFlashData_t record
 * and written atomically: erase sector → program 4 flash words.
 *
 * Public API
 * ──────────
 *   RobotFlash_Save()  — marshal g_axis_param[] → flash
 *   RobotFlash_Load()  — flash → g_axis_param[]  (returns 0 if blank/corrupt)
 */
#ifndef UI_FLASH_STORAGE_H
#define UI_FLASH_STORAGE_H

#include <stdint.h>
#include "axis_types.h"    /* AxisParam_t, AXIS_COUNT  */
#include "robot_config.h"  /* ROBOT_FLASH_MAGIC/VERSION */

#ifdef __cplusplus
extern "C" {
#endif

/* ── On-flash image ──────────────────────────────────────────────────────── */
/*
 * Total: 128 bytes = 4 × 32-byte flash words
 *
 *  offset  0 :  uint32_t  magic        (4)
 *  offset  4 :  uint16_t  version      (2)
 *  offset  6 :  uint8_t   axis_count   (1)  — AXIS_COUNT sanity gate
 *  offset  7 :  uint8_t   _rsvd        (1)
 *  offset  8 :  AxisParam_t axis[2]   (96)  — 48 bytes × 2 axes
 *  offset 104:  uint32_t  crc32        (4)  — CRC32 over axis[] only
 *  offset 108:  uint8_t   _pad[20]    (20)  — pad to 128 bytes
 */
typedef struct {
    uint32_t    magic;
    uint16_t    version;
    uint8_t     axis_count;
    uint8_t     _rsvd;
    AxisParam_t axis[AXIS_COUNT];
    uint32_t    crc32;
    uint8_t     _pad[20];
} RobotFlashData_t;

/* ── Primary API ─────────────────────────────────────────────────────────── */

/**
 * @brief  Write g_axis_param[AXIS_COUNT] to flash.
 * @return 1 on success, 0 on HAL error or verify mismatch.
 * @note   Erases the full 128 KB sector then programs 160 bytes.
 *         Call only from a low-priority task — NOT from EtherCAT_Task.
 */
uint8_t RobotFlash_Save(void);

/**
 * @brief  Read flash into g_axis_param[AXIS_COUNT].
 * @return 1 on success, 0 if the sector is blank or the CRC is invalid.
 * @note   Call AxisConfig_InitDefaults() before this function so that
 *         any fields absent in an older flash image get safe defaults.
 */
uint8_t RobotFlash_Load(void);

/* ── Home-offset helpers ─────────────────────────────────────────────────── */
uint8_t UiFlashStorage_SaveHome(int32_t hw_offset);
uint8_t UiFlashStorage_LoadHome(int32_t *hw_offset);

#ifdef __cplusplus
}
#endif

#endif /* UI_FLASH_STORAGE_H */
