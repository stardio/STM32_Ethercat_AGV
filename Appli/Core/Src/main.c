/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : AGV EtherCAT controller — FreeRTOS entry point
  *
  * Two tasks:
  *   EtherCAT_Task  (RT, 1 ms) — SOEM PDO loop
  *   DefaultTask    (low, 10 ms) — UART protocol dispatch + flash save
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"

/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "ui_flash_storage.h"
#include "axis_config.h"
#include "soem/soem.h"
#include "osal.h"
#include "lan8742.h"
#include "soem_port.h"
#include "task.h"
#include "uart_protocol.h"
#include "io_handler.h"
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart3;

ETH_HandleTypeDef heth;
__ALIGN_BEGIN ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT] __ALIGN_END
    __attribute__((section(".eth_dma"), aligned(32)));
__ALIGN_BEGIN ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT] __ALIGN_END
    __attribute__((section(".eth_dma"), aligned(32)));

volatile uint8_t  g_ethLinkStatus   = 0;
volatile uint8_t  g_soemInitStatus  = 0;
volatile uint8_t  g_slaveCount      = 0;
volatile uint32_t g_soemErrorCode   = 0;

/* Debug counters from nicdrv.c */
extern volatile uint32_t g_soem_tx_count;
extern volatile uint32_t g_soem_tx_fail;
extern volatile uint32_t g_soem_rx_count;
extern volatile uint32_t g_soem_eth_state;
extern volatile uint32_t g_soem_setupnic_state;
extern volatile uint32_t g_soem_hal_status;
extern volatile uint32_t g_soem_eth_error;
extern volatile uint32_t g_soem_outframe_entry;
extern volatile uint32_t g_soem_outframe_step;
extern volatile uint32_t g_soem_txbuflength;

/* Debug counters from ec_base.c */
extern volatile uint32 g_soem_bwr_count;
extern volatile uint32 g_soem_brd_count;
extern volatile uint32 g_soem_getindex_count;
extern volatile uint32 g_soem_srconfirm_count;

/* Debug counters from ec_config.c */
extern volatile uint32 g_soem_config_init_entry;
extern volatile uint32 g_soem_detect_slaves_entry;
extern volatile uint32 g_soem_init_context_done;
extern volatile uint32 g_soem_detect_step;
extern volatile int    g_soem_sockhandle_value;

/* Async flash save flag — set in EtherCAT_Task, consumed in DefaultTask */
volatile uint8_t g_flashSavePending = 0U;

osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
    .name       = "defaultTask",
    .stack_size = 1024 * 4,
    .priority   = (osPriority_t)osPriorityBelowNormal,
};

/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static void MPU_Config(void);
static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART3_Init(void);
static void MX_ETH_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */
void EtherCAT_Task(void *argument);
/* USER CODE END PFP */

/* ── UART ring buffer ─────────────────────────────────────────────────────── */
#define UART_LOG_BUF_SIZE 4096U

static volatile char     uart_log_buf[UART_LOG_BUF_SIZE];
static volatile uint16_t uart_log_head = 0;
static volatile uint16_t uart_log_tail = 0;
static volatile uint8_t  uart_dma_busy = 0;
static char uart_dma_chunk[256];

static void USART3_SendText(const char *text)
{
    if (text == NULL) return;
    uint16_t len = (uint16_t)strlen(text);
    for (uint16_t i = 0; i < len; i++)
    {
        uint16_t next = (uart_log_head + 1U) % UART_LOG_BUF_SIZE;
        if (next == uart_log_tail) break;
        uart_log_buf[uart_log_head] = text[i];
        uart_log_head = next;
    }
}

static void USART3_SendBinary(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0U) return;
    for (uint16_t i = 0U; i < len; i++)
    {
        uint16_t next = (uart_log_head + 1U) % UART_LOG_BUF_SIZE;
        if (next == uart_log_tail) break;
        uart_log_buf[uart_log_head] = (char)data[i];
        uart_log_head = next;
    }
}

static void UART_LogFlush(void)
{
    if (uart_dma_busy) return;
    uint16_t h = uart_log_head;
    uint16_t t = uart_log_tail;
    if (h == t) return;

    uint16_t n = 0;
    while (t != h && n < sizeof(uart_dma_chunk))
    {
        uart_dma_chunk[n++] = uart_log_buf[t];
        t = (t + 1U) % UART_LOG_BUF_SIZE;
    }
    if (HAL_UART_Transmit_IT(&huart3, (uint8_t *)uart_dma_chunk, n) == HAL_OK)
    {
        uart_log_tail = t;
        uart_dma_busy = 1;
    }
}

/* HAL UART callbacks */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3)
        uart_dma_busy = 0;
}

static volatile uint8_t g_pcUartRxByte = 0U;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3)
    {
        UartProto_FeedRxByte(g_pcUartRxByte);
        (void)HAL_UART_Receive_IT(&huart3, (uint8_t *)&g_pcUartRxByte, 1U);
    }
}

/* ── Application entry point ─────────────────────────────────────────────── */
int main(void)
{
    MPU_Config();
    SCB_EnableICache();
    SCB_EnableDCache();

    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    IO_Init();           /* I/O 확장: DO/DI/AI/PWM 초기화 */
    MX_USART3_Init();

    /* Arm first UART RX byte */
    (void)HAL_UART_Receive_IT(&huart3, (uint8_t *)&g_pcUartRxByte, 1U);

    osKernelInitialize();

    defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

    static const osThreadAttr_t ethercatTask_attributes = {
        .name       = "EtherCATTask",
        .stack_size = 4096 * 4,
        .priority   = (osPriority_t)osPriorityRealtime,
    };
    osThreadNew(EtherCAT_Task, NULL, &ethercatTask_attributes);

    osKernelStart();
    while (1) {}
}

/* ── USART3 init (NUCLEO-H753ZI VCP: PD8=TX PD9=RX) ─────────────────────── */
static void MX_USART3_Init(void)
{
    huart3.Instance          = USART3;
    huart3.Init.BaudRate     = 921600;
    huart3.Init.WordLength   = UART_WORDLENGTH_8B;
    huart3.Init.StopBits     = UART_STOPBITS_1;
    huart3.Init.Parity       = UART_PARITY_NONE;
    huart3.Init.Mode         = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart3) != HAL_OK)
        Error_Handler();
}

/* ── GPIO init (NUCLEO-H753ZI LEDs: PB0 green / PE1 yellow / PB14 red) ───── */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    HAL_GPIO_WritePin(LED_GREEN_PORT,  LED_GREEN_PIN,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_YELLOW_PORT, LED_YELLOW_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_PORT,    LED_RED_PIN,    GPIO_PIN_RESET);

    gpio.Pin   = LED_GREEN_PIN | LED_RED_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = LED_YELLOW_PIN;
    HAL_GPIO_Init(GPIOE, &gpio);
}

/* ── LAN8742 PHY helpers ─────────────────────────────────────────────────── */
static int32_t ETH_PHY_IO_Init(void)     { HAL_ETH_SetMDIOClockRange(&heth); return 0; }
static int32_t ETH_PHY_IO_DeInit(void)   { return 0; }
static int32_t ETH_PHY_IO_GetTick(void)  { return (int32_t)HAL_GetTick(); }

static int32_t ETH_PHY_IO_ReadReg(uint32_t dev, uint32_t reg, uint32_t *val)
{
    return (HAL_ETH_ReadPHYRegister(&heth, dev, reg, val) == HAL_OK) ? 0 : -1;
}

static int32_t ETH_PHY_IO_WriteReg(uint32_t dev, uint32_t reg, uint32_t val)
{
    return (HAL_ETH_WritePHYRegister(&heth, dev, reg, val) == HAL_OK) ? 0 : -1;
}

static lan8742_Object_t lan8742;
static lan8742_IOCtx_t  lan8742_io_ctx = {
    ETH_PHY_IO_Init, ETH_PHY_IO_DeInit,
    ETH_PHY_IO_WriteReg, ETH_PHY_IO_ReadReg, ETH_PHY_IO_GetTick
};

/* ── ETH init ────────────────────────────────────────────────────────────── */
static void MX_ETH_Init(void)
{
    static uint8_t MACAddr[6] = {0x00, 0x80, 0xE1, 0x00, 0x00, 0x01};

    heth.Instance            = ETH;
    heth.Init.MACAddr        = MACAddr;
    heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
    heth.Init.TxDesc         = DMATxDscrTab;
    heth.Init.RxDesc         = DMARxDscrTab;
    heth.Init.RxBuffLen      = 1536;

    g_soemErrorCode = 100;
    if (HAL_ETH_Init(&heth) != HAL_OK)
    {
        g_ethLinkStatus = 0;
        g_soemErrorCode = 131;
        return;
    }

    /* Promiscuous mode for EtherCAT */
    ETH_MACFilterConfigTypeDef fc;
    HAL_ETH_GetMACFilterConfig(&heth, &fc);
    fc.PromiscuousMode  = ENABLE;
    fc.ReceiveAllMode   = ENABLE;
    fc.PassAllMulticast = ENABLE;
    HAL_ETH_SetMACFilterConfig(&heth, &fc);

    ETH_MACConfigTypeDef mc;
    if (HAL_ETH_GetMACConfig(&heth, &mc) == HAL_OK)
    {
        mc.LoopbackMode = DISABLE;
        (void)HAL_ETH_SetMACConfig(&heth, &mc);
    }

    LAN8742_RegisterBusIO(&lan8742, &lan8742_io_ctx);

    /* Probe PHY address */
    uint8_t phyAddr = 0xFF;
    for (uint32_t addr = 0; addr < 32U; addr++)
    {
        uint32_t bsr = 0U;
        if (HAL_ETH_ReadPHYRegister(&heth, addr, LAN8742_BSR, &bsr) == HAL_OK &&
            bsr != 0x0000U && bsr != 0xFFFFU)
        {
            phyAddr = (uint8_t)addr;
            break;
        }
    }
    lan8742.DevAddr = (phyAddr != 0xFFU) ? phyAddr : 0U;

    if (LAN8742_Init(&lan8742) != LAN8742_STATUS_OK)
    {
        g_ethLinkStatus = 0;
        g_soemErrorCode = 190;
        return;
    }

    if (HAL_ETH_Start(&heth) == HAL_OK)
    {
        int32_t ls = LAN8742_GetLinkState(&lan8742);
        g_ethLinkStatus = (ls > LAN8742_STATUS_LINK_DOWN) ? 1U : 0U;
        g_soemErrorCode = (g_ethLinkStatus == 1U) ? 300U : 301U;
    }
    else
    {
        g_ethLinkStatus = 0;
        g_soemErrorCode = 250;
    }
}

/* ── EtherCAT Task (1 ms, highest priority) ──────────────────────────────── */
void EtherCAT_Task(void *argument)
{
    (void)argument;

    #define LD1_PORT LED_GREEN_PORT
    #define LD1_PIN  LED_GREEN_PIN
    #define LD2_PORT LED_YELLOW_PORT
    #define LD2_PIN  LED_YELLOW_PIN

    osDelay(500);

    for (int i = 0; i < 3; i++)
    {
        HAL_GPIO_WritePin(LD1_PORT, LD1_PIN, GPIO_PIN_SET);
        osDelay(100);
        HAL_GPIO_WritePin(LD1_PORT, LD1_PIN, GPIO_PIN_RESET);
        osDelay(100);
    }

    USART3_SendText("[ETH] task start\r\n");
    MX_ETH_Init();

    /* Wait for PHY link (up to ~6 s) */
    int32_t linkState = LAN8742_STATUS_LINK_DOWN;
    for (int i = 0; i < 60; i++)
    {
        linkState = LAN8742_GetLinkState(&lan8742);
        if (linkState > LAN8742_STATUS_LINK_DOWN) break;
        HAL_GPIO_TogglePin(LD2_PORT, LD2_PIN);
        osDelay(100);
    }

    if (linkState <= LAN8742_STATUS_LINK_DOWN)
    {
        g_soemErrorCode = 239;
        for (;;) { HAL_GPIO_TogglePin(LD2_PORT, LD2_PIN); osDelay(200); }
    }

    g_ethLinkStatus = 1;

    extern void osal_dwt_init(void);
    osal_dwt_init();

    USART3_SendText("\r\n[SOEM] Initializing EtherCAT master...\r\n");

    SOEM_PortSetLog(USART3_SendText);
    SOEM_PortInit();

    /* Load axis parameters from flash */
    AxisConfig_InitDefaults();
    if (RobotFlash_Load() != 0U)
    {
        for (uint8_t ax = 0U; ax < (uint8_t)AXIS_COUNT; ax++)
            SOEM_LoadHomeHwOffset((AxisId_t)ax, g_axis_param[ax].home_offset);
    }
    SOEM_SyncRtFromAxisParam();
    SOEM_RefreshAllLimits();

    UartProto_Init(USART3_SendBinary);
    SOEM_PortSetLog(UartProto_SendLog);

    USART3_SendText("[SOEM] Starting cyclic processing\r\n");
    g_soemErrorCode = 310;

    /* DWT cycle counter for timing */
    volatile uint32_t *DWT_CYCCNT = (volatile uint32_t *)0xE0001004U;
    const uint32_t cpu_freq = SystemCoreClock;
    uint32_t cyc_prev     = *DWT_CYCCNT;
    uint32_t cyc_min      = 0xFFFFFFFFU;
    uint32_t cyc_max      = 0U;
    uint64_t cyc_sum      = 0U;
    uint32_t cyc_cnt      = 0U;
    uint32_t cyc_poll_max = 0U;

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(1);

    for (;;)
    {
        uint32_t poll_start = *DWT_CYCCNT;

        SOEM_PortPoll();

        uint32_t poll_elapsed = *DWT_CYCCNT - poll_start;
        if (poll_elapsed > cyc_poll_max) cyc_poll_max = poll_elapsed;

        UART_LogFlush();

        /* Cycle timing stats every 5 s */
        uint32_t cyc_now   = *DWT_CYCCNT;
        uint32_t cyc_delta = cyc_now - cyc_prev;
        cyc_prev = cyc_now;

        if (cyc_cnt > 0U)
        {
            if (cyc_delta < cyc_min) cyc_min = cyc_delta;
            if (cyc_delta > cyc_max) cyc_max = cyc_delta;
            cyc_sum += cyc_delta;
        }
        cyc_cnt++;

        if (cyc_cnt > 0U && (cyc_cnt % 30000U) == 0U)
        {
            uint32_t avg_us  = (uint32_t)(cyc_sum / (cyc_cnt - 1U) / (cpu_freq / 1000000U));
            uint32_t min_us  = cyc_min / (cpu_freq / 1000000U);
            uint32_t max_us  = cyc_max / (cpu_freq / 1000000U);
            uint32_t poll_us = cyc_poll_max / (cpu_freq / 1000000U);
            char tbuf[160];
            snprintf(tbuf, sizeof(tbuf),
                     "[TIMING] cycles=%lu avg=%lu us min=%lu us max=%lu us poll_max=%lu us\r\n",
                     (unsigned long)cyc_cnt, (unsigned long)avg_us,
                     (unsigned long)min_us,  (unsigned long)max_us,
                     (unsigned long)poll_us);
            UartProto_SendLog(tbuf);
            cyc_min = 0xFFFFFFFFU;
            cyc_max = 0U;
            cyc_sum = 0U;
            cyc_cnt = 1U;
            cyc_poll_max = 0U;
        }

        HAL_GPIO_TogglePin(LD1_PORT, LD1_PIN);
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

/* ── Default Task (10 ms, low priority) ──────────────────────────────────── */
void StartDefaultTask(void *argument)
{
    (void)argument;

    USART3_SendText("[AGV] DefaultTask start\r\n");

    uint32_t io_tick = 0U;   /* IO_STATUS 200 ms 타이머 */

    for (;;)
    {
        /* Dispatch incoming SLIP packets (AGV_VELOCITY, IO_SET, etc.) */
        UartProto_PollRx();

        /* Broadcast AGV odometry + status every 10 ms */
        UartProto_SendStatus();

        /* IO_STATUS 브로드캐스트 200 ms 주기 */
        if (++io_tick >= IO_STATUS_TICKS)
        {
            io_tick = 0U;
            UartProto_SendIoStatus();
        }

        /* Flash save deferred from EtherCAT_Task */
        if (g_flashSavePending != 0U)
        {
            g_flashSavePending = 0U;
            (void)RobotFlash_Save();
        }

        osDelay(10);
    }
}

/* ── MPU config (ETH DMA descriptors non-cacheable at 0x24070000) ─────────── */
static void MPU_Config(void)
{
    MPU_Region_InitTypeDef MPU_Init = {0};
    HAL_MPU_Disable();

    MPU_Init.Enable           = MPU_REGION_ENABLE;
    MPU_Init.Number           = MPU_REGION_NUMBER0;
    MPU_Init.BaseAddress      = 0x24070000U;
    MPU_Init.Size             = MPU_REGION_SIZE_64KB;
    MPU_Init.SubRegionDisable = 0x00U;
    MPU_Init.TypeExtField     = MPU_TEX_LEVEL1;
    MPU_Init.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_Init.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    MPU_Init.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    MPU_Init.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    MPU_Init.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&MPU_Init);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/* ── SystemClock: HSE 8 MHz → PLL1 → 480 MHz (NUCLEO-H753ZI) ─────────────── */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = 4U;
    osc.PLL.PLLN       = 480U;
    osc.PLL.PLLP       = 2U;
    osc.PLL.PLLQ       = 4U;
    osc.PLL.PLLR       = 2U;
    osc.PLL.PLLRGE     = RCC_PLL1VCIRANGE_2;
    osc.PLL.PLLVCOSEL  = RCC_PLL1VCOWIDE;
    osc.PLL.PLLFRACN   = 0U;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                         RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.SYSCLKDivider  = RCC_SYSCLK_DIV1;
    clk.AHBCLKDivider  = RCC_HCLK_DIV2;
    clk.APB3CLKDivider = RCC_APB3_DIV2;
    clk.APB1CLKDivider = RCC_APB1_DIV2;
    clk.APB2CLKDivider = RCC_APB2_DIV2;
    clk.APB4CLKDivider = RCC_APB4_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) Error_Handler();
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file; (void)line;
}
#endif
