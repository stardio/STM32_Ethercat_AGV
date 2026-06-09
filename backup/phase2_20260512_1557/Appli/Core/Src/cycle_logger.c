#include "cycle_logger.h"
#include <string.h>
#include <stdio.h>

static CycleRecord_t g_log[CYCLE_LOG_MAX];
static uint16_t      g_head  = 0U;  /* index of next write slot */
static uint16_t      g_count = 0U;  /* number of valid entries */

void CycleLogger_Init(void)
{
    memset(g_log, 0, sizeof(g_log));
    g_head  = 0U;
    g_count = 0U;
}

void CycleLogger_Record(const CycleRecord_t *rec)
{
    if (rec == NULL) { return; }
    g_log[g_head] = *rec;
    g_head = (uint16_t)((g_head + 1U) % CYCLE_LOG_MAX);
    if (g_count < CYCLE_LOG_MAX) { g_count++; }
}

CycleRecord_t* CycleLogger_GetLast(uint8_t offset)
{
    if ((g_count == 0U) || (offset >= g_count)) { return NULL; }

    /* Head points to the NEXT write slot; head-1 is the newest record. */
    int32_t idx = (int32_t)g_head - 1 - (int32_t)offset;
    while (idx < 0)
    {
        idx += (int32_t)CYCLE_LOG_MAX;
    }
    return &g_log[(uint16_t)idx % CYCLE_LOG_MAX];
}

uint16_t CycleLogger_GetCount(void)
{
    return g_count;
}

void CycleLogger_Clear(void)
{
    memset(g_log, 0, sizeof(g_log));
    g_head  = 0U;
    g_count = 0U;
}

static const char* judge_str(JudgeResult_t r)
{
    switch (r)
    {
        case JUDGE_OK:            return "OK";
        case JUDGE_NG_FORCE_HIGH: return "NG_FORCE_HIGH";
        case JUDGE_NG_FORCE_LOW:  return "NG_FORCE_LOW";
        case JUDGE_NG_POS_HIGH:   return "NG_POS_HIGH";
        case JUDGE_NG_POS_LOW:    return "NG_POS_LOW";
        case JUDGE_NG_TIME_OVER:  return "NG_TIME_OVER";
        case JUDGE_NG_INTERLOCK:  return "NG_INTERLOCK";
        case JUDGE_NG_ABORT:      return "NG_ABORT";
        default:                  return "UNKNOWN";
    }
}

uint16_t CycleLogger_SerializeCSV(char *buf, uint16_t max_len,
                                   uint16_t start_offset, uint16_t max_rows)
{
    if ((buf == NULL) || (max_len < 2U)) { return 0U; }

    static const char header[] =
        "cycle_no,boot_ms,recipe_idx,recipe_id,result,peak_force_pct,end_pos,cycle_time_ms,alarm_id\r\n";

    uint16_t written = 0U;
    uint16_t hlen = (uint16_t)(sizeof(header) - 1U);

    if (hlen >= max_len) { return 0U; }
    memcpy(buf, header, hlen);
    written = hlen;

    uint16_t rows = 0U;
    for (uint16_t off = start_offset;
         (off < g_count) && (rows < max_rows) && (written < max_len - 1U);
         off++, rows++)
    {
        const CycleRecord_t *r = CycleLogger_GetLast((uint8_t)off);
        if (r == NULL) { break; }

        char line[128];
        int len = snprintf(line, sizeof(line),
                           "%lu,%lu,%u,%u,%s,%u,%ld,%lu,%u\r\n",
                           (unsigned long)r->cycle_number,
                           (unsigned long)r->boot_ms,
                           (unsigned int)r->recipe_idx,
                           (unsigned int)r->recipe_id,
                           judge_str(r->result),
                           (unsigned int)r->peak_force_pct,
                           (long)r->end_position,
                           (unsigned long)r->cycle_time_ms,
                           (unsigned int)r->alarm_id);

        if (len <= 0) { continue; }
        if ((written + (uint16_t)len) >= max_len) { break; }

        memcpy(buf + written, line, (uint16_t)len);
        written = (uint16_t)(written + (uint16_t)len);
    }

    buf[written] = '\0';
    return written;
}
