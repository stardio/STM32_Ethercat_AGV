/**
 * @file    io_handler.c
 * @brief   AGV 베이스보드 I/O 확장 드라이버 구현
 *
 * 클럭 기준 (SystemClock_Config 480 MHz 설정)
 *   SYSCLK = 480 MHz
 *   HCLK   = 240 MHz  (AHB_DIV2)
 *   APB1   = 120 MHz  (APB1_DIV2)
 *   TIM3 클럭 = APB1 × 2 = 240 MHz  (APB prescaler ≠ 1)
 *
 * TIM3 PWM 20 kHz
 *   prescaler = 11  → 240 MHz / 12 = 20 MHz
 *   period    = 999 → 20 MHz / 1000 = 20 kHz
 *   duty 분해능: 1000 steps, 0.1 % per step
 *   duty_x100 (0–10000) → pulse = duty_x100 / 10
 *
 * ADC1 연속 DMA
 *   채널 순서: INP15(PA3) → INP18(PA4) → INP19(PA5) → INP3(PA6)
 *   해상도: 12-bit (0–4095)
 *   DMA1_Stream1, Circular 모드 — 버퍼 자동 갱신
 */

#include "io_handler.h"
#include <string.h>

/* ── DO: PF0–PF7 ─────────────────────────────────────────────────────────── */
#define DO_PORT   GPIOF
#define DO_PINS   ((uint16_t)0x00FFU)   /* PF0–PF7 */

static uint8_t _do_val = 0U;

/* ── PWM: TIM3 CH1–4 on PC6/PC7/PC8/PC9 ─────────────────────────────────── */
#define PWM_PORT     GPIOC
#define PWM_PINS     (GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9)
#define PWM_AF       GPIO_AF2_TIM3
#define PWM_PERIOD   999U   /* 1000 steps, 20 kHz */

static TIM_HandleTypeDef _htim3;
static uint16_t _pwm_duty[IO_PWM_COUNT] = {0};

/* ── ADC1: PA3(INP15) PA4(INP18) PA5(INP19) PA6(INP3) ───────────────────── */
#define AI_PORT   GPIOA
#define AI_PINS   (GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6)

static ADC_HandleTypeDef  _hadc1;
static DMA_HandleTypeDef  _hdma_adc1;
static volatile uint16_t  _adc_buf[IO_AI_COUNT];

/* ── DO 초기화 ────────────────────────────────────────────────────────────── */
static void _do_init(void)
{
    __HAL_RCC_GPIOF_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin   = DO_PINS;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DO_PORT, &g);

    /* 초기 모든 출력 OFF */
    HAL_GPIO_WritePin(DO_PORT, DO_PINS, GPIO_PIN_RESET);
}

/* ── DI 초기화 ────────────────────────────────────────────────────────────── */
static void _di_init(void)
{
    /* GPIOD 클럭은 main.c MX_GPIO_Init()에서 이미 활성화 */
    GPIO_InitTypeDef g = {0};
    g.Pin  = 0x00FFU;   /* PD0–PD7 */
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;   /* 옵토커플러 입력 → 액티브 LOW */
    HAL_GPIO_Init(GPIOD, &g);
}

/* ── PWM (TIM3) 초기화 ───────────────────────────────────────────────────── */
static void _pwm_init(void)
{
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin       = PWM_PINS;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = PWM_AF;
    HAL_GPIO_Init(PWM_PORT, &g);

    _htim3.Instance               = TIM3;
    _htim3.Init.Prescaler         = 11U;            /* 240 MHz / 12 = 20 MHz */
    _htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    _htim3.Init.Period            = PWM_PERIOD;     /* 20 MHz / 1000 = 20 kHz */
    _htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    _htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&_htim3);

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = 0U;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;

    HAL_TIM_PWM_ConfigChannel(&_htim3, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&_htim3, &oc, TIM_CHANNEL_2);
    HAL_TIM_PWM_ConfigChannel(&_htim3, &oc, TIM_CHANNEL_3);
    HAL_TIM_PWM_ConfigChannel(&_htim3, &oc, TIM_CHANNEL_4);

    HAL_TIM_PWM_Start(&_htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&_htim3, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&_htim3, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&_htim3, TIM_CHANNEL_4);
}

/* ── ADC1 (DMA Circular) 초기화 ─────────────────────────────────────────── */
static void _adc_init(void)
{
    __HAL_RCC_ADC12_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* AI 핀을 아날로그 모드로 */
    GPIO_InitTypeDef g = {0};
    g.Pin  = AI_PINS;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(AI_PORT, &g);

    /* DMA1_Stream1 ← ADC1 (DMAMUX 요청) */
    _hdma_adc1.Instance                 = DMA1_Stream1;
    _hdma_adc1.Init.Request             = DMA_REQUEST_ADC1;
    _hdma_adc1.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    _hdma_adc1.Init.PeriphInc           = DMA_PINC_DISABLE;
    _hdma_adc1.Init.MemInc              = DMA_MINC_ENABLE;
    _hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    _hdma_adc1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    _hdma_adc1.Init.Mode                = DMA_CIRCULAR;
    _hdma_adc1.Init.Priority            = DMA_PRIORITY_LOW;
    _hdma_adc1.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&_hdma_adc1);
    __HAL_LINKDMA(&_hadc1, DMA_Handle, _hdma_adc1);

    /* ADC1 연속 스캔 모드 */
    _hadc1.Instance = ADC1;
    _hadc1.Init.ClockPrescaler           = ADC_CLOCK_ASYNC_DIV4;
    _hadc1.Init.Resolution               = ADC_RESOLUTION_12B;
    _hadc1.Init.ScanConvMode             = ADC_SCAN_ENABLE;
    _hadc1.Init.EOCSelection             = ADC_EOC_SEQ_CONV;
    _hadc1.Init.LowPowerAutoWait         = DISABLE;
    _hadc1.Init.ContinuousConvMode       = ENABLE;
    _hadc1.Init.NbrOfConversion          = IO_AI_COUNT;
    _hadc1.Init.DiscontinuousConvMode    = DISABLE;
    _hadc1.Init.ExternalTrigConv         = ADC_SOFTWARE_START;
    _hadc1.Init.ExternalTrigConvEdge     = ADC_EXTERNALTRIGCONVEDGE_NONE;
    _hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;
    _hadc1.Init.Overrun                  = ADC_OVR_DATA_OVERWRITTEN;
    _hadc1.Init.LeftBitShift             = ADC_LEFTBITSHIFT_NONE;
    _hadc1.Init.OversamplingMode         = DISABLE;
    HAL_ADC_Init(&_hadc1);

    /* 채널 설정: PA3=INP15, PA4=INP18, PA5=INP19, PA6=INP3 */
    ADC_ChannelConfTypeDef ch = {0};
    ch.SamplingTime = ADC_SAMPLETIME_64CYCLES_5;
    ch.SingleDiff   = ADC_SINGLE_ENDED;
    ch.OffsetNumber = ADC_OFFSET_NONE;
    ch.Offset       = 0U;

    ch.Channel = ADC_CHANNEL_15; ch.Rank = ADC_REGULAR_RANK_1;
    HAL_ADC_ConfigChannel(&_hadc1, &ch);

    ch.Channel = ADC_CHANNEL_18; ch.Rank = ADC_REGULAR_RANK_2;
    HAL_ADC_ConfigChannel(&_hadc1, &ch);

    ch.Channel = ADC_CHANNEL_19; ch.Rank = ADC_REGULAR_RANK_3;
    HAL_ADC_ConfigChannel(&_hadc1, &ch);

    ch.Channel = ADC_CHANNEL_3;  ch.Rank = ADC_REGULAR_RANK_4;
    HAL_ADC_ConfigChannel(&_hadc1, &ch);

    /* 오프셋 캘리브레이션 후 DMA 연속 변환 시작 */
    HAL_ADCEx_Calibration_Start(&_hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
    HAL_ADC_Start_DMA(&_hadc1, (uint32_t *)_adc_buf, IO_AI_COUNT);
}

/* ── 공개 API ────────────────────────────────────────────────────────────── */

void IO_Init(void)
{
    memset((void *)_adc_buf, 0, sizeof(_adc_buf));
    _do_init();
    _di_init();
    _pwm_init();
    _adc_init();
}

void IO_DO_Set(uint8_t mask, uint8_t val)
{
    _do_val = (_do_val & (uint8_t)(~mask)) | (val & mask);
    for (int i = 0; i < 8; i++) {
        if (mask & (uint8_t)(1U << i)) {
            HAL_GPIO_WritePin(DO_PORT, (uint16_t)(1U << i),
                (_do_val & (uint8_t)(1U << i)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    }
}

uint8_t IO_DO_Get(void) { return _do_val; }

uint8_t IO_DI_Get(void)
{
    /* PD0–PD7 읽기, 액티브 LOW → 반전하여 반환 */
    return (uint8_t)(~(GPIOD->IDR & 0xFFU));
}

uint16_t IO_AI_Get(uint8_t ch)
{
    if (ch >= IO_AI_COUNT) return 0U;
    return _adc_buf[ch];   /* DMA가 지속적으로 갱신 */
}

void IO_PWM_Set(uint8_t ch, uint16_t duty_x100)
{
    if (ch >= IO_PWM_COUNT) return;
    if (duty_x100 > IO_PWM_DUTY_MAX) duty_x100 = IO_PWM_DUTY_MAX;
    _pwm_duty[ch] = duty_x100;

    /* duty_x100 / 10 → pulse (0–1000, period=999) */
    uint32_t pulse = (uint32_t)duty_x100 / 10U;

    switch (ch) {
        case 0: __HAL_TIM_SET_COMPARE(&_htim3, TIM_CHANNEL_1, pulse); break;
        case 1: __HAL_TIM_SET_COMPARE(&_htim3, TIM_CHANNEL_2, pulse); break;
        case 2: __HAL_TIM_SET_COMPARE(&_htim3, TIM_CHANNEL_3, pulse); break;
        case 3: __HAL_TIM_SET_COMPARE(&_htim3, TIM_CHANNEL_4, pulse); break;
        default: break;
    }
}

uint16_t IO_PWM_Get(uint8_t ch)
{
    return (ch < IO_PWM_COUNT) ? _pwm_duty[ch] : 0U;
}
