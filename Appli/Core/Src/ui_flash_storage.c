#include "ui_flash_storage.h"

#include <string.h>

#include "main.h"
#include "stm32_extmem.h"
#include "stm32_extmem_conf.h"

#define UI_FLASH_REGION_START       (0x70F00000UL)
#define UI_FLASH_REGION_END         (0x70FFFFFFUL)
#define UI_FLASH_SLOT_SIZE          (0x00001000UL)

#define UI_FLASH_PROGRAM_ADDR       (UI_FLASH_REGION_START + 0x0000UL)
#define UI_FLASH_MANUAL_ADDR        (UI_FLASH_REGION_START + 0x1000UL)
#define UI_FLASH_PARAMETER_ADDR     (UI_FLASH_REGION_START + 0x2000UL)

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

/* HAL handle consumed by EXTMEM (declared extern in stm32_extmem_conf.h). */
XSPI_HandleTypeDef hxspi2;

static uint8_t uiFlashInitialized = 0U;
static uint32_t uiFlashMapBase = 0U;

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

static void UiFlash_InitHandle(void)
{
    memset(&hxspi2, 0, sizeof(hxspi2));

    hxspi2.Instance = XSPI2;
    hxspi2.Init.FifoThresholdByte = 4U;
    hxspi2.Init.MemoryMode = HAL_XSPI_SINGLE_MEM;
    hxspi2.Init.MemoryType = HAL_XSPI_MEMTYPE_MACRONIX;
    hxspi2.Init.MemorySize = HAL_XSPI_SIZE_1GB;
    hxspi2.Init.ChipSelectHighTimeCycle = 2U;
    hxspi2.Init.FreeRunningClock = HAL_XSPI_FREERUNCLK_DISABLE;
    hxspi2.Init.ClockMode = HAL_XSPI_CLOCK_MODE_0;
    hxspi2.Init.WrapSize = HAL_XSPI_WRAP_NOT_SUPPORTED;
    hxspi2.Init.ClockPrescaler = 0U;
    hxspi2.Init.SampleShifting = HAL_XSPI_SAMPLE_SHIFT_NONE;
    hxspi2.Init.ChipSelectBoundary = HAL_XSPI_BONDARYOF_NONE;
    hxspi2.Init.MaxTran = 0U;
    hxspi2.Init.Refresh = 0U;
    hxspi2.Init.MemorySelect = HAL_XSPI_CSSEL_NCS1;
}

static uint8_t UiFlash_EnsureInitialized(void)
{
    if (uiFlashInitialized != 0U)
    {
        return 1U;
    }

    UiFlash_InitHandle();

    memset(extmem_list_config, 0, sizeof(extmem_list_config));
    extmem_list_config[EXTMEMORY_1].MemType = EXTMEM_NOR_SFDP;
    extmem_list_config[EXTMEMORY_1].Handle = (void*)&hxspi2;
    extmem_list_config[EXTMEMORY_1].ConfigType = EXTMEM_LINK_CONFIG_8LINES;

    if (EXTMEM_Init(EXTMEMORY_1, HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_XSPI2)) != EXTMEM_OK)
    {
        return 0U;
    }

    if (EXTMEM_GetMapAddress(EXTMEMORY_1, &uiFlashMapBase) != EXTMEM_OK)
    {
        return 0U;
    }

    if (EXTMEM_MemoryMappedMode(EXTMEMORY_1, EXTMEM_ENABLE) != EXTMEM_OK)
    {
        return 0U;
    }

    uiFlashInitialized = 1U;
    return 1U;
}

static uint8_t UiFlash_AddressToOffset(uint32_t absoluteAddress, uint32_t* offset)
{
    if (offset == 0)
    {
        return 0U;
    }

    if ((absoluteAddress < UI_FLASH_REGION_START) || (absoluteAddress > UI_FLASH_REGION_END))
    {
        return 0U;
    }

    if (absoluteAddress < uiFlashMapBase)
    {
        return 0U;
    }

    *offset = absoluteAddress - uiFlashMapBase;
    return 1U;
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

static uint8_t UiFlash_SaveParameterRecordInternal(const UiFlashRecord* record)
{
    uint32_t primask;
    uint32_t sectorError = 0U;
    FLASH_EraseInitTypeDef eraseInit;
    HAL_StatusTypeDef status;

    if (record == 0)
    {
        return 0U;
    }

    if ((sizeof(UiFlashRecord) % 16U) != 0U)
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

    for (uint32_t offset = 0U; offset < sizeof(UiFlashRecord); offset += 16U)
    {
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,
                                   UI_PARAM_FLASH_ADDR + offset,
                                   (uint32_t)((const uint8_t*)record + offset));
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

    UiFlash_InvalidateDCacheForRange((const void*)UI_PARAM_FLASH_ADDR, sizeof(UiFlashRecord));

    if (memcmp((const void*)UI_PARAM_FLASH_ADDR, record, sizeof(UiFlashRecord)) != 0)
    {
        return 0U;
    }

    return 1U;
}

static uint8_t UiFlash_LoadParameterRecordInternal(void* payload, uint32_t payloadSize)
{
    const UiFlashRecord* record = (const UiFlashRecord*)UI_PARAM_FLASH_ADDR;

    if (payload == 0)
    {
        return 0U;
    }

    UiFlash_InvalidateDCacheForRange(record, sizeof(UiFlashRecord));

    if (UiFlash_ValidateRecord(record, UI_FLASH_PAGE_PARAMETER, payloadSize) == 0U)
    {
        return 0U;
    }

    memcpy(payload, record->payload, payloadSize);
    return 1U;
}

static uint8_t UiFlash_SaveRecord(uint32_t absoluteAddress, const UiFlashRecord* record)
{
    if ((record == 0) || (UiFlash_EnsureInitialized() == 0U))
    {
        return 0U;
    }

    uint32_t offset = 0U;
    if (UiFlash_AddressToOffset(absoluteAddress, &offset) == 0U)
    {
        return 0U;
    }

    if (EXTMEM_EraseSector(EXTMEMORY_1, offset, UI_FLASH_SLOT_SIZE) != EXTMEM_OK)
    {
        return 0U;
    }

    if (EXTMEM_WriteInMappedMode(EXTMEMORY_1,
                                 absoluteAddress,
                                 (const uint8_t* const)record,
                                 sizeof(UiFlashRecord)) != EXTMEM_OK)
    {
        return 0U;
    }

    const UiFlashRecord* verify = (const UiFlashRecord*)absoluteAddress;
    if (memcmp(verify, record, sizeof(UiFlashRecord)) != 0)
    {
        return 0U;
    }

    return 1U;
}

static uint8_t UiFlash_LoadRecord(uint32_t absoluteAddress,
                                  UiFlashPageId expectedPage,
                                  void* payload,
                                  uint32_t payloadSize)
{
    if ((payload == 0) || (UiFlash_EnsureInitialized() == 0U))
    {
        return 0U;
    }

    const UiFlashRecord* record = (const UiFlashRecord*)absoluteAddress;

    if (UiFlash_ValidateRecord(record, expectedPage, payloadSize) == 0U)
    {
        return 0U;
    }

    memcpy(payload, record->payload, payloadSize);
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

    return UiFlash_SaveRecord(UI_FLASH_PROGRAM_ADDR, &record);
}

uint8_t UiFlashStorage_LoadProgram(UiFlashProgramData* data)
{
    return UiFlash_LoadRecord(UI_FLASH_PROGRAM_ADDR,
                              UI_FLASH_PAGE_PROGRAM,
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

    return UiFlash_SaveRecord(UI_FLASH_MANUAL_ADDR, &record);
}

uint8_t UiFlashStorage_LoadManual(UiFlashManualData* data)
{
    return UiFlash_LoadRecord(UI_FLASH_MANUAL_ADDR,
                              UI_FLASH_PAGE_MANUAL,
                              data,
                              sizeof(UiFlashManualData));
}

uint8_t UiFlashStorage_SaveParameter(const UiFlashParameterData* data)
{
    UiFlashRecord record __attribute__((aligned(16)));

    if ((data == 0) ||
        (UiFlash_BuildRecord(UI_FLASH_PAGE_PARAMETER, data, sizeof(UiFlashParameterData), &record) == 0U))
    {
        return 0U;
    }

    return UiFlash_SaveParameterRecordInternal(&record);
}

uint8_t UiFlashStorage_LoadParameter(UiFlashParameterData* data)
{
    return UiFlash_LoadParameterRecordInternal(data, sizeof(UiFlashParameterData));
}
