#include "nvs_params.h"
#include <Preferences.h>
#include <math.h>

#define NVS_NS "agv_params"

static AgvParams_t _p;
static Preferences _prefs;

static const AgvParams_t _defaults = {
    .wheel_base_m   = 0.60f,
    .wheel_radius_m = 0.075f,
    .pole_pairs     = 7,
    .erpm_scale     = 0,          // computed
    .erpm_scale_r   = 0,          // computed
    .erpm_dir_left  = 1.0f,
    .erpm_dir_right = -1.0f,      // right wheel physically reversed
    .can_baud       = 500000,
    .can_id_left    = 1,
    .can_id_right   = 2,
    .odom_pub_ms    = 100,
};

void Params_ComputeErpm(void)
{
    // erpm_scale = pole_pairs * 60 / (2*PI*wheel_radius)  [ERPM per m/s]
    float circ = 2.0f * (float)M_PI * _p.wheel_radius_m;
    _p.erpm_scale   = (int32_t)((float)_p.pole_pairs * 60.0f / circ);
    _p.erpm_scale_r = _p.erpm_scale;
}

void Params_Init(void)
{
    _p = _defaults;

    _prefs.begin(NVS_NS, true);   // read-only

    _p.wheel_base_m   = _prefs.getFloat("wb_m",       _defaults.wheel_base_m);
    _p.wheel_radius_m = _prefs.getFloat("wr_m",       _defaults.wheel_radius_m);
    _p.pole_pairs     = _prefs.getUChar("pole_pairs",  _defaults.pole_pairs);
    _p.erpm_dir_left  = _prefs.getFloat("dir_l",       _defaults.erpm_dir_left);
    _p.erpm_dir_right = _prefs.getFloat("dir_r",       _defaults.erpm_dir_right);
    _p.can_baud       = _prefs.getULong("can_baud",    _defaults.can_baud);
    _p.can_id_left    = _prefs.getUChar("can_id_l",    _defaults.can_id_left);
    _p.can_id_right   = _prefs.getUChar("can_id_r",    _defaults.can_id_right);
    _p.odom_pub_ms    = _prefs.getULong("odom_ms",     _defaults.odom_pub_ms);

    int32_t saved_es = _prefs.getInt("erpm_scale", 0);
    _prefs.end();

    if (saved_es != 0) {
        _p.erpm_scale = _p.erpm_scale_r = saved_es;
    } else {
        Params_ComputeErpm();
    }
}

void Params_Save(void)
{
    _prefs.begin(NVS_NS, false);  // read-write
    _prefs.putFloat("wb_m",        _p.wheel_base_m);
    _prefs.putFloat("wr_m",        _p.wheel_radius_m);
    _prefs.putUChar("pole_pairs",  _p.pole_pairs);
    _prefs.putFloat("dir_l",       _p.erpm_dir_left);
    _prefs.putFloat("dir_r",       _p.erpm_dir_right);
    _prefs.putULong("can_baud",    _p.can_baud);
    _prefs.putUChar("can_id_l",    _p.can_id_left);
    _prefs.putUChar("can_id_r",    _p.can_id_right);
    _prefs.putULong("odom_ms",     _p.odom_pub_ms);
    _prefs.putInt("erpm_scale",    _p.erpm_scale);
    _prefs.end();
}

AgvParams_t* Params_Get(void) { return &_p; }
