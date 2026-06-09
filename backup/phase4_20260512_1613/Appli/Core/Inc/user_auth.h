#ifndef USER_AUTH_H
#define USER_AUTH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    USER_LEVEL_OPERATOR  = 0,   /* default — cycle start/stop, alarm ACK, counter view */
    USER_LEVEL_ENGINEER  = 1,   /* recipe edit, parameter change, judge limits */
    USER_LEVEL_ADMIN     = 2,   /* user management, NVM reset, system settings */
} UserLevel_t;

/* PIN is a 4-digit decimal number stored as uint16_t */
#define USER_PIN_ENGINEER   1234U
#define USER_PIN_ADMIN      9999U
#define USER_SESSION_TIMEOUT_MS  (10UL * 60UL * 1000UL)  /* 10 minutes auto-logout */

void         UserAuth_Init(void);
void         UserAuth_Tick(void);            /* call every 1ms — session timeout */

uint8_t      UserAuth_Login(uint16_t pin);   /* returns 1 on success */
void         UserAuth_Logout(void);
UserLevel_t  UserAuth_GetLevel(void);
uint8_t      UserAuth_IsAtLeast(UserLevel_t required);

/* For UART reply: ULV,level=%u,name=%s */
void UserAuth_ReplyLevel(void (*reply_fn)(const char *fmt, ...));

#ifdef __cplusplus
}
#endif

#endif /* USER_AUTH_H */
