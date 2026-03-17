#ifndef SETTINGS_PERSISTENCE_H
#define SETTINGS_PERSISTENCE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_PERSIST_PARAM_COUNT 7U
#define SETTINGS_PERSIST_PROGRAM_COUNT 10U

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t checksum;
    int32_t jogStepCounts;
    int32_t manualCyclePosition;
    int32_t manualCycleSpeed;
    int16_t manualCycleTorque;
    uint8_t manualCycleAbsMode;
    uint8_t reserved0;
    int32_t parameterValues[SETTINGS_PERSIST_PARAM_COUNT];
    int32_t programValues[SETTINGS_PERSIST_PROGRAM_COUNT];
} PersistentSettingsData;

void SettingsPersistence_Init(void);
uint8_t SettingsPersistence_Load(PersistentSettingsData* outData);
void SettingsPersistence_Save(const PersistentSettingsData* inData);

#ifdef __cplusplus
}
#endif

#endif