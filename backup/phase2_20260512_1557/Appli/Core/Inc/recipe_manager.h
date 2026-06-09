#ifndef RECIPE_MANAGER_H
#define RECIPE_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "press_state_machine.h"

#define RECIPE_MAX_COUNT   10U
#define RECIPE_NAME_LEN    20U
#define RECIPE_MAGIC       0xA55AU
#define RECIPE_NVM_VERSION 1U

/* Flash sector for recipe storage (sector FLASH_SECTOR_NB-2 = sector 6, 8KB) */
#define RECIPE_FLASH_SECTOR  6U

typedef struct {
    uint16_t magic;
    uint8_t  version;
    uint8_t  locked;               /* 1 = read-only for Operator */
    uint16_t recipe_id;
    char     product_name[RECIPE_NAME_LEN];

    /* Process parameters (mirrors PressConfig_t) */
    int32_t  approach_speed;
    int32_t  approach_pos;
    int32_t  contact_speed;
    uint16_t contact_torque_th;
    int32_t  press_speed;
    int32_t  press_target_pos;
    uint16_t press_max_force;
    uint32_t dwell_time_ms;
    int32_t  return_speed;
    int32_t  return_pos;
    uint32_t cycle_timeout_ms;

    /* Quality judgment limits */
    uint16_t judge_force_max;
    uint16_t judge_force_min;
    int32_t  judge_pos_max;
    int32_t  judge_pos_min;
    uint32_t judge_cycle_time_max;

    uint32_t saved_timestamp;
    uint16_t crc16;
} RecipeData_t;

typedef struct {
    uint16_t    magic;
    uint8_t     nvm_version;
    uint8_t     active_idx;
    uint8_t     count;
    uint8_t     _pad[3];
    RecipeData_t recipes[RECIPE_MAX_COUNT];
    uint16_t    store_crc;
} RecipeStore_t;

void     RecipeManager_Init(void);
uint8_t  RecipeManager_Save(uint8_t idx, const RecipeData_t *recipe);
uint8_t  RecipeManager_Load(uint8_t idx, RecipeData_t *out);
uint8_t  RecipeManager_Activate(uint8_t idx);
uint8_t  RecipeManager_Delete(uint8_t idx);
uint8_t  RecipeManager_Clone(uint8_t src, uint8_t dst);
uint8_t  RecipeManager_SetLock(uint8_t idx, uint8_t locked);
uint8_t  RecipeManager_FlushToFlash(void);

uint8_t          RecipeManager_GetActiveIdx(void);
RecipeData_t*    RecipeManager_GetActive(void);
RecipeData_t*    RecipeManager_GetSlot(uint8_t idx);
uint8_t          RecipeManager_GetCount(void);

/* Convert active recipe to PressConfig for use in state machine */
void     RecipeManager_ApplyActive(void);

/* Reply formatted recipe config over UART (for Web UI) */
void     RecipeManager_ReplyConfig(uint8_t idx, void (*reply_fn)(const char *fmt, ...));

#ifdef __cplusplus
}
#endif

#endif /* RECIPE_MANAGER_H */
