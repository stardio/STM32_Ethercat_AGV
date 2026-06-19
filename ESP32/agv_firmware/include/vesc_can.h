#pragma once
#include <stdint.h>
#include <stdbool.h>

// VESC CAN node IDs
#define VESC_ID_LEFT   1
#define VESC_ID_RIGHT  2

// CAN bus speed
#define CAN_BAUD_500K  500000

// VESC CAN packet command IDs (extended frame: ID = cmd<<8 | vesc_id)
#define VESC_CAN_CMD_SET_RPM    3    // 4B big-endian int32 ERPM
#define VESC_CAN_CMD_SET_DUTY   0    // 4B big-endian int32 duty*100000
#define VESC_CAN_CMD_STATUS_1   9    // status: ERPM(4B) + current(2B) + duty(2B)
#define VESC_CAN_CMD_STATUS_5  27    // status: tach(4B) + vIn(2B)

typedef struct {
    int32_t erpm;            // electrical RPM (negative = reverse)
    float   current_a;       // motor current [A]
    float   duty;            // duty cycle [-1.0, 1.0]
    uint32_t last_update_ms; // millis() at last STATUS_1 reception
    bool    alive;           // true if updated within last 500ms
} VescStatus_t;

void    Vesc_Init(void);
void    Vesc_SetErpm(uint8_t vesc_id, int32_t erpm);
void    Vesc_SetDuty(uint8_t vesc_id, float duty);
void    Vesc_Stop(void);
void    Vesc_PollRx(void);                    // call from CAN RX task
bool    Vesc_IsAlive(uint8_t vesc_id);
const VescStatus_t* Vesc_GetStatus(uint8_t vesc_id);
