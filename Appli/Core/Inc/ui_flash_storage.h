#ifndef UI_FLASH_STORAGE_H
#define UI_FLASH_STORAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_FLASH_PROGRAM_VALUE_COUNT    10U
#define UI_FLASH_PARAMETER_VALUE_COUNT  7U

typedef struct
{
    int32_t values[UI_FLASH_PROGRAM_VALUE_COUNT];
} UiFlashProgramData;

typedef struct
{
    int32_t position;
    int32_t speed;
    int16_t torque;
    uint8_t absMode;
    uint8_t reserved[5];
} UiFlashManualData;

typedef struct
{
    int32_t values[UI_FLASH_PARAMETER_VALUE_COUNT];
} UiFlashParameterData;

uint8_t UiFlashStorage_SaveProgram(const UiFlashProgramData* data);
uint8_t UiFlashStorage_LoadProgram(UiFlashProgramData* data);

uint8_t UiFlashStorage_SaveManual(const UiFlashManualData* data);
uint8_t UiFlashStorage_LoadManual(UiFlashManualData* data);

uint8_t UiFlashStorage_SaveParameter(const UiFlashParameterData* data);
uint8_t UiFlashStorage_LoadParameter(UiFlashParameterData* data);

#ifdef __cplusplus
}
#endif

#endif /* UI_FLASH_STORAGE_H */
