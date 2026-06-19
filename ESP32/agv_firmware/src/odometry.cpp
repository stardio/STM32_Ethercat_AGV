#include "odometry.h"
#include "nvs_params.h"
#include "vesc_can.h"
#include <math.h>
#include <string.h>

static OdomState_t _state;
static int32_t     _prev_tach_l = 0;
static int32_t     _prev_tach_r = 0;
static uint32_t    _prev_ms     = 0;
static bool        _initialized = false;

void Odom_Init(void)  { memset(&_state, 0, sizeof(_state)); _initialized = false; }
void Odom_Reset(void) { memset(&_state, 0, sizeof(_state)); _initialized = false; }

void Odom_Update(int32_t tach_l, int32_t tach_r, uint32_t now_ms)
{
    AgvParams_t *p = Params_Get();

    if (!_initialized) {
        _prev_tach_l = tach_l;
        _prev_tach_r = tach_r;
        _prev_ms     = now_ms;
        _state.tach_left  = tach_l;
        _state.tach_right = tach_r;
        _initialized = true;
        return;
    }

    float dt = (float)(now_ms - _prev_ms) * 0.001f;
    if (dt <= 0.0f || dt > 1.0f) {
        _prev_ms = now_ms;
        return;
    }

    // ERPM → rad/s → m/s
    // tach is electrical revolution count from VESC STATUS_5
    // delta_erpm_ticks / pole_pairs = mechanical revolutions
    // v [m/s] = mech_rev * circ / dt
    int32_t d_tach_l = tach_l - _prev_tach_l;
    int32_t d_tach_r = tach_r - _prev_tach_r;

    float circ = 2.0f * (float)M_PI * p->wheel_radius_m;
    float mech_per_etick = 1.0f / (float)p->pole_pairs;

    float dL = (float)d_tach_l * mech_per_etick * circ * p->erpm_dir_left;
    float dR = (float)d_tach_r * mech_per_etick * circ * p->erpm_dir_right;

    _state.v_left  = dL / dt;
    _state.v_right = dR / dt;

    float dC = (dL + dR) * 0.5f;
    float dT = (dR - dL) / p->wheel_base_m;

    _state.x     += dC * cosf(_state.theta + dT * 0.5f);
    _state.y     += dC * sinf(_state.theta + dT * 0.5f);
    _state.theta += dT;
    // keep theta in [-pi, pi]
    while (_state.theta >  (float)M_PI) _state.theta -= 2.0f * (float)M_PI;
    while (_state.theta < -(float)M_PI) _state.theta += 2.0f * (float)M_PI;

    _state.tach_left  = tach_l;
    _state.tach_right = tach_r;
    _prev_tach_l = tach_l;
    _prev_tach_r = tach_r;
    _prev_ms     = now_ms;
}

const OdomState_t* Odom_Get(void) { return &_state; }
