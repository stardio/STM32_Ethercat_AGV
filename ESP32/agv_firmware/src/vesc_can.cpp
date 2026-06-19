#include "vesc_can.h"
#include "pin_config.h"
#include <Arduino.h>
#include "driver/twai.h"

// VESC CAN: extended frames (29-bit), ID = (cmd << 8) | vesc_id
#define MAKE_ID(cmd, id)  (((uint32_t)(cmd) << 8) | (uint8_t)(id))

static VescStatus_t _status[3];  // index 1=left, 2=right

static void _can_send(uint32_t ext_id, const uint8_t *data, uint8_t len)
{
    twai_message_t msg = {};
    msg.extd       = 1;
    msg.identifier = ext_id;
    msg.data_length_code = len;
    memcpy(msg.data, data, len);
    twai_transmit(&msg, pdMS_TO_TICKS(5));
}

void Vesc_Init(void)
{
    // CAN_SPEED_MODE LOW = 500kbps
    pinMode(CAN_SPEED_MODE, OUTPUT);
    digitalWrite(CAN_SPEED_MODE, LOW);

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, TWAI_MODE_NORMAL);
    twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    twai_driver_install(&g, &t, &f);
    twai_start();

    uint32_t alerts = TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_TX_FAILED |
                      TWAI_ALERT_RX_DATA    | TWAI_ALERT_BUS_ERROR  |
                      TWAI_ALERT_ERR_PASS;
    twai_reconfigure_alerts(alerts, NULL);

    memset(_status, 0, sizeof(_status));
}

void Vesc_SetErpm(uint8_t vesc_id, int32_t erpm)
{
    uint8_t d[4];
    d[0] = (erpm >> 24) & 0xFF;
    d[1] = (erpm >> 16) & 0xFF;
    d[2] = (erpm >>  8) & 0xFF;
    d[3] = (erpm      ) & 0xFF;
    _can_send(MAKE_ID(VESC_CAN_CMD_SET_RPM, vesc_id), d, 4);
}

void Vesc_SetDuty(uint8_t vesc_id, float duty)
{
    int32_t v = (int32_t)(duty * 100000.0f);
    uint8_t d[4];
    d[0] = (v >> 24) & 0xFF;
    d[1] = (v >> 16) & 0xFF;
    d[2] = (v >>  8) & 0xFF;
    d[3] = (v      ) & 0xFF;
    _can_send(MAKE_ID(VESC_CAN_CMD_SET_DUTY, vesc_id), d, 4);
}

void Vesc_Stop(void)
{
    Vesc_SetDuty(VESC_ID_LEFT,  0.0f);
    Vesc_SetDuty(VESC_ID_RIGHT, 0.0f);
}

void Vesc_PollRx(void)
{
    uint32_t alerts = 0;
    twai_read_alerts(&alerts, 0);

    if (alerts & TWAI_ALERT_RX_DATA) {
        twai_message_t msg;
        while (twai_receive(&msg, 0) == ESP_OK) {
            if (!msg.extd) continue;
            uint8_t  cmd  = (msg.identifier >> 8) & 0xFF;
            uint8_t  id   = msg.identifier & 0xFF;
            if (id < 1 || id > 2) continue;

            if (cmd == VESC_CAN_CMD_STATUS_1 && msg.data_length_code >= 8) {
                int32_t erpm = ((int32_t)msg.data[0] << 24) |
                               ((int32_t)msg.data[1] << 16) |
                               ((int32_t)msg.data[2] <<  8) |
                                (int32_t)msg.data[3];
                int16_t cur  = ((int16_t)msg.data[4] << 8) | msg.data[5];
                int16_t duty = ((int16_t)msg.data[6] << 8) | msg.data[7];
                _status[id].erpm       = erpm;
                _status[id].current_a  = cur  / 10.0f;
                _status[id].duty       = duty / 1000.0f;
                _status[id].last_update_ms = millis();
                _status[id].alive      = true;
            }
        }
    }

    // Mark as dead if no update for 500ms
    uint32_t now = millis();
    for (int i = 1; i <= 2; i++) {
        if (_status[i].alive && (now - _status[i].last_update_ms) > 500)
            _status[i].alive = false;
    }
}

bool Vesc_IsAlive(uint8_t id)
{
    if (id < 1 || id > 2) return false;
    return _status[id].alive;
}

const VescStatus_t* Vesc_GetStatus(uint8_t id)
{
    if (id < 1 || id > 2) return &_status[0];
    return &_status[id];
}
