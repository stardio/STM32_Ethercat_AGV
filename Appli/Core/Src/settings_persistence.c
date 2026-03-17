#include "settings_persistence.h"

#include "main.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum
{
    SETTINGS_PERSIST_MAGIC = 0x53544731UL,
    SETTINGS_PERSIST_VERSION = 1UL,
    SETTINGS_PERSIST_FNV_OFFSET = 2166136261UL,
    SETTINGS_PERSIST_FNV_PRIME = 16777619UL
};

static PersistentSettingsData g_persistentSettings __attribute__((section(".bkpsram"), used));
static uint8_t g_persistenceReady = 0U;

static void settingsInvalidateDCacheForRange(const void* address, size_t sizeBytes)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if ((address == NULL) || (sizeBytes == 0U))
    {
        return;
    }

    const uintptr_t lineSize = 32U;
    const uintptr_t start = ((uintptr_t)address) & ~(lineSize - 1U);
    const uintptr_t end = (((uintptr_t)address) + sizeBytes + (lineSize - 1U)) & ~(lineSize - 1U);
    SCB_InvalidateDCache_by_Addr((uint32_t*)start, (int32_t)(end - start));
    __DSB();
    __ISB();
#else
    (void)address;
    (void)sizeBytes;
#endif
}

static void settingsCleanDCacheForRange(const void* address, size_t sizeBytes)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if ((address == NULL) || (sizeBytes == 0U))
    {
        return;
    }

    const uintptr_t lineSize = 32U;
    const uintptr_t start = ((uintptr_t)address) & ~(lineSize - 1U);
    const uintptr_t end = (((uintptr_t)address) + sizeBytes + (lineSize - 1U)) & ~(lineSize - 1U);
    SCB_CleanDCache_by_Addr((uint32_t*)start, (int32_t)(end - start));
    __DSB();
    __ISB();
#else
    (void)address;
    (void)sizeBytes;
#endif
}

static uint32_t settingsComputeFNV1a(const void* address, size_t sizeBytes)
{
    uint32_t hash = SETTINGS_PERSIST_FNV_OFFSET;
    const uint8_t* bytes = (const uint8_t*)address;

    for (size_t index = 0U; index < sizeBytes; index++)
    {
        hash ^= bytes[index];
        hash *= SETTINGS_PERSIST_FNV_PRIME;
    }

    return hash;
}

static uint32_t settingsComputeChecksum(const PersistentSettingsData* data)
{
    const uint8_t* bytes = (const uint8_t*)data;
    const size_t checksumOffset = offsetof(PersistentSettingsData, checksum);
    const size_t afterChecksumOffset = checksumOffset + sizeof(data->checksum);
    uint32_t hash = settingsComputeFNV1a(bytes, checksumOffset);

    for (size_t index = afterChecksumOffset; index < sizeof(PersistentSettingsData); index++)
    {
        hash ^= bytes[index];
        hash *= SETTINGS_PERSIST_FNV_PRIME;
    }

    return hash;
}

static uint8_t settingsIsValidSnapshot(const PersistentSettingsData* snapshot)
{
    if (snapshot == NULL)
    {
        return 0U;
    }

    if ((snapshot->magic != SETTINGS_PERSIST_MAGIC) ||
        (snapshot->version != SETTINGS_PERSIST_VERSION) ||
        (snapshot->checksum != settingsComputeChecksum(snapshot)))
    {
        return 0U;
    }

    return 1U;
}
static void settingsWriteBkpsram(const PersistentSettingsData* snapshot)
{
    if (snapshot == NULL)
    {
        return;
    }

    memcpy(&g_persistentSettings, snapshot, sizeof(g_persistentSettings));
    settingsCleanDCacheForRange(&g_persistentSettings, sizeof(g_persistentSettings));
}

static void settingsPrepareSnapshot(const PersistentSettingsData* inData, PersistentSettingsData* outSnapshot)
{
    if ((inData == NULL) || (outSnapshot == NULL))
    {
        return;
    }

    memcpy(outSnapshot, inData, sizeof(*outSnapshot));
    outSnapshot->magic = SETTINGS_PERSIST_MAGIC;
    outSnapshot->version = SETTINGS_PERSIST_VERSION;
    outSnapshot->checksum = 0U;
    outSnapshot->checksum = settingsComputeChecksum(outSnapshot);
}

void SettingsPersistence_Init(void)
{
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_BKPRAM_CLK_ENABLE();
    __HAL_RCC_BKPRAM_CLK_SLEEP_ENABLE();
    (void)HAL_PWREx_EnableBkUpReg();
    g_persistenceReady = 1U;
}

uint8_t SettingsPersistence_Load(PersistentSettingsData* outData)
{
    PersistentSettingsData snapshot;

    if ((g_persistenceReady == 0U) || (outData == NULL))
    {
        return 0U;
    }

    settingsInvalidateDCacheForRange(&g_persistentSettings, sizeof(g_persistentSettings));
    memcpy(&snapshot, &g_persistentSettings, sizeof(snapshot));

    if (settingsIsValidSnapshot(&snapshot) != 0U)
    {
        memcpy(outData, &snapshot, sizeof(snapshot));
        return 1U;
    }

    return 0U;
}

void SettingsPersistence_Save(const PersistentSettingsData* inData)
{
    PersistentSettingsData snapshot;

    if ((g_persistenceReady == 0U) || (inData == NULL))
    {
        return;
    }

    settingsPrepareSnapshot(inData, &snapshot);
    settingsWriteBkpsram(&snapshot);
}