#include "ui_flash_storage.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "main.h"

#define UI_PARAM_FLASH_SECTOR       (FLASH_SECTOR_NB - 1U)
#define UI_PARAM_FLASH_ADDR         (FLASH_BASE + (UI_PARAM_FLASH_SECTOR * FLASH_SECTOR_SIZE))

#define UI_FLASH_MAGIC              (0x31465355UL) /* "UFS1" */
#define UI_FLASH_VERSION            (1U)

typedef enum
{
    UI_FLASH_PAGE_PROGRAM = 1,
    UI_FLASH_PAGE_MANUAL = 2,
    UI_FLASH_PAGE_PARAMETER = 3
} UiFlashPageId;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t pageId;
    uint32_t payloadSize;
    uint32_t crc32;
    uint8_t payload[64];
} UiFlashRecord;

typedef struct
{
    /* Keep Parameter first for backward compatibility with legacy single-record layout. */
    UiFlashRecord parameter;
    UiFlashRecord manual;
    UiFlashRecord program;
} UiFlashInternalLayout;

static uint32_t UiFlash_Crc32(const uint8_t* data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;

    for (uint32_t i = 0U; i < length; i++)
    {
        crc ^= (uint32_t)data[i];
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1UL));
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }

    return ~crc;
}

static uint8_t UiFlash_BuildRecord(UiFlashPageId pageId,
                                   const void* payload,
                                   uint32_t payloadSize,
                                   UiFlashRecord* record)
{
    if ((payload == 0) || (record == 0) || (payloadSize > sizeof(record->payload)))
    {
        return 0U;
    }

    memset(record, 0xFF, sizeof(*record));
    record->magic = UI_FLASH_MAGIC;
    record->version = UI_FLASH_VERSION;
    record->pageId = (uint16_t)pageId;
    record->payloadSize = payloadSize;
    memcpy(record->payload, payload, payloadSize);

    record->crc32 = UiFlash_Crc32(record->payload, payloadSize);
    record->crc32 ^= UiFlash_Crc32((const uint8_t*)&record->pageId, sizeof(record->pageId));
    record->crc32 ^= UiFlash_Crc32((const uint8_t*)&record->payloadSize, sizeof(record->payloadSize));

    return 1U;
}

static uint8_t UiFlash_ValidateRecord(const UiFlashRecord* record,
                                      UiFlashPageId expectedPage,
                                      uint32_t expectedPayloadSize)
{
    if (record == 0)
    {
        return 0U;
    }

    if (record->magic != UI_FLASH_MAGIC)
    {
        return 0U;
    }

    if (record->version != UI_FLASH_VERSION)
    {
        return 0U;
    }

    if (record->pageId != (uint16_t)expectedPage)
    {
        return 0U;
    }

    if (record->payloadSize != expectedPayloadSize)
    {
        return 0U;
    }

    uint32_t crc = UiFlash_Crc32(record->payload, record->payloadSize);
    crc ^= UiFlash_Crc32((const uint8_t*)&record->pageId, sizeof(record->pageId));
    crc ^= UiFlash_Crc32((const uint8_t*)&record->payloadSize, sizeof(record->payloadSize));

    return (crc == record->crc32) ? 1U : 0U;
}

static void UiFlash_InvalidateDCacheForRange(const void* address, uint32_t sizeBytes)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if ((address == 0) || (sizeBytes == 0U))
    {
        return;
    }

    const uint32_t lineSize = 32U;
    const uint32_t start = ((uint32_t)address) & ~(lineSize - 1U);
    const uint32_t end = (((uint32_t)address) + sizeBytes + (lineSize - 1U)) & ~(lineSize - 1U);

    SCB_InvalidateDCache_by_Addr((uint32_t*)start, (int32_t)(end - start));
    __DSB();
    __ISB();
#else
    (void)address;
    (void)sizeBytes;
#endif
}

static void UiFlash_RestoreInterruptState(uint32_t primask)
{
    if ((primask & 1U) == 0U)
    {
        __enable_irq();
    }
}

static void UiFlash_ReadLayout(UiFlashInternalLayout* layout)
{
    if (layout == 0)
    {
        return;
    }

    UiFlash_InvalidateDCacheForRange((const void*)UI_PARAM_FLASH_ADDR, sizeof(UiFlashInternalLayout));
    memcpy(layout, (const void*)UI_PARAM_FLASH_ADDR, sizeof(UiFlashInternalLayout));
}

static uint8_t UiFlash_WriteLayout(const UiFlashInternalLayout* layout)
{
    uint32_t primask;
    uint32_t sectorError = 0U;
    FLASH_EraseInitTypeDef eraseInit;
    HAL_StatusTypeDef status;

    if (layout == 0)
    {
        return 0U;
    }

    if (((uint32_t)sizeof(UiFlashInternalLayout) % 16U) != 0U)
    {
        return 0U;
    }

    if ((uint32_t)sizeof(UiFlashInternalLayout) > FLASH_SECTOR_SIZE)
    {
        return 0U;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK)
    {
        UiFlash_RestoreInterruptState(primask);
        return 0U;
    }

    memset(&eraseInit, 0, sizeof(eraseInit));
    eraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
    eraseInit.Sector = UI_PARAM_FLASH_SECTOR;
    eraseInit.NbSectors = 1U;

    status = HAL_FLASHEx_Erase(&eraseInit, &sectorError);
    if ((status != HAL_OK) || (sectorError != 0xFFFFFFFFUL))
    {
        (void)HAL_FLASH_Lock();
        UiFlash_RestoreInterruptState(primask);
        return 0U;
    }

    for (uint32_t offset = 0U; offset < sizeof(UiFlashInternalLayout); offset += 16U)
    {
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,
                                   UI_PARAM_FLASH_ADDR + offset,
                                   (uint32_t)((const uint8_t*)layout + offset));
        if (status != HAL_OK)
        {
            (void)HAL_FLASH_Lock();
            UiFlash_RestoreInterruptState(primask);
            return 0U;
        }
    }

    status = HAL_FLASH_Lock();
    UiFlash_RestoreInterruptState(primask);

    if (status != HAL_OK)
    {
        return 0U;
    }

    UiFlash_InvalidateDCacheForRange((const void*)UI_PARAM_FLASH_ADDR, sizeof(UiFlashInternalLayout));

    if (memcmp((const void*)UI_PARAM_FLASH_ADDR, layout, sizeof(UiFlashInternalLayout)) != 0)
    {
        return 0U;
    }

    return 1U;
}

static UiFlashRecord* UiFlash_GetRecordMutable(UiFlashInternalLayout* layout, UiFlashPageId page)
{
    if (layout == 0)
    {
        return 0;
    }

    if (page == UI_FLASH_PAGE_PARAMETER)
    {
        return &layout->parameter;
    }
    if (page == UI_FLASH_PAGE_MANUAL)
    {
        return &layout->manual;
    }
    if (page == UI_FLASH_PAGE_PROGRAM)
    {
        return &layout->program;
    }

    return 0;
}

static const UiFlashRecord* UiFlash_GetRecordConst(const UiFlashInternalLayout* layout, UiFlashPageId page)
{
    if (layout == 0)
    {
        return 0;
    }

    if (page == UI_FLASH_PAGE_PARAMETER)
    {
        return &layout->parameter;
    }
    if (page == UI_FLASH_PAGE_MANUAL)
    {
        return &layout->manual;
    }
    if (page == UI_FLASH_PAGE_PROGRAM)
    {
        return &layout->program;
    }

    return 0;
}

static uint8_t UiFlash_SaveRecord(UiFlashPageId page, const UiFlashRecord* record)
{
    UiFlashInternalLayout layout;
    UiFlashRecord* slot = 0;

    if (record == 0)
    {
        return 0U;
    }

    memset(&layout, 0xFF, sizeof(layout));
    UiFlash_ReadLayout(&layout);

    slot = UiFlash_GetRecordMutable(&layout, page);
    if (slot == 0)
    {
        return 0U;
    }

    *slot = *record;
    return UiFlash_WriteLayout(&layout);
}

static uint8_t UiFlash_LoadRecord(UiFlashPageId page,
                                  void* payload,
                                  uint32_t payloadSize)
{
    UiFlashInternalLayout layout;
    const UiFlashRecord* slot = 0;

    if (payload == 0)
    {
        return 0U;
    }

    UiFlash_ReadLayout(&layout);

    slot = UiFlash_GetRecordConst(&layout, page);
    if (slot == 0)
    {
        return 0U;
    }

    if (UiFlash_ValidateRecord(slot, page, payloadSize) == 0U)
    {
        return 0U;
    }

    memcpy(payload, slot->payload, payloadSize);
    return 1U;
}

uint8_t UiFlashStorage_SaveProgram(const UiFlashProgramData* data)
{
    UiFlashRecord record;

    if ((data == 0) ||
        (UiFlash_BuildRecord(UI_FLASH_PAGE_PROGRAM, data, sizeof(UiFlashProgramData), &record) == 0U))
    {
        return 0U;
    }

    return UiFlash_SaveRecord(UI_FLASH_PAGE_PROGRAM, &record);
}

uint8_t UiFlashStorage_LoadProgram(UiFlashProgramData* data)
{
    return UiFlash_LoadRecord(UI_FLASH_PAGE_PROGRAM,
                              data,
                              sizeof(UiFlashProgramData));
}

uint8_t UiFlashStorage_SaveManual(const UiFlashManualData* data)
{
    UiFlashRecord record;

    if ((data == 0) ||
        (UiFlash_BuildRecord(UI_FLASH_PAGE_MANUAL, data, sizeof(UiFlashManualData), &record) == 0U))
    {
        return 0U;
    }

    return UiFlash_SaveRecord(UI_FLASH_PAGE_MANUAL, &record);
}

uint8_t UiFlashStorage_LoadManual(UiFlashManualData* data)
{
    return UiFlash_LoadRecord(UI_FLASH_PAGE_MANUAL,
                              data,
                              sizeof(UiFlashManualData));
}

uint8_t UiFlashStorage_SaveParameter(const UiFlashParameterData* data)
{
    UiFlashRecord record;

    if ((data == 0) ||
        (UiFlash_BuildRecord(UI_FLASH_PAGE_PARAMETER, data, sizeof(UiFlashParameterData), &record) == 0U))
    {
        return 0U;
    }

    return UiFlash_SaveRecord(UI_FLASH_PAGE_PARAMETER, &record);
}

uint8_t UiFlashStorage_LoadParameter(UiFlashParameterData* data)
{
    return UiFlash_LoadRecord(UI_FLASH_PAGE_PARAMETER,
                              data,
                              sizeof(UiFlashParameterData));
}
