#pragma once
#include <stdint.h>

typedef struct {
    float    wheel_base_m;     // [m]  distance between wheel centres (default 0.60)
    float    wheel_radius_m;   // [m]  wheel radius (default 0.075)
    uint8_t  pole_pairs;       // BLDC pole pairs (default 7)
    int32_t  erpm_scale;       // ERPM per (m/s) — computed from pole_pairs+radius
                               // erpm_scale = pole_pairs * 60 / (2*PI*radius)
    int32_t  erpm_scale_r;     // right wheel ERPM scale (default = erpm_scale)
    float    erpm_dir_left;    // +1.0 or -1.0 (motor direction)
    float    erpm_dir_right;
    uint32_t can_baud;         // CAN bus speed in bps (default 500000)
    uint8_t  can_id_left;      // VESC node ID left  (default 1)
    uint8_t  can_id_right;     // VESC node ID right (default 2)
    uint32_t odom_pub_ms;      // odometry publish interval ms (default 100)
} AgvParams_t;

void        Params_Init(void);       // load from NVS, apply defaults if missing
void        Params_Save(void);       // persist to NVS
AgvParams_t* Params_Get(void);       // get pointer to current params
void        Params_ComputeErpm(void);// recalculate erpm_scale from wheel_radius + pole_pairs
