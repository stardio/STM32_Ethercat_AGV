#ifndef STM32_EXTMEM_CONF_H
#define STM32_EXTMEM_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#define EXTMEM_DRIVER_NOR_SFDP   1
#define EXTMEM_DRIVER_PSRAM      0
#define EXTMEM_DRIVER_SDCARD     0
#define EXTMEM_DRIVER_USER       0

#define EXTMEM_SAL_XSPI          1
#define EXTMEM_SAL_SD            0

#include "stm32h7rsxx_hal.h"
#include "stm32_extmem.h"
#include "stm32_extmem_type.h"

extern XSPI_HandleTypeDef hxspi2;

enum
{
    EXTMEMORY_1 = 0
};

extern EXTMEM_DefinitionTypeDef extmem_list_config[1];
#if defined(EXTMEM_C)
EXTMEM_DefinitionTypeDef extmem_list_config[1];
#endif

#ifdef __cplusplus
}
#endif

#endif /* STM32_EXTMEM_CONF_H */
