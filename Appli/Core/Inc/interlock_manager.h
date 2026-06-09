#ifndef INTERLOCK_MANAGER_H
#define INTERLOCK_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* -----------------------------------------------------------------------
 * GPIO pin mapping — configure per actual HW wiring.
 * Active-LOW inputs (input=0 means signal asserted = danger).
 * E-Stop and Door are active-LOW (NC contact type).
 * Two-Hand, Drive-Ready, Home are active-HIGH.
 * Set INTERLOCK_SIM_DEFAULT 1 to bypass GPIO and enable all interlocks
 * in software (for bench test without physical wiring).
 * ----------------------------------------------------------------------- */
#define INTERLOCK_SIM_DEFAULT  1   /* 1 = software simulation mode (no GPIO needed) */

/* GPIO mapping — replace with real pins when HW is wired */
#define ILOCK_ESTOP_PORT       GPIOA
#define ILOCK_ESTOP_PIN        GPIO_PIN_0   /* Active-LOW NC contact */
#define ILOCK_DOOR_PORT        GPIOA
#define ILOCK_DOOR_PIN         GPIO_PIN_1   /* Active-LOW NC contact */
#define ILOCK_TWOHAND_L_PORT   GPIOA
#define ILOCK_TWOHAND_L_PIN    GPIO_PIN_2   /* Active-HIGH NO button */
#define ILOCK_TWOHAND_R_PORT   GPIOA
#define ILOCK_TWOHAND_R_PIN    GPIO_PIN_3   /* Active-HIGH NO button */

/* Two-hand simultaneity window (ms): both buttons must be pressed within this time */
#define ILOCK_TWOHAND_WINDOW_MS  500U

typedef struct {
    uint8_t estop_ok;       /* 1 = E-Stop released (safe) */
    uint8_t door_ok;        /* 1 = Safety door closed */
    uint8_t twohand_left;   /* 1 = Left hand button pressed */
    uint8_t twohand_right;  /* 1 = Right hand button pressed */
    uint8_t drive_ready;    /* 1 = Drive in Ready/Operational state */
    uint8_t home_complete;  /* 1 = Homing completed */
    uint8_t twohand_sync;   /* 1 = Both hands pressed within window */
} InterlockState_t;

typedef enum {
    SAFE_STOP  = 0,
    SAFE_READY = 1,
    SAFE_FAULT = 2,
} SafetyState_t;

void            InterlockManager_Init(void);
void            InterlockManager_Update(void);
void            InterlockManager_SetDriveReady(uint8_t ready);
void            InterlockManager_SetHomeComplete(uint8_t complete);
void            InterlockManager_SetSimMode(uint8_t enable);
void            InterlockManager_SimSetEstop(uint8_t ok);
void            InterlockManager_SimSetDoor(uint8_t ok);
void            InterlockManager_SimSetTwoHandLeft(uint8_t pressed);
void            InterlockManager_SimSetTwoHandRight(uint8_t pressed);
uint8_t         InterlockManager_IsAutoReady(void);
uint8_t         InterlockManager_IsCycleStartReady(void);
uint8_t         InterlockManager_IsEmergencyStop(void);
SafetyState_t   InterlockManager_GetSafetyState(void);
const InterlockState_t* InterlockManager_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* INTERLOCK_MANAGER_H */
