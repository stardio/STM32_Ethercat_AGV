#ifndef __STM32H7xx_HAL_CONF_H
#define __STM32H7xx_HAL_CONF_H

#define HAL_MODULE_ENABLED
#define HAL_ADC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_CRC_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_ETH_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_USART_MODULE_ENABLED
#define HAL_PCD_MODULE_ENABLED

#define HSE_VALUE    8000000U
#define HSE_STARTUP_TIMEOUT  100U
#define HSI_VALUE    64000000U
#define CSI_VALUE    4000000U
#define LSI_VALUE    32000U
#define LSE_VALUE    32768U
#define LSE_STARTUP_TIMEOUT  5000U
#define EXTERNAL_CLOCK_VALUE 12288000U

#define VDD_VALUE                    3300U
#define TICK_INT_PRIORITY            0U
#define USE_RTOS                     0U
#define USE_SD_TRANSCEIVER           0U
#define USE_SPI_CRC                  0U

#define  USE_HAL_ADC_REGISTER_CALLBACKS         0U
#define  USE_HAL_CEC_REGISTER_CALLBACKS         0U
#define  USE_HAL_COMP_REGISTER_CALLBACKS        0U
#define  USE_HAL_CORDIC_REGISTER_CALLBACKS      0U
#define  USE_HAL_CRYP_REGISTER_CALLBACKS        0U
#define  USE_HAL_DAC_REGISTER_CALLBACKS         0U
#define  USE_HAL_DCMI_REGISTER_CALLBACKS        0U
#define  USE_HAL_DFSDM_REGISTER_CALLBACKS       0U
#define  USE_HAL_DMA2D_REGISTER_CALLBACKS       0U
#define  USE_HAL_DSI_REGISTER_CALLBACKS         0U
#define  USE_HAL_DTS_REGISTER_CALLBACKS         0U
#define  USE_HAL_ETH_REGISTER_CALLBACKS         0U
#define  USE_HAL_FDCAN_REGISTER_CALLBACKS       0U
#define  USE_HAL_FMAC_REGISTER_CALLBACKS        0U
#define  USE_HAL_HRTIM_REGISTER_CALLBACKS       0U
#define  USE_HAL_HSEM_REGISTER_CALLBACKS        0U
#define  USE_HAL_I2C_REGISTER_CALLBACKS         0U
#define  USE_HAL_I2S_REGISTER_CALLBACKS         0U
#define  USE_HAL_IRDA_REGISTER_CALLBACKS        0U
#define  USE_HAL_JPEG_REGISTER_CALLBACKS        0U
#define  USE_HAL_LPTIM_REGISTER_CALLBACKS       0U
#define  USE_HAL_LTDC_REGISTER_CALLBACKS        0U
#define  USE_HAL_MDIOS_REGISTER_CALLBACKS       0U
#define  USE_HAL_MMC_REGISTER_CALLBACKS         0U
#define  USE_HAL_NAND_REGISTER_CALLBACKS        0U
#define  USE_HAL_NOR_REGISTER_CALLBACKS         0U
#define  USE_HAL_OPAMP_REGISTER_CALLBACKS       0U
#define  USE_HAL_OTG_FS_REGISTER_CALLBACKS      0U
#define  USE_HAL_OTG_HS_REGISTER_CALLBACKS      0U
#define  USE_HAL_PCD_REGISTER_CALLBACKS         0U
#define  USE_HAL_QSPI_REGISTER_CALLBACKS        0U
#define  USE_HAL_RNG_REGISTER_CALLBACKS         0U
#define  USE_HAL_RTC_REGISTER_CALLBACKS         0U
#define  USE_HAL_SAI_REGISTER_CALLBACKS         0U
#define  USE_HAL_SD_REGISTER_CALLBACKS          0U
#define  USE_HAL_SDRAM_REGISTER_CALLBACKS       0U
#define  USE_HAL_SMARTCARD_REGISTER_CALLBACKS   0U
#define  USE_HAL_SMBUS_REGISTER_CALLBACKS       0U
#define  USE_HAL_SPI_REGISTER_CALLBACKS         0U
#define  USE_HAL_SPDIFRX_REGISTER_CALLBACKS     0U
#define  USE_HAL_SRAM_REGISTER_CALLBACKS        0U
#define  USE_HAL_SWPMI_REGISTER_CALLBACKS       0U
#define  USE_HAL_TIM_REGISTER_CALLBACKS         0U
#define  USE_HAL_UART_REGISTER_CALLBACKS        0U
#define  USE_HAL_USART_REGISTER_CALLBACKS       0U
#define  USE_HAL_WWDG_REGISTER_CALLBACKS        0U

#include "stm32h7xx_hal_rcc.h"
#include "stm32h7xx_hal_gpio.h"
#include "stm32h7xx_hal_dma.h"
#include "stm32h7xx_hal_cortex.h"
#include "stm32h7xx_hal_adc.h"
#include "stm32h7xx_hal_crc.h"
#include "stm32h7xx_hal_eth.h"
#include "stm32h7xx_hal_exti.h"
#include "stm32h7xx_hal_flash.h"
#include "stm32h7xx_hal_i2c.h"
#include "stm32h7xx_hal_pwr.h"
#include "stm32h7xx_hal_tim.h"
#include "stm32h7xx_hal_uart.h"
#include "stm32h7xx_hal_usart.h"
#include "stm32h7xx_hal_pcd.h"

#define assert_param(expr) ((void)0U)

#endif /* __STM32H7xx_HAL_CONF_H */
