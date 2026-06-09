#include "recipe_manager.h"
#include "stm32h7xx_hal.h"
#include <string.h>
#include <stdio.h>

/* Flash address of recipe sector */
#define RECIPE_FLASH_ADDR  (FLASH_BASE + (RECIPE_FLASH_SECTOR * FLASH_SECTOR_SIZE))

static RecipeStore_t g_store;
static uint8_t       g_initialized = 0U;

/* -----------------------------------------------------------------------
 * CRC-16/CCITT-FALSE
 * ----------------------------------------------------------------------- */
static uint16_t calc_crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;
    for (uint32_t i = 0U; i < len; i++)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8U);
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
                crc = (uint16_t)((crc << 1U) ^ 0x1021U);
            else
                crc = (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

static uint16_t recipe_crc(const RecipeData_t *r)
{
    return calc_crc16((const uint8_t *)r,
                      offsetof(RecipeData_t, crc16));
}

static uint16_t store_crc(const RecipeStore_t *s)
{
    return calc_crc16((const uint8_t *)s,
                      offsetof(RecipeStore_t, store_crc));
}

static void invalidate_dcache_range(const void *addr, uint32_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    const uint32_t line = 32U;
    uint32_t start = ((uint32_t)addr) & ~(line - 1U);
    uint32_t end   = (((uint32_t)addr) + size + (line - 1U)) & ~(line - 1U);
    SCB_InvalidateDCache_by_Addr((uint32_t*)start, (int32_t)(end - start));
    __DSB(); __ISB();
#else
    (void)addr; (void)size;
#endif
}

static uint8_t flash_write_store(void)
{
    uint32_t sectorError = 0U;
    FLASH_EraseInitTypeDef eraseInit;
    HAL_StatusTypeDef status;

    if (sizeof(RecipeStore_t) > FLASH_SECTOR_SIZE) { return 0U; }
    /* Round up to 32-byte flash word boundary */
    static uint8_t __attribute__((aligned(32))) flash_buf[((sizeof(RecipeStore_t) + 31U) / 32U) * 32U];
    memset(flash_buf, 0xFF, sizeof(flash_buf));
    memcpy(flash_buf, &g_store, sizeof(g_store));

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK) { __set_PRIMASK(primask); return 0U; }

    memset(&eraseInit, 0, sizeof(eraseInit));
    eraseInit.TypeErase    = FLASH_TYPEERASE_SECTORS;
    eraseInit.Banks        = FLASH_BANK_1;
    eraseInit.Sector       = RECIPE_FLASH_SECTOR;
    eraseInit.NbSectors    = 1U;
    eraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    status = HAL_FLASHEx_Erase(&eraseInit, &sectorError);
    if ((status != HAL_OK) || (sectorError != 0xFFFFFFFFUL))
    {
        (void)HAL_FLASH_Lock();
        __set_PRIMASK(primask);
        return 0U;
    }

    uint32_t dst_addr = RECIPE_FLASH_ADDR;
    uint32_t words    = sizeof(flash_buf) / 32U;

    for (uint32_t i = 0U; i < words; i++)
    {
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,
                                   dst_addr,
                                   (uint32_t)(flash_buf + i * 32U));
        if (status != HAL_OK)
        {
            (void)HAL_FLASH_Lock();
            __set_PRIMASK(primask);
            return 0U;
        }
        dst_addr += 32U;
    }

    (void)HAL_FLASH_Lock();
    __set_PRIMASK(primask);
    return 1U;
}

static void load_defaults(void)
{
    memset(&g_store, 0, sizeof(g_store));
    g_store.magic       = RECIPE_MAGIC;
    g_store.nvm_version = RECIPE_NVM_VERSION;
    g_store.active_idx  = 0U;
    g_store.count       = 0U;

    /* Slot 0 — factory default */
    RecipeData_t *r = &g_store.recipes[0];
    r->magic            = RECIPE_MAGIC;
    r->version          = 1U;
    r->recipe_id        = 1U;
    strncpy(r->product_name, "Default", RECIPE_NAME_LEN - 1U);
    r->approach_speed   = 500;
    r->approach_pos     = 50;
    r->contact_speed    = 50;
    r->contact_torque_th = 100U;  /* 10.0% (0.1% units) */
    r->press_speed      = 100;
    r->press_target_pos = 0;
    r->press_max_force  = 800U;  /* 80.0% (0.1% units) */
    r->dwell_time_ms    = 500U;
    r->return_speed     = 500;
    r->return_pos       = 0;
    r->cycle_timeout_ms = 30000U;
    r->judge_force_max  = 80U;
    r->judge_force_min  = 5U;
    r->judge_pos_max    = 0;
    r->judge_pos_min    = 0;
    r->judge_cycle_time_max = 0U;
    r->saved_timestamp  = 0U;
    r->crc16            = recipe_crc(r);

    g_store.count        = 1U;
    g_store.store_crc    = store_crc(&g_store);
}

void RecipeManager_Init(void)
{
    invalidate_dcache_range((const void *)RECIPE_FLASH_ADDR, sizeof(RecipeStore_t));
    memcpy(&g_store, (const void *)RECIPE_FLASH_ADDR, sizeof(RecipeStore_t));

    if ((g_store.magic != RECIPE_MAGIC) ||
        (g_store.nvm_version != RECIPE_NVM_VERSION) ||
        (store_crc(&g_store) != g_store.store_crc))
    {
        load_defaults();
        (void)flash_write_store();
    }

    g_initialized = 1U;
}

uint8_t RecipeManager_Save(uint8_t idx, const RecipeData_t *recipe)
{
    if ((idx >= RECIPE_MAX_COUNT) || (recipe == NULL)) { return 0U; }
    if ((g_store.recipes[idx].locked != 0U) && (idx < g_store.count)) { return 0U; }

    g_store.recipes[idx] = *recipe;
    g_store.recipes[idx].magic   = RECIPE_MAGIC;
    g_store.recipes[idx].crc16   = recipe_crc(&g_store.recipes[idx]);

    if (idx >= g_store.count)
    {
        g_store.count = (uint8_t)(idx + 1U);
    }

    g_store.store_crc = store_crc(&g_store);
    return flash_write_store();
}

uint8_t RecipeManager_Load(uint8_t idx, RecipeData_t *out)
{
    if ((idx >= g_store.count) || (out == NULL)) { return 0U; }
    if (g_store.recipes[idx].magic != RECIPE_MAGIC) { return 0U; }
    if (recipe_crc(&g_store.recipes[idx]) != g_store.recipes[idx].crc16) { return 0U; }

    *out = g_store.recipes[idx];
    return 1U;
}

uint8_t RecipeManager_Activate(uint8_t idx)
{
    if (idx >= g_store.count) { return 0U; }
    if (g_store.recipes[idx].magic != RECIPE_MAGIC) { return 0U; }

    g_store.active_idx = idx;
    g_store.store_crc  = store_crc(&g_store);
    return flash_write_store();
}

uint8_t RecipeManager_Delete(uint8_t idx)
{
    if ((idx >= g_store.count) || (idx == g_store.active_idx)) { return 0U; }
    if (g_store.recipes[idx].locked != 0U) { return 0U; }

    memset(&g_store.recipes[idx], 0, sizeof(RecipeData_t));
    /* Compact: shift remaining recipes down */
    for (uint8_t i = idx; i < (g_store.count - 1U); i++)
    {
        g_store.recipes[i] = g_store.recipes[i + 1U];
    }
    memset(&g_store.recipes[g_store.count - 1U], 0, sizeof(RecipeData_t));
    g_store.count--;
    if (g_store.active_idx >= g_store.count)
    {
        g_store.active_idx = 0U;
    }
    g_store.store_crc = store_crc(&g_store);
    return flash_write_store();
}

uint8_t RecipeManager_Clone(uint8_t src, uint8_t dst)
{
    if ((src >= g_store.count) || (dst >= RECIPE_MAX_COUNT)) { return 0U; }
    if (src == dst) { return 0U; }

    g_store.recipes[dst] = g_store.recipes[src];
    g_store.recipes[dst].recipe_id = (uint16_t)(dst + 1U);
    g_store.recipes[dst].version   = 1U;
    g_store.recipes[dst].locked    = 0U;
    g_store.recipes[dst].crc16     = recipe_crc(&g_store.recipes[dst]);

    if (dst >= g_store.count)
    {
        g_store.count = (uint8_t)(dst + 1U);
    }

    g_store.store_crc = store_crc(&g_store);
    return flash_write_store();
}

uint8_t RecipeManager_SetLock(uint8_t idx, uint8_t locked)
{
    if (idx >= g_store.count) { return 0U; }
    g_store.recipes[idx].locked = (locked != 0U) ? 1U : 0U;
    g_store.recipes[idx].crc16  = recipe_crc(&g_store.recipes[idx]);
    g_store.store_crc = store_crc(&g_store);
    return flash_write_store();
}

uint8_t RecipeManager_FlushToFlash(void)
{
    g_store.store_crc = store_crc(&g_store);
    return flash_write_store();
}

uint8_t RecipeManager_GetActiveIdx(void)
{
    return g_store.active_idx;
}

RecipeData_t* RecipeManager_GetActive(void)
{
    return &g_store.recipes[g_store.active_idx];
}

RecipeData_t* RecipeManager_GetSlot(uint8_t idx)
{
    if (idx >= RECIPE_MAX_COUNT) { return NULL; }
    return &g_store.recipes[idx];
}

uint8_t RecipeManager_GetCount(void)
{
    return g_store.count;
}

void RecipeManager_ApplyActive(void)
{
    const RecipeData_t *r = RecipeManager_GetActive();
    PressConfig_t *cfg    = PressStateMachine_GetConfig();

    cfg->approach_speed     = r->approach_speed;
    cfg->approach_pos       = r->approach_pos;
    cfg->contact_speed      = r->contact_speed;
    cfg->contact_torque_th  = r->contact_torque_th;
    cfg->press_speed        = r->press_speed;
    cfg->press_target_pos   = r->press_target_pos;
    cfg->press_max_force    = r->press_max_force;
    cfg->dwell_time_ms      = r->dwell_time_ms;
    cfg->return_speed       = r->return_speed;
    cfg->return_pos         = r->return_pos;
    cfg->cycle_timeout_ms   = r->cycle_timeout_ms;
    cfg->judge_force_max    = r->judge_force_max;
    cfg->judge_force_min    = r->judge_force_min;
    cfg->judge_pos_max      = r->judge_pos_max;
    cfg->judge_pos_min      = r->judge_pos_min;
    cfg->judge_cycle_time_max = r->judge_cycle_time_max;
}

void RecipeManager_ReplyConfig(uint8_t idx, void (*reply_fn)(const char *fmt, ...))
{
    if ((idx >= g_store.count) || (reply_fn == NULL)) { return; }
    const RecipeData_t *r = &g_store.recipes[idx];

    reply_fn("RCP,idx=%u,id=%u,name=%s,ver=%u,lock=%u,active=%u",
             (unsigned int)idx,
             (unsigned int)r->recipe_id,
             r->product_name,
             (unsigned int)r->version,
             (unsigned int)r->locked,
             (unsigned int)(idx == g_store.active_idx));

    reply_fn("RCP_P,apspd=%ld,appos=%ld,ctspd=%ld,ctth=%u,prspd=%ld,tpos=%ld,mf=%u,dw=%lu,rtspd=%ld,rtpos=%ld,to=%lu",
             (long)r->approach_speed, (long)r->approach_pos,
             (long)r->contact_speed, (unsigned int)r->contact_torque_th,
             (long)r->press_speed, (long)r->press_target_pos,
             (unsigned int)r->press_max_force, (unsigned long)r->dwell_time_ms,
             (long)r->return_speed, (long)r->return_pos,
             (unsigned long)r->cycle_timeout_ms);

    reply_fn("RCP_J,fmax=%u,fmin=%u,pmax=%ld,pmin=%ld,tmax=%lu",
             (unsigned int)r->judge_force_max,
             (unsigned int)r->judge_force_min,
             (long)r->judge_pos_max,
             (long)r->judge_pos_min,
             (unsigned long)r->judge_cycle_time_max);
}
