#include "maint_counter.h"
#include <string.h>

static MaintCounterState_t g_state;

void MaintCounter_Init(void)
{
    memset(&g_state, 0, sizeof(g_state));
    g_state.pm_threshold = MAINT_PM_THRESHOLD_DEFAULT;
}

void MaintCounter_IncrementCycle(void)
{
    g_state.total_cycles++;
    g_state.since_last_maint++;

    if (g_state.since_last_maint >= g_state.pm_threshold)
    {
        g_state.pm_alert = 1U;
    }
}

void MaintCounter_Reset(void)
{
    g_state.since_last_maint = 0U;
    g_state.pm_alert = 0U;
}

void MaintCounter_SetThreshold(uint32_t threshold)
{
    if (threshold == 0U) { return; }
    g_state.pm_threshold = threshold;
    g_state.pm_alert = (g_state.since_last_maint >= threshold) ? 1U : 0U;
}

const MaintCounterState_t* MaintCounter_GetState(void)
{
    return &g_state;
}

void MaintCounter_Reply(void (*reply_fn)(const char *fmt, ...))
{
    if (reply_fn == NULL) { return; }
    reply_fn("MCT,total=%lu,since=%lu,thresh=%lu,alert=%u",
             (unsigned long)g_state.total_cycles,
             (unsigned long)g_state.since_last_maint,
             (unsigned long)g_state.pm_threshold,
             (unsigned int)g_state.pm_alert);
}
