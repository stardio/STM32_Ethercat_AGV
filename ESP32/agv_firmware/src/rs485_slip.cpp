#include "rs485_slip.h"
#include "pin_config.h"
#include <Arduino.h>
#include <HardwareSerial.h>

#define RS485 Serial1

static uint8_t  _rx_buf[SLIP_BUF_SIZE];
static uint16_t _rx_len   = 0;
static bool     _rx_esc   = false;
static bool     _rx_active = false;

void Slip_Init(uint32_t baud)
{
    // Boost power must be on before RS485 transceiver works
    pinMode(ME2107_EN, OUTPUT);
    digitalWrite(ME2107_EN, HIGH);

    // MAX13487E: SHUTDOWN=HIGH (enable), RE=HIGH (receive mode default)
    pinMode(RS485_SHUTDOWN_PIN, OUTPUT);
    digitalWrite(RS485_SHUTDOWN_PIN, HIGH);

    pinMode(RS485_RE_PIN, OUTPUT);
    digitalWrite(RS485_RE_PIN, HIGH);   // receive mode

    RS485.begin(baud, SERIAL_8N1, RS485_RX, RS485_TX);
}

uint16_t Crc16Ccitt(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8U);
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1U) ^ 0x1021U) : (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

uint16_t Slip_Encode(const uint8_t *in, uint16_t in_len, uint8_t *out, uint16_t out_max)
{
    uint16_t n = 0;
    if (n >= out_max) return 0;
    out[n++] = SLIP_END;
    for (uint16_t i = 0; i < in_len; i++) {
        if (in[i] == SLIP_END) {
            if (n + 2 > out_max) return 0;
            out[n++] = SLIP_ESC; out[n++] = SLIP_ESC_END;
        } else if (in[i] == SLIP_ESC) {
            if (n + 2 > out_max) return 0;
            out[n++] = SLIP_ESC; out[n++] = SLIP_ESC_ESC;
        } else {
            if (n >= out_max) return 0;
            out[n++] = in[i];
        }
    }
    if (n >= out_max) return 0;
    out[n++] = SLIP_END;
    return n;
}

void Slip_SendFrame(const uint8_t *data, uint16_t len)
{
    uint8_t enc[SLIP_BUF_SIZE * 2 + 4];
    uint16_t enc_len = Slip_Encode(data, len, enc, sizeof(enc));
    if (enc_len == 0) return;

    // Switch to TX mode: RE LOW
    digitalWrite(RS485_RE_PIN, LOW);
    RS485.write(enc, enc_len);
    RS485.flush();
    // Return to RX mode
    digitalWrite(RS485_RE_PIN, HIGH);
}

// Returns complete frame length, or 0 if no complete frame yet.
uint16_t Slip_PollRx(uint8_t *frame_out, uint16_t max_len)
{
    while (RS485.available()) {
        uint8_t b = RS485.read();

        if (b == SLIP_END) {
            if (_rx_active && _rx_len > 0) {
                uint16_t n = _rx_len;
                memcpy(frame_out, _rx_buf, (n < max_len) ? n : max_len);
                _rx_len = 0; _rx_esc = false; _rx_active = false;
                return n;
            }
            _rx_len = 0; _rx_esc = false; _rx_active = true;
            continue;
        }
        if (!_rx_active) continue;

        if (_rx_esc) {
            _rx_esc = false;
            if (b == SLIP_ESC_END)      b = SLIP_END;
            else if (b == SLIP_ESC_ESC) b = SLIP_ESC;
        } else if (b == SLIP_ESC) {
            _rx_esc = true;
            continue;
        }

        if (_rx_len < SLIP_BUF_SIZE) {
            _rx_buf[_rx_len++] = b;
        } else {
            _rx_len = 0; _rx_active = false;  // frame too long, reset
        }
    }
    return 0;
}
