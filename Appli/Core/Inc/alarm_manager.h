#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    ALARM_NONE             = 0,
    /* Safety interlock 100s */
    ALARM_ESTOP            = 101,
    ALARM_DOOR_OPEN        = 102,
    ALARM_TWOHAND_RELEASED = 103,
    /* Drive 200s */
    ALARM_DRIVE_FAULT      = 201,
    ALARM_DRIVE_NOT_READY  = 202,
    /* Process 300s */
    ALARM_CYCLE_TIMEOUT    = 301,
    ALARM_OVERLOAD         = 302,
    ALARM_POS_ERROR        = 303,
    /* Quality/Production 400s */
    ALARM_CONSECUTIVE_NG   = 401,
    /* System 500s */
    ALARM_NVM_ERROR        = 501,
    ALARM_COMM_ERROR       = 502,
} AlarmCode_t;

#define ALARM_LOG_MAX          20U
#define ALARM_CONSECUTIVE_NG_LIMIT  5U   /* raise alarm after N consecutive NG */

typedef struct {
    AlarmCode_t code;
    uint32_t    occurred_ms;
    uint32_t    cleared_ms;    /* 0 = not cleared */
    uint8_t     ack_done;
    char        message[40];
} AlarmRecord_t;

void           AlarmManager_Init(void);
void           AlarmManager_Tick(void);

void           AlarmManager_Raise(AlarmCode_t code);
void           AlarmManager_Ack(void);
void           AlarmManager_Reset(void);

AlarmCode_t    AlarmManager_GetActive(void);
uint8_t        AlarmManager_IsActive(void);
uint8_t        AlarmManager_IsAcked(void);

/* History */
uint8_t              AlarmManager_GetHistoryCount(void);
const AlarmRecord_t* AlarmManager_GetHistoryEntry(uint8_t offset); /* 0=newest */

/* Reply formatted history entry over UART */
void AlarmManager_ReplyActive(void (*reply_fn)(const char *fmt, ...));

#ifdef __cplusplus
}
#endif

#endif /* ALARM_MANAGER_H */
