#pragma once
#include <stdint.h>

typedef struct {
    float x;        // [m] from start position
    float y;        // [m]
    float theta;    // [rad] CCW positive
    float v_left;   // [m/s] left wheel
    float v_right;  // [m/s] right wheel
    int32_t tach_left;   // accumulated tachometer (ERPM-ticks)
    int32_t tach_right;
} OdomState_t;

void Odom_Init(void);
void Odom_Reset(void);
void Odom_Update(int32_t tach_left, int32_t tach_right, uint32_t now_ms);
const OdomState_t* Odom_Get(void);

// ERPM→m/s: v = erpm / pole_pairs / 60 * wheel_circ
// Tachometer: accumulated from VESC STATUS_5 (total electrical revolutions)
