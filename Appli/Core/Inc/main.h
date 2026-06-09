#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

/* ── NUCLEO-H753ZI User LED ───────────────────────────────────────────── */
#define LED_GREEN_PIN   GPIO_PIN_0    /* PB0  */
#define LED_GREEN_PORT  GPIOB
#define LED_YELLOW_PIN  GPIO_PIN_1    /* PE1  */
#define LED_YELLOW_PORT GPIOE
#define LED_RED_PIN     GPIO_PIN_14   /* PB14 */
#define LED_RED_PORT    GPIOB

/* ── Flash settings: Bank2 Sector7 (0x081E0000, 128KB) ──────────────── */
#define FLASH_SETTINGS_ADDR    0x081E0000U
#define FLASH_SETTINGS_BANK    FLASH_BANK_2
#define FLASH_SETTINGS_SECTOR  FLASH_SECTOR_7

void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
