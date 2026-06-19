#pragma once
#include <stdint.h>

#define SLIP_END      0xC0U
#define SLIP_ESC      0xDBU
#define SLIP_ESC_END  0xDCU
#define SLIP_ESC_ESC  0xDDU

#define SLIP_BUF_SIZE  512U   // max decoded frame bytes

void     Slip_Init(uint32_t baud);
void     Slip_SendFrame(const uint8_t *data, uint16_t len);
uint16_t Slip_PollRx(uint8_t *frame_out, uint16_t max_len);  // returns 0 or frame len
uint16_t Slip_Encode(const uint8_t *in, uint16_t in_len, uint8_t *out, uint16_t out_max);
uint16_t Crc16Ccitt(const uint8_t *data, uint16_t len);
