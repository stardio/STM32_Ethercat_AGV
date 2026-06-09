#include "interlock_manager.h"
#include "stm32h7rsxx_hal.h"
#include <string.h>

static InterlockState_t g_ilock = {0};
static SafetyState_t    g_safety_state = SAFE_STOP;
static uint8_t          g_sim_mode = INTERLOCK_SIM_DEFAULT;

/* Software-simulated input values (used when g_sim_mode == 1) */
static uint8_t g_sim_estop    = 1U;  /* default: E-Stop released */
static uint8_t g_sim_door     = 1U;  /* default: door closed */
static uint8_t g_sim_left     = 0U;
static uint8_t g_sim_right    = 0U;

/* Two-hand timing */
static uint32_t g_twohand_left_tick  = 0U;
static uint32_t g_twohand_right_tick = 0U;
static uint8_t  g_twohand_prev_left  = 0U;
static uint8_t  g_twohand_prev_right = 0U;

void InterlockManager_Init(void)
{
    memset(&g_ilock, 0, sizeof(g_ilock));
    g_safety_state = SAFE_STOP;
    g_twohand_left_tick  = 0U;
    g_twohand_right_tick = 0U;
    g_twohand_prev_left  = 0U;
    g_twohand_prev_right = 0U;

    /* In sim mode, start with all interlocks satisfied except two-hand */
    if (g_sim_mode != 0U)
    {
        g_ilock.estop_ok  = 1U;
        g_ilock.door_ok   = 1U;
    }
}

static uint8_t read_estop_ok(void)
{
    if (g_sim_mode != 0U)
    {
        return g_sim_estop;
    }
    /* Active-LOW NC contact: GPIO=0 means E-Stop pressed (danger) */
    return (HAL_GPIO_ReadPin(ILOCK_ESTOP_PORT, ILOCK_ESTOP_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

static uint8_t read_door_ok(void)
{
    if (g_sim_mode != 0U)
    {
        return g_sim_door;
    }
    /* Active-LOW NC contact: GPIO=0 means door open */
    return (HAL_GPIO_ReadPin(ILOCK_DOOR_PORT, ILOCK_DOOR_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

static uint8_t read_twohand_left(void)
{
    if (g_sim_mode != 0U)
    {
        return g_sim_left;
    }
    return (HAL_GPIO_ReadPin(ILOCK_TWOHAND_L_PORT, ILOCK_TWOHAND_L_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

static uint8_t read_twohand_right(void)
{
    if (g_sim_mode != 0U)
    {
        return g_sim_right;
    }
    return (HAL_GPIO_ReadPin(ILOCK_TWOHAND_R_PORT, ILOCK_TWOHAND_R_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

static void update_twohand_sync(uint8_t left, uint8_t right)
{
    uint32_t now = HAL_GetTick();

    /* Detect rising edge of each hand */
    if ((left != 0U) && (g_twohand_prev_left == 0U))
    {
        g_twohand_left_tick = now;
    }
    if ((right != 0U) && (g_twohand_prev_right == 0U))
    {
        g_twohand_right_tick = now;
    }

    /* Reset sync if either hand released */
    if ((left == 0U) || (right == 0U))
    {
        g_ilock.twohand_sync = 0U;
    }
    else
    {
        /* Both pressed: check timing window */
        uint32_t diff = (g_twohand_left_tick > g_twohand_right_tick)
                        ? (g_twohand_left_tick - g_twohand_right_tick)
                        : (g_twohand_right_tick - g_twohand_left_tick);
        g_ilock.twohand_sync = (diff <= ILOCK_TWOHAND_WINDOW_MS) ? 1U : 0U;
    }

    g_twohand_prev_left  = left;
    g_twohand_prev_right = right;
}

static void update_safety_state(void)
{
    /* E-Stop overrides everything → SAFE_FAULT */
    if (g_ilock.estop_ok == 0U)
    {
        g_safety_state = SAFE_FAULT;
        return;
    }

    /* All basic interlocks satisfied → SAFE_READY */
    if ((g_ilock.estop_ok   != 0U) &&
        (g_ilock.door_ok    != 0U) &&
        (g_ilock.drive_ready != 0U))
    {
        g_safety_state = SAFE_READY;
    }
    else
    {
        g_safety_state = SAFE_STOP;
    }
}

void InterlockManager_Update(void)
{
    uint8_t left  = read_twohand_left();
    uint8_t right = read_twohand_right();

    g_ilock.estop_ok     = read_estop_ok();
    g_ilock.door_ok      = read_door_ok();
    g_ilock.twohand_left  = left;
    g_ilock.twohand_right = right;

    update_twohand_sync(left, right);
    update_safety_state();
}

void InterlockManager_SetDriveReady(uint8_t ready)
{
    g_ilock.drive_ready = (ready != 0U) ? 1U : 0U;
}

void InterlockManager_SetHomeComplete(uint8_t complete)
{
    g_ilock.home_complete = (complete != 0U) ? 1U : 0U;
}

void InterlockManager_SetSimMode(uint8_t enable)
{
    g_sim_mode = (enable != 0U) ? 1U : 0U;
}

void InterlockManager_SimSetEstop(uint8_t ok)
{
    g_sim_estop = (ok != 0U) ? 1U : 0U;
}

void InterlockManager_SimSetDoor(uint8_t ok)
{
    g_sim_door = (ok != 0U) ? 1U : 0U;
}

void InterlockManager_SimSetTwoHandLeft(uint8_t pressed)
{
    g_sim_left = (pressed != 0U) ? 1U : 0U;
}

void InterlockManager_SimSetTwoHandRight(uint8_t pressed)
{
    g_sim_right = (pressed != 0U) ? 1U : 0U;
}

uint8_t InterlockManager_IsAutoReady(void)
{
    return ((g_ilock.estop_ok    != 0U) &&
            (g_ilock.door_ok     != 0U) &&
            (g_ilock.drive_ready != 0U) &&
            (g_ilock.home_complete != 0U)) ? 1U : 0U;
}

uint8_t InterlockManager_IsCycleStartReady(void)
{
    /* AUTO cycle requires all interlocks + two-hand sync */
    return ((InterlockManager_IsAutoReady() != 0U) &&
            (g_ilock.twohand_sync != 0U)) ? 1U : 0U;
}

uint8_t InterlockManager_IsEmergencyStop(void)
{
    return (g_ilock.estop_ok == 0U) ? 1U : 0U;
}

SafetyState_t InterlockManager_GetSafetyState(void)
{
    return g_safety_state;
}

const InterlockState_t* InterlockManager_GetState(void)
{
    return &g_ilock;
}
