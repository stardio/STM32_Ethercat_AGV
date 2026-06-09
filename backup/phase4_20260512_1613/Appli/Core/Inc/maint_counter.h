#ifndef MAINT_COUNTER_H
#define MAINT_COUNTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MAINT_PM_THRESHOLD_DEFAULT  100000UL  /* alert after 100k cycles */

typedef struct {
    uint32_t total_cycles;       /* lifetime total (survives power-cycle via NVM ideally) */
    uint32_t since_last_maint;   /* resets on MaintCounter_Reset() */
    uint32_t pm_threshold;       /* configurable threshold */
    uint8_t  pm_alert;           /* 1 = threshold exceeded */
} MaintCounterState_t;

void         MaintCounter_Init(void);
void         MaintCounter_IncrementCycle(void);
void         MaintCounter_Reset(void);           /* maintenance done — reset since_last */
void         MaintCounter_SetThreshold(uint32_t threshold);

const MaintCounterState_t* MaintCounter_GetState(void);

/* UART reply: MCT,total=%lu,since=%lu,thresh=%lu,alert=%u */
void MaintCounter_Reply(void (*reply_fn)(const char *fmt, ...));

#ifdef __cplusplus
}
#endif

#endif /* MAINT_COUNTER_H */
