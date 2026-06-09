#include "user_auth.h"
#include "stm32h7rsxx_hal.h"
#include <string.h>

static UserLevel_t g_level      = USER_LEVEL_OPERATOR;
static uint32_t    g_login_tick = 0U;

static const char* level_name(UserLevel_t l)
{
    switch (l)
    {
        case USER_LEVEL_ENGINEER: return "ENGINEER";
        case USER_LEVEL_ADMIN:    return "ADMIN";
        default:                  return "OPERATOR";
    }
}

void UserAuth_Init(void)
{
    g_level      = USER_LEVEL_OPERATOR;
    g_login_tick = 0U;
}

void UserAuth_Tick(void)
{
    if (g_level == USER_LEVEL_OPERATOR) { return; }

    uint32_t now = HAL_GetTick();
    if ((now - g_login_tick) >= USER_SESSION_TIMEOUT_MS)
    {
        g_level = USER_LEVEL_OPERATOR;
    }
}

uint8_t UserAuth_Login(uint16_t pin)
{
    if (pin == USER_PIN_ADMIN)
    {
        g_level      = USER_LEVEL_ADMIN;
        g_login_tick = HAL_GetTick();
        return 1U;
    }
    if (pin == USER_PIN_ENGINEER)
    {
        g_level      = USER_LEVEL_ENGINEER;
        g_login_tick = HAL_GetTick();
        return 1U;
    }
    return 0U;
}

void UserAuth_Logout(void)
{
    g_level      = USER_LEVEL_OPERATOR;
    g_login_tick = 0U;
}

UserLevel_t UserAuth_GetLevel(void)
{
    return g_level;
}

uint8_t UserAuth_IsAtLeast(UserLevel_t required)
{
    return (g_level >= required) ? 1U : 0U;
}

void UserAuth_ReplyLevel(void (*reply_fn)(const char *fmt, ...))
{
    if (reply_fn == NULL) { return; }
    reply_fn("ULV,level=%u,name=%s", (unsigned int)g_level, level_name(g_level));
}
