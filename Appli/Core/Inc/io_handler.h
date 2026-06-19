/**
 * @file    io_handler.h
 * @brief   AGV 베이스보드 I/O 확장 드라이버
 *
 * 핀 배치 (NUCLEO-H753ZI Morpho 커넥터 기준)
 * ─────────────────────────────────────────────
 *  DO×8  (Digital Output) : PF0–PF7  — 푸시풀, 옵토커플러 드라이버 연결
 *  DI×8  (Digital Input)  : PD0–PD7  — 내부 풀업, 액티브 LOW (옵토커플러)
 *  AI×4  (Analog Input)   : PA3 PA4 PA5 PA6 — ADC1, 0–3.3 V
 *  PWM×4 (PWM Output)     : PC6 PC7 PC8 PC9 — TIM3 CH1–4, 20 kHz
 *
 * IO_STATUS 브로드캐스트: 200 ms 주기 (DefaultTask 내)
 */
#ifndef IO_HANDLER_H
#define IO_HANDLER_H

#include "stm32h7xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 포인트 수 ───────────────────────────────────────────────────────────── */
#define IO_DO_COUNT   8U
#define IO_DI_COUNT   8U
#define IO_AI_COUNT   4U
#define IO_PWM_COUNT  4U

/* IO_STATUS 전송 주기 (DefaultTask 10 ms tick 기준) */
#define IO_STATUS_TICKS  20U   /* 10 ms × 20 = 200 ms */

/* PWM duty 스케일: 0–10000 = 0.00–100.00 % */
#define IO_PWM_DUTY_MAX  10000U

/* ── 공개 API ────────────────────────────────────────────────────────────── */

void IO_Init(void);

/* Digital Output ─────────────────────────────────────── */
/**
 * @brief DO 비트 출력
 * @param mask  변경할 비트 마스크 (1=이 비트를 변경)
 * @param val   출력값 (mask 비트 내에서만 적용)
 */
void    IO_DO_Set(uint8_t mask, uint8_t val);

/** @brief 현재 DO 출력 상태 반환 */
uint8_t IO_DO_Get(void);

/* Digital Input ─────────────────────────────────────── */
/**
 * @brief DI 8채널 일괄 읽기
 * @return 비트맵, bit[n]=1 이면 DIn 활성(HIGH after invert)
 */
uint8_t IO_DI_Get(void);

/* Analog Input ─────────────────────────────────────── */
/**
 * @brief ADC 원시값 읽기 (12-bit: 0–4095)
 * @param ch  채널 번호 0–3
 */
uint16_t IO_AI_Get(uint8_t ch);

/* PWM Output ─────────────────────────────────────────── */
/**
 * @brief PWM duty 설정
 * @param ch        채널 0–3
 * @param duty_x100 0–10000 (0.00–100.00 %)
 */
void     IO_PWM_Set(uint8_t ch, uint16_t duty_x100);

/** @brief 현재 PWM duty 반환 (0–10000) */
uint16_t IO_PWM_Get(uint8_t ch);

#ifdef __cplusplus
}
#endif

#endif /* IO_HANDLER_H */
