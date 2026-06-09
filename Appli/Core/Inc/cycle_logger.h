#ifndef CYCLE_LOGGER_H
#define CYCLE_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "press_state_machine.h"

#define CYCLE_LOG_MAX  100U

typedef struct {
    uint32_t      boot_ms;          /* HAL_GetTick() at cycle end */
    uint32_t      cycle_number;
    uint8_t       recipe_idx;
    uint8_t       recipe_id;
    uint8_t       op_mode;          /* OperationMode_t */
    uint8_t       _pad;
    JudgeResult_t result;
    uint16_t      peak_force_pct;   /* % torque × 10 (0.1% resolution) */
    int32_t       end_position;     /* user units */
    uint32_t      cycle_time_ms;
    uint16_t      alarm_id;         /* 0 = no alarm */
} CycleRecord_t;

void            CycleLogger_Init(void);
void            CycleLogger_Record(const CycleRecord_t *rec);
CycleRecord_t*  CycleLogger_GetLast(uint8_t offset);   /* 0 = newest */
uint16_t        CycleLogger_GetCount(void);
void            CycleLogger_Clear(void);

/* Write CSV text into buf. Returns bytes written (excluding NUL). */
uint16_t        CycleLogger_SerializeCSV(char *buf, uint16_t max_len,
                                          uint16_t start_offset, uint16_t max_rows);

#ifdef __cplusplus
}
#endif

#endif /* CYCLE_LOGGER_H */
