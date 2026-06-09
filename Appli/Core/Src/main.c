/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "settings_persistence.h"
#include "ui_flash_storage.h"
#include "axis_config.h"
#include "interpolator.h"
#include "soem/soem.h"
#include "osal.h"
#include "lan8742.h"
#include "soem_port.h"
#include "task.h"  /* vTaskDelayUntil */
#include "interlock_manager.h"
#include "press_state_machine.h"
#include "recipe_manager.h"
#include "cycle_logger.h"
#include "alarm_manager.h"
#include "user_auth.h"
#include "maint_counter.h"
#include "uart_protocol.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Non-blocking UART ring buffer for EtherCAT logging */
#define UART_LOG_BUF_SIZE 4096U  /* enlarged for graph streaming */
static volatile char uart_log_buf[UART_LOG_BUF_SIZE];
static volatile uint16_t uart_log_head = 0;  /* written by producer (EtherCAT task) */
static volatile uint16_t uart_log_tail = 0;  /* consumed by DMA/flush task */
static volatile uint8_t  uart_dma_busy = 0;  /* 1 = DMA transfer in flight */
static char uart_dma_chunk[256];  /* DMA source buffer in normal RAM */

/* ── High-resolution press graph capture (1ms EtherCAT loop) ─────────────── */
#define GRAPH_BUF_MAX     2000U  /* 2 ms × 2000 = 4 s max cycle               */
#define GRAPH_SAMPLE_DIV     2U  /* sample every 2ms inside 1ms loop           */
#define GRAPH_STREAM_DIV     2U  /* send 1 ASCII line every 2ms → < UART speed */

typedef struct {
    int32_t  pos;      /* mm (soem_hw_to_user units, already converted)       */
    int16_t  torque;   /* 0.1% of rated torque                                */
    int16_t  velocity; /* user unit/s (from drive TxPDO)                      */
    uint16_t ms;       /* elapsed ms since cycle capture start                */
    uint8_t  step;     /* PressState_t value                                  */
    uint8_t  _pad;
} GraphSample_t;

static GraphSample_t g_graphBuf[GRAPH_BUF_MAX];
static uint16_t      g_graphCount       = 0U;
static uint16_t      g_graphStreamCount = 0U; /* frozen count at stream trigger */
static uint8_t       g_graphActive      = 0U; /* 1 = sampling in progress      */
static uint8_t       g_graphSendPend    = 0U; /* 1 = waiting to stream to PC   */
static uint16_t      g_graphSendIdx     = 0U; /* next sample index to send     */
static uint32_t      g_graphDivCnt      = 0U; /* sample decimation counter     */
static uint32_t      g_graphStreamDiv   = 0U; /* stream rate limiter counter   */
static uint32_t      g_graphStartMs     = 0U; /* HAL_GetTick() at capture start */

/* Set to 1 while validating CN3 pin 2/3 short loopback, set back to 0 afterwards. */
#define WIFI_UART7_LOOPBACK_TEST 0
/* Set to 1 while validating SPI4 MOSI/MISO loopback in SPI bridge mode.
 * Validated route on STM32H7S78-DK: AF5 + SSOE.
 */
#define WIFI_SPI4_LOOPBACK_TEST 0
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

UART_HandleTypeDef huart3;

ETH_HandleTypeDef heth;
/* ETH DMA descriptors: AXI SRAM non-cacheable region (MPU Region 0 at 0x24070000) */
__ALIGN_BEGIN ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT] __ALIGN_END __attribute__((section(".eth_dma"), aligned(32)));
__ALIGN_BEGIN ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT] __ALIGN_END __attribute__((section(".eth_dma"), aligned(32)));

/* Debug status variables */
volatile uint8_t g_ethLinkStatus = 0;
volatile uint8_t g_soemInitStatus = 0;
volatile uint8_t g_slaveCount = 0;
volatile uint32_t g_soemErrorCode = 0;

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
extern volatile int g_soem_sockhandle_value;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
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

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Non-blocking UART: push text into ring buffer, flushed by UART_LogFlush() */
static void USART3_SendText(const char *text)
{
  if (text == NULL) return;
  uint16_t len = (uint16_t)strlen(text);
  for (uint16_t i = 0; i < len; i++)
  {
    uint16_t next = (uart_log_head + 1U) % UART_LOG_BUF_SIZE;
    if (next == uart_log_tail)
      break;  /* buffer full — drop oldest bytes silently */
    uart_log_buf[uart_log_head] = text[i];
    uart_log_head = next;
  }
}

/* Binary TX callback for UartProto — pushes raw bytes into the same ring buffer */
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

/* Flush ring buffer to UART via interrupt (call from low-priority context) */
static void UART_LogFlush(void)
{
  if (uart_dma_busy) return;  /* previous transfer still in flight */
  uint16_t h = uart_log_head;
  uint16_t t = uart_log_tail;
  if (h == t) return;  /* nothing to send */

  /* Copy one chunk from ring buffer. Commit tail only when IT TX starts. */
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

#define PC_CMD_LINE_MAX 192U
#define PC_PROGRAM_POSITION_TOLERANCE 3
#define PC_PROGRAM_RETURN_TOLERANCE_HW 5
#define PC_UART_RX_RING_SIZE 512U
#define PC_UART_RX_POLL_BUDGET 32U

typedef struct
{
  uint8_t  parameterReadPending;
  uint32_t parameterReadStartMs;
} PcCommandContext;

static PcCommandContext g_pcCmd;
static char g_pcCmdLine[PC_CMD_LINE_MAX];
static uint16_t g_pcCmdLen = 0U;
static volatile uint8_t g_pcUartRxRing[PC_UART_RX_RING_SIZE];
static volatile uint16_t g_pcUartRxHead = 0U;
static volatile uint16_t g_pcUartRxTail = 0U;
static uint8_t g_pcUartRxByte = 0U;

/* Async flash save flags — set in EtherCAT_Task, consumed in DefaultTask.
 * Flash erase can take seconds and must NOT run inside the 1ms EtherCAT loop. */
#define FLASH_SAVE_HOME    0x01U
#define FLASH_SAVE_PARAM   0x02U
#define FLASH_SAVE_PROGRAM 0x04U
#define FLASH_SAVE_MANUAL  0x08U
volatile uint8_t  g_flashSavePending  = 0U;

static void Pc_ProcessCommandLine(char *line);

static uint8_t Pc_ToLowerAscii(uint8_t c)
{
  if (c >= (uint8_t)'A' && c <= (uint8_t)'Z')
  {
    return (uint8_t)(c + ((uint8_t)'a' - (uint8_t)'A'));
  }
  return c;
}

static uint8_t Pc_StartsWithIgnoreCase(const char *text, const char *prefix)
{
  if (text == NULL || prefix == NULL)
  {
    return 0U;
  }

  while (*prefix != '\0')
  {
    if (*text == '\0')
    {
      return 0U;
    }
    if (Pc_ToLowerAscii((uint8_t)*text) != Pc_ToLowerAscii((uint8_t)*prefix))
    {
      return 0U;
    }
    text++;
    prefix++;
  }
  return 1U;
}

static char* Pc_Trim(char *s)
{
  char *end;
  if (s == NULL)
  {
    return s;
  }

  while (*s == ' ' || *s == '\t')
  {
    s++;
  }

  end = s + strlen(s);
  while (end > s)
  {
    char c = *(end - 1);
    if (c != ' ' && c != '\t')
    {
      break;
    }
    end--;
  }
  *end = '\0';
  return s;
}

static void Pc_CmdReply(const char *fmt, ...)
{
  char msg[192];
  va_list args;
  int len;

  va_start(args, fmt);
  len = vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);

  if (len < 0)
  {
    return;
  }

  if (len >= (int)sizeof(msg))
  {
    len = (int)sizeof(msg) - 1;
    msg[len] = '\0';
  }

  USART3_SendText(msg);
  USART3_SendText("\r\n");
}


static void Pc_CommandInit(void)
{
  uint8_t ax;

  memset(&g_pcCmd, 0, sizeof(g_pcCmd));

  AxisConfig_InitDefaults();
  if (RobotFlash_Load() != 0U)
  {
    for (ax = 0U; ax < (uint8_t)AXIS_COUNT; ax++)
    {
      SOEM_LoadHomeHwOffset((AxisId_t)ax, g_axis_param[ax].home_offset);
    }
  }
  SOEM_SyncRtFromAxisParam();
  SOEM_RefreshAllLimits();
}

static void Pc_CommandHandleByte(uint8_t ch)
{
  if (ch == (uint8_t)'\r')
  {
    return;
  }

  if (ch == (uint8_t)'\n')
  {
    g_pcCmdLine[g_pcCmdLen] = '\0';
    if (g_pcCmdLen > 0U)
    {
      Pc_ProcessCommandLine(g_pcCmdLine);
    }
    g_pcCmdLen = 0U;
    return;
  }

  if (g_pcCmdLen < (PC_CMD_LINE_MAX - 1U))
  {
    g_pcCmdLine[g_pcCmdLen++] = (char)ch;
  }
  else
  {
    g_pcCmdLen = 0U;
  }
}

static void Pc_CommandEnsureRxArmed(void)
{
  if (huart3.RxState == HAL_UART_STATE_READY)
  {
    (void)HAL_UART_Receive_IT(&huart3, &g_pcUartRxByte, 1U);
  }
}

static void Pc_ProcessCommandLine(char *line)
{
  char *payload = Pc_Trim(line);
  char *eq;
  char *key;

  if (payload == NULL || *payload == '\0')
  {
    return;
  }

  if (Pc_StartsWithIgnoreCase(payload, "CMD,"))
  {
    payload += 4;
  }

  eq = strchr(payload, '=');
  if (eq == NULL)
  {
    return;
  }

  *eq = '\0';
  key = Pc_Trim(payload);

  {
    char msg[96];
    (void)snprintf(msg, sizeof(msg), "[CMD] unknown key=%s", (key != NULL) ? key : "?");
    Pc_CmdReply(msg);
  }
}

static void Pc_CommandPollUart(void)
{
  uint8_t ch;
  uint16_t budget = PC_UART_RX_POLL_BUDGET;

  /* Keep work bounded each cycle to reduce jitter on the 1ms control loop. */
  while (g_pcUartRxTail != g_pcUartRxHead && budget > 0U)
  {
    ch = g_pcUartRxRing[g_pcUartRxTail];
    g_pcUartRxTail = (uint16_t)((g_pcUartRxTail + 1U) % PC_UART_RX_RING_SIZE);
    Pc_CommandHandleByte(ch);
    budget--;
  }

  Pc_CommandEnsureRxArmed();
}

static void Pc_CommandTick(void)
{
}

/* Wi-Fi/UART7/SPI4 diagnostics are disabled in this firmware branch. */
#if 0
static void WIFI_UART7_LoopbackSelfTest(void)
{
#if WIFI_UART7_LOOPBACK_TEST
  static const uint8_t txPattern[] = {'A', 'T', '\r', '\n'};
  uint8_t rxByte = 0U;
  uint8_t rxCapture[8] = {0};
  uint32_t rxCount = 0U;
  uint32_t txOk = 0U;
  uint32_t pass = 0U;
  uint32_t start = 0U;
  char msg[160];

  /* Drain stale UART7 bytes first. */
  for (uint32_t i = 0U; i < 32U; i++)
  {
    if (HAL_UART_Receive(&huart7, &rxByte, 1U, 2U) != HAL_OK)
    {
      break;
    }
  }

  if (HAL_UART_Transmit(&huart7, (uint8_t*)txPattern, (uint16_t)sizeof(txPattern), 100U) == HAL_OK)
  {
    txOk = 1U;
  }

  start = HAL_GetTick();
  while ((HAL_GetTick() - start) < 250U)
  {
    if (HAL_UART_Receive(&huart7, &rxByte, 1U, 5U) == HAL_OK)
    {
      if (rxCount < (uint32_t)sizeof(rxCapture))
      {
        rxCapture[rxCount] = rxByte;
      }
      rxCount++;
      if ((rxCount >= (uint32_t)sizeof(txPattern)) &&
          (memcmp(rxCapture, txPattern, sizeof(txPattern)) == 0))
      {
        pass = 1U;
        break;
      }
    }
  }

  (void)snprintf(msg,
                 sizeof(msg),
                 "[WIFI] UART7 loopback tx_ok=%lu rx=%lu first=%02X %02X %02X %02X pass=%lu\\r\\n",
                 (unsigned long)txOk,
                 (unsigned long)rxCount,
                 (unsigned int)rxCapture[0],
                 (unsigned int)rxCapture[1],
                 (unsigned int)rxCapture[2],
                 (unsigned int)rxCapture[3],
                 (unsigned long)pass);
  USART3_SendText(msg);
#endif
}

static void WIFI_UART7_QuickAtProbe(void)
{
  static const uint8_t atCmd[] = {'A', 'T', '\r', '\n'};
  uint8_t rxBuf[10] = {0};
  uint8_t rxByte = 0U;
  uint32_t probeOk = 0U;
  char msg[200];

  USART3_SendText("[WIFI] Switching to UART7 for MB1400...\r\n");

  /* Pulse CHIP_EN to wake module before first probe. */
  HAL_GPIO_WritePin(MB1400_CHIP_EN_GPIO_Port, MB1400_CHIP_EN_Pin, GPIO_PIN_RESET);
  osDelay(100);
  HAL_GPIO_WritePin(MB1400_CHIP_EN_GPIO_Port, MB1400_CHIP_EN_Pin, GPIO_PIN_SET);
  osDelay(500);

  for (uint32_t rtsMode = 0U; rtsMode < 2U; rtsMode++)
  {
    uint32_t txOk = 0U;
    uint32_t rxOk = 0U;
    GPIO_PinState rtsPin = (rtsMode == 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET;

    HAL_GPIO_WritePin(MB1400_RTS_GPIO_Port, MB1400_RTS_Pin, rtsPin);
    osDelay(20);

    /* Drain stale UART7 bytes. */
    for (uint32_t i = 0U; i < 32U; i++)
    {
      if (HAL_UART_Receive(&huart7, &rxByte, 1U, 2U) != HAL_OK)
      {
        break;
      }
    }

    memset(rxBuf, 0, sizeof(rxBuf));
    if (HAL_UART_Transmit(&huart7, (uint8_t*)atCmd, (uint16_t)sizeof(atCmd), 100U) == HAL_OK)
    {
      txOk = 1U;
    }

    if (HAL_UART_Receive(&huart7, rxBuf, (uint16_t)sizeof(rxBuf), 500U) == HAL_OK)
    {
      rxOk = 1U;
      probeOk = 1U;
    }

    (void)snprintf(msg,
                   sizeof(msg),
                   "[WIFI] UART7 probe rts=%lu tx_ok=%lu rx_ok=%lu first=%02X %02X %02X %02X\r\n",
                   (unsigned long)rtsMode,
                   (unsigned long)txOk,
                   (unsigned long)rxOk,
                   (unsigned int)rxBuf[0],
                   (unsigned int)rxBuf[1],
                   (unsigned int)rxBuf[2],
                   (unsigned int)rxBuf[3]);
    USART3_SendText(msg);

    if (rxOk != 0U)
    {
      /* Keep working RTS level for subsequent AT init. */
      break;
    }
  }

  if (probeOk == 0U)
  {
    USART3_SendText("[WIFI] Still no response on UART7. Check PE7/PE8 AF config and SB1/SB5/SB7.\r\n");
  }
}

#if WIFI_SPI4_LOOPBACK_TEST
static uint32_t g_spi4_last_sr_before = 0U;
static uint32_t g_spi4_last_sr_after = 0U;
static uint32_t g_spi4_last_stage = 0U;

static void WIFI_SPI4_GpioShortSelfTest(void)
{
#if WIFI_SPI4_LOOPBACK_TEST
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  uint32_t pdHigh = 0U;
  uint32_t puLow = 0U;
  uint32_t pass = 0U;
  char msg[160];

  __HAL_RCC_GPIOE_CLK_ENABLE();

  /* STMod+ SPI route on STM32H7S78-DK: PE14 (MOSI), PE13 (MISO). */
  GPIO_InitStruct.Pin = GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_14, GPIO_PIN_SET);
  HAL_Delay(1U);
  if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_13) == GPIO_PIN_SET)
  {
    pdHigh = 1U;
  }

  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_14, GPIO_PIN_RESET);
  HAL_Delay(1U);
  if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_13) == GPIO_PIN_RESET)
  {
    puLow = 1U;
  }

  pass = ((pdHigh != 0U) && (puLow != 0U)) ? 1U : 0U;

  (void)snprintf(msg,
                 sizeof(msg),
                 "[WIFI] SPI4 gpio-short pe14->pe13 pd_high=%lu pu_low=%lu pass=%lu\\r\\n",
                 (unsigned long)pdHigh,
                 (unsigned long)puLow,
                 (unsigned long)pass);
  USART3_SendText(msg);
#endif
}

static void WIFI_SPI4_ClearFlags(void)
{
#if WIFI_SPI4_LOOPBACK_TEST
  SPI4->IFCR = (SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_OVRC | SPI_IFCR_UDRC | SPI_IFCR_MODFC | SPI_IFCR_SUSPC);
#endif
}

static void WIFI_SPI4_InitForLoopback(uint32_t gpioAlternate, uint32_t cfg2ExtraBits)
{
#if WIFI_SPI4_LOOPBACK_TEST
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_SPI4_CLK_ENABLE();

  /* SPI4 on PE12(SCK), PE13(MISO), PE14(MOSI), PE10(CS) for STMod+ SPI mode. */
  GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = (uint32_t)gpioAlternate;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  __HAL_RCC_SPI4_FORCE_RESET();
  __HAL_RCC_SPI4_RELEASE_RESET();

  SPI4->CR1 = 0U;
  SPI4->CR2 = 0U;
  SPI4->IFCR = 0xFFFFFFFFUL;

  /* 8-bit data, conservative baud divider for first bring-up. */
  SPI4->CFG1 = ((7UL << SPI_CFG1_DSIZE_Pos) | SPI_CFG1_MBR_1 | SPI_CFG1_MBR_0);
  SPI4->CFG2 = (SPI_CFG2_MASTER | SPI_CFG2_SSM | cfg2ExtraBits);
  SPI4->CR1 = SPI_CR1_SSI;
  SET_BIT(SPI4->CR1, SPI_CR1_SPE);

  g_spi4_last_sr_before = SPI4->SR;
  if ((SPI4->SR & SPI_SR_MODF) != 0U)
  {
    WIFI_SPI4_ClearFlags();
  }
#endif
}

static uint8_t WIFI_SPI4_TransferByte(uint8_t txByte, uint8_t *rxByte, uint32_t timeoutMs)
{
#if WIFI_SPI4_LOOPBACK_TEST
  uint32_t tickStart;

  g_spi4_last_stage = 0U;
  g_spi4_last_sr_before = SPI4->SR;

  if (rxByte == NULL)
  {
    g_spi4_last_stage = 7U;
    g_spi4_last_sr_after = SPI4->SR;
    return 0U;
  }

  WIFI_SPI4_ClearFlags();

  MODIFY_REG(SPI4->CR2, SPI_CR2_TSIZE, 1UL);
  SET_BIT(SPI4->CR1, SPI_CR1_CSTART);

  tickStart = HAL_GetTick();
  while ((SPI4->SR & SPI_SR_TXP) == 0U)
  {
    if ((SPI4->SR & SPI_SR_MODF) != 0U)
    {
      g_spi4_last_stage = 4U;
      g_spi4_last_sr_after = SPI4->SR;
      WIFI_SPI4_ClearFlags();
      return 0U;
    }
    if ((HAL_GetTick() - tickStart) >= timeoutMs)
    {
      g_spi4_last_stage = 1U;
      g_spi4_last_sr_after = SPI4->SR;
      return 0U;
    }
  }
  *(__IO uint8_t*)&SPI4->TXDR = txByte;

  tickStart = HAL_GetTick();
  while ((SPI4->SR & SPI_SR_RXP) == 0U)
  {
    if ((SPI4->SR & SPI_SR_MODF) != 0U)
    {
      g_spi4_last_stage = 5U;
      g_spi4_last_sr_after = SPI4->SR;
      WIFI_SPI4_ClearFlags();
      return 0U;
    }
    if ((HAL_GetTick() - tickStart) >= timeoutMs)
    {
      g_spi4_last_stage = 2U;
      g_spi4_last_sr_after = SPI4->SR;
      return 0U;
    }
  }
  *rxByte = *(__IO uint8_t*)&SPI4->RXDR;

  tickStart = HAL_GetTick();
  while ((SPI4->SR & SPI_SR_EOT) == 0U)
  {
    if ((SPI4->SR & SPI_SR_MODF) != 0U)
    {
      g_spi4_last_stage = 6U;
      g_spi4_last_sr_after = SPI4->SR;
      WIFI_SPI4_ClearFlags();
      return 0U;
    }
    if ((HAL_GetTick() - tickStart) >= timeoutMs)
    {
      g_spi4_last_stage = 3U;
      g_spi4_last_sr_after = SPI4->SR;
      return 0U;
    }
  }

  g_spi4_last_stage = 100U;
  g_spi4_last_sr_after = SPI4->SR;
  WIFI_SPI4_ClearFlags();
  return 1U;
#else
  (void)txByte;
  (void)rxByte;
  (void)timeoutMs;
  return 0U;
#endif
}

static void WIFI_SPI4_LoopbackSelfTest(void)
{
#if WIFI_SPI4_LOOPBACK_TEST
  typedef struct
  {
    uint32_t gpioAlternate;
    uint32_t cfg2ExtraBits;
    const char* name;
  } WIFI_SPI4_TestCase;

  static const WIFI_SPI4_TestCase testCases[] = {
    {GPIO_AF5_SPI4, 0U, "af5"},
    {GPIO_AF6_SPI4, 0U, "af6"},
    {GPIO_AF5_SPI4, SPI_CFG2_IOSWP, "af5+ioswp"},
    {GPIO_AF6_SPI4, SPI_CFG2_IOSWP, "af6+ioswp"},
    {GPIO_AF5_SPI4, SPI_CFG2_AFCNTR, "af5+afcntr"},
    {GPIO_AF6_SPI4, SPI_CFG2_AFCNTR, "af6+afcntr"},
    {GPIO_AF5_SPI4, SPI_CFG2_SSOE, "af5+ssoe"},
    {GPIO_AF6_SPI4, SPI_CFG2_SSOE, "af6+ssoe"},
    {GPIO_AF5_SPI4, (SPI_CFG2_IOSWP | SPI_CFG2_AFCNTR), "af5+ioswp+afcntr"},
    {GPIO_AF6_SPI4, (SPI_CFG2_IOSWP | SPI_CFG2_AFCNTR), "af6+ioswp+afcntr"},
    {GPIO_AF5_SPI4, (SPI_CFG2_IOSWP | SPI_CFG2_SSOE), "af5+ioswp+ssoe"},
    {GPIO_AF6_SPI4, (SPI_CFG2_IOSWP | SPI_CFG2_SSOE), "af6+ioswp+ssoe"}
  };
  static const uint8_t txPattern[] = {0xA5U, 0x5AU, 0x3CU, 0xC3U};
  uint8_t rxPattern[sizeof(txPattern)] = {0};
  uint32_t selectedCase = 0xFFFFFFFFUL;
  char msg[240];

  WIFI_SPI4_GpioShortSelfTest();

  for (uint32_t caseIndex = 0U; caseIndex < (uint32_t)(sizeof(testCases) / sizeof(testCases[0])); caseIndex++)
  {
    uint32_t txCount = 0U;
    uint32_t rxMatch = 0U;
    uint32_t pass = 1U;
    uint32_t sr = 0U;

    memset(rxPattern, 0, sizeof(rxPattern));
    WIFI_SPI4_InitForLoopback(testCases[caseIndex].gpioAlternate, testCases[caseIndex].cfg2ExtraBits);

    for (uint32_t i = 0U; i < (uint32_t)sizeof(txPattern); i++)
    {
      uint8_t rxByte = 0U;
      if (WIFI_SPI4_TransferByte(txPattern[i], &rxByte, 20U) == 0U)
      {
        pass = 0U;
        break;
      }

      txCount++;
      rxPattern[i] = rxByte;
      if (rxByte == txPattern[i])
      {
        rxMatch++;
      }
      else
      {
        pass = 0U;
      }
    }

    sr = SPI4->SR;
    CLEAR_BIT(SPI4->CR1, SPI_CR1_SPE);

    (void)snprintf(msg,
                   sizeof(msg),
                   "[WIFI] SPI4 probe=%s tx=%lu match=%lu stage=%lu sr0=%08lX sr1=%08lX sr=%08lX rx=%02X %02X %02X %02X pass=%lu\\r\\n",
                   testCases[caseIndex].name,
                   (unsigned long)txCount,
                   (unsigned long)rxMatch,
                   (unsigned long)g_spi4_last_stage,
                   (unsigned long)g_spi4_last_sr_before,
                   (unsigned long)g_spi4_last_sr_after,
                   (unsigned long)sr,
                   (unsigned int)rxPattern[0],
                   (unsigned int)rxPattern[1],
                   (unsigned int)rxPattern[2],
                   (unsigned int)rxPattern[3],
                   (unsigned long)pass);
    USART3_SendText(msg);

    if (pass != 0U)
    {
      selectedCase = caseIndex;
      break;
    }
  }

  if (selectedCase != 0xFFFFFFFFUL)
  {
    (void)snprintf(msg,
                   sizeof(msg),
                   "[WIFI] SPI4 loopback route=%s selected\\r\\n",
                   testCases[selectedCase].name);
    USART3_SendText(msg);
  }
  else
  {
    USART3_SendText("[WIFI] SPI4 loopback no valid route found\\r\\n");
  }
#endif
}
#endif
#endif /* Wi-Fi/UART7/SPI4 diagnostics disabled */

/* Called by HAL when IT TX completes */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART3)
  {
    uart_dma_busy = 0;
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART3)
  {
    UartProto_FeedRxByte(g_pcUartRxByte);
    (void)HAL_UART_Receive_IT(&huart3, &g_pcUartRxByte, 1U);
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* MPU must be configured before enabling caches */
  MPU_Config();

  SCB_EnableICache();
  SCB_EnableDCache();

  HAL_Init();

  SystemClock_Config();

  SettingsPersistence_Init();

  MX_GPIO_Init();
  MX_USART3_Init();
  Pc_CommandEnsureRxArmed();

  /* ETH init deferred to EtherCAT_Task to avoid blocking main() */

  osKernelInitialize();

  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  static const osThreadAttr_t ethercatTask_attributes = {
    .name = "EtherCATTask",
    .stack_size = 4096 * 4,
    .priority = (osPriority_t) osPriorityRealtime,
  };
  osThreadNew(EtherCAT_Task, NULL, &ethercatTask_attributes);

  osKernelStart();

  while (1) {}
}

/**
  * @brief USART3 Initialization (NUCLEO-H753ZI VCP: PD8=TX, PD9=RX)
  */
static void MX_USART3_Init(void)
{
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 921600;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization (NUCLEO-H753ZI: LEDs PB0/PE1/PB14 + ETH port clocks)
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /* User LEDs OFF initially */
  HAL_GPIO_WritePin(LED_GREEN_PORT,  LED_GREEN_PIN,  GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_YELLOW_PORT, LED_YELLOW_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_RED_PORT,    LED_RED_PIN,    GPIO_PIN_RESET);

  /* LD1 Green (PB0), LD3 Red (PB14) */
  gpio.Pin   = LED_GREEN_PIN | LED_RED_PIN;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);

  /* LD2 Yellow (PE1) */
  gpio.Pin = LED_YELLOW_PIN;
  HAL_GPIO_Init(GPIOE, &gpio);
}

/* USER CODE BEGIN 4 */

/* LAN8742 PHY IO functions */
static int32_t ETH_PHY_IO_Init(void)
{
  HAL_ETH_SetMDIOClockRange(&heth);
  return 0;
}

static int32_t ETH_PHY_IO_DeInit(void)
{
  return 0;
}

static int32_t ETH_PHY_IO_ReadReg(uint32_t dev_addr, uint32_t reg_addr, uint32_t *p_reg_val)
{
  if (HAL_ETH_ReadPHYRegister(&heth, dev_addr, reg_addr, p_reg_val) != HAL_OK)
  {
    return -1;
  }
  return 0;
}

static int32_t ETH_PHY_IO_WriteReg(uint32_t dev_addr, uint32_t reg_addr, uint32_t reg_val)
{
  if (HAL_ETH_WritePHYRegister(&heth, dev_addr, reg_addr, reg_val) != HAL_OK)
  {
    return -1;
  }
  return 0;
}

static int32_t ETH_PHY_IO_GetTick(void)
{
  return (int32_t)HAL_GetTick();
}

static lan8742_Object_t lan8742;
static lan8742_IOCtx_t lan8742_io_ctx = {
  ETH_PHY_IO_Init,
  ETH_PHY_IO_DeInit,
  ETH_PHY_IO_WriteReg,
  ETH_PHY_IO_ReadReg,
  ETH_PHY_IO_GetTick
};

/**
  * @brief ETH Initialization Function
  * @param None
  * @retval None
  */
static void MX_ETH_Init(void)
{
  static uint8_t MACAddr[6] = {0x00, 0x80, 0xE1, 0x00, 0x00, 0x01};

  heth.Instance = ETH;
  heth.Init.MACAddr = MACAddr;
  heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
  heth.Init.TxDesc = DMATxDscrTab;
  heth.Init.RxDesc = DMARxDscrTab;
  heth.Init.RxBuffLen = 1536;

  g_soemErrorCode = 100;  /* Starting ETH init (10%) */

  /* Note: Clock configuration is done in HAL_ETH_MspInit (stm32h7xx_hal_msp.c) */
  
  HAL_StatusTypeDef status = HAL_ETH_Init(&heth);
  g_soemErrorCode = 120;  /* HAL_ETH_Init returned (12%) */
  
  if (status != HAL_OK)
  {
    g_ethLinkStatus = 0;
    g_soemErrorCode = 130 + status;  /* 131=Error, 132=Busy, 133=Timeout (13%) */
    return;
  }

  /* Enable promiscuous mode for EtherCAT */
  ETH_MACFilterConfigTypeDef filterConfig;
  HAL_ETH_GetMACFilterConfig(&heth, &filterConfig);
  filterConfig.PromiscuousMode = ENABLE;
  filterConfig.ReceiveAllMode = ENABLE;
  filterConfig.PassAllMulticast = ENABLE;
  HAL_ETH_SetMACFilterConfig(&heth, &filterConfig);
  
  g_soemErrorCode = 150;  /* ETH Init OK, starting PHY (15%) */

  /* Force MAC out of loopback if any previous config touched it */
  ETH_MACConfigTypeDef macConf;
  if (HAL_ETH_GetMACConfig(&heth, &macConf) == HAL_OK)
  {
    macConf.LoopbackMode = DISABLE;
    (void)HAL_ETH_SetMACConfig(&heth, &macConf);
  }

  /* Initialize LAN8742 PHY */
  LAN8742_RegisterBusIO(&lan8742, &lan8742_io_ctx);

  /* Probe PHY address (0..31) from BSR before LAN8742_Init */
  uint8_t phyAddrFound = 0xFF;
  for (uint32_t addr = 0; addr < 32U; addr++)
  {
    uint32_t bsr = 0U;
    if ((HAL_ETH_ReadPHYRegister(&heth, addr, LAN8742_BSR, &bsr) == HAL_OK) &&
        (bsr != 0x0000U) && (bsr != 0xFFFFU))
    {
      phyAddrFound = (uint8_t)addr;
      break;
    }
  }

  if (phyAddrFound != 0xFFU)
  {
    lan8742.DevAddr = phyAddrFound;
    g_soemErrorCode = 160 + phyAddrFound;  /* PHY addr found: 160..191 */
  }
  else
  {
    lan8742.DevAddr = 0U;
    g_soemErrorCode = 159;  /* PHY addr scan failed, fallback to 0 */
  }

  int32_t phyStatus = LAN8742_Init(&lan8742);
  if (phyStatus != LAN8742_STATUS_OK)
  {
    g_ethLinkStatus = 0;
    g_soemErrorCode = 190 + (-phyStatus);  /* PHY Init failed (19%) */
    return;
  }

  g_soemErrorCode = 200;  /* PHY Init OK, starting ETH (20%) */
  
  /* Start ETH - polling mode for SOEM */
  status = HAL_ETH_Start(&heth);
  if (status == HAL_OK)
  {
    int32_t linkState = LAN8742_GetLinkState(&lan8742);
    g_ethLinkStatus = (linkState > LAN8742_STATUS_LINK_DOWN) ? 1 : 0;
    g_soemErrorCode = (g_ethLinkStatus == 1) ? 300 : 301;  /* 300=link up, 301=ETH started but link down */
  }
  else
  {
    g_ethLinkStatus = 0;
    g_soemErrorCode = 250 + status;  /* HAL_ETH_Start failed (25%) */
  }
}

/* SOEM context and buffers */
static char IOmap[4096];
static ecx_contextt ecatContext;

/**
  * @brief  EtherCAT Task - handles SOEM initialization and cyclic processing
  * @param  argument: Not used
  * @retval None
  */
void EtherCAT_Task(void *argument)
{
  (void)argument;

  /* NUCLEO-H753ZI: LD1 Green = PB0, LD2 Yellow = PE1 */
  #define LD1_PORT LED_GREEN_PORT
  #define LD1_PIN  LED_GREEN_PIN
  #define LD2_PORT LED_YELLOW_PORT
  #define LD2_PIN  LED_YELLOW_PIN

  /* Wait for system to stabilize */
  osDelay(500);
  
  /* LED blink to indicate task started - LD1 (Green) */
  for (int i = 0; i < 3; i++)
  {
    HAL_GPIO_WritePin(LD1_PORT, LD1_PIN, GPIO_PIN_SET);
    osDelay(100);
    HAL_GPIO_WritePin(LD1_PORT, LD1_PIN, GPIO_PIN_RESET);
    osDelay(100);
  }

  USART3_SendText("[ETH] task start\r\n");

  /* Initialize ETH in this task to avoid blocking main() */
  MX_ETH_Init();

  /* Wait for PHY link up (auto-negotiation can take a few seconds) */
  int32_t linkStatePhy = LAN8742_STATUS_LINK_DOWN;
  for (int i = 0; i < 60; i++) /* up to ~6 seconds */
  {
    linkStatePhy = LAN8742_GetLinkState(&lan8742);
    if (linkStatePhy > LAN8742_STATUS_LINK_DOWN)
    {
      break;
    }

    g_soemErrorCode = 240 + (i % 10);  /* 240-249: waiting link */
    HAL_GPIO_TogglePin(LD2_PORT, LD2_PIN);
    osDelay(100);
  }

  if (linkStatePhy <= LAN8742_STATUS_LINK_DOWN)
  {
    g_soemErrorCode = 239;  /* link timeout */
    while (1)
    {
      HAL_GPIO_TogglePin(LD2_PORT, LD2_PIN);
      osDelay(200);
    }
  }

  g_ethLinkStatus = 1;
  g_soemErrorCode = 280;  /* link up, entering continuous TX test */

#ifndef SOEM_ENABLED
  /* === ETH-ONLY CONTINUOUS TX TEST === */
  {
    /* Simple test packet in RAM_CMD (MPU non-cacheable) for DMA */
    static uint8_t testPkt[64] __attribute__((section(".eth_dma"), aligned(32)));
    memset(testPkt, 0xAA, 64);
    /* Broadcast destination MAC */
    testPkt[0] = 0xFF; testPkt[1] = 0xFF; testPkt[2] = 0xFF;
    testPkt[3] = 0xFF; testPkt[4] = 0xFF; testPkt[5] = 0xFF;
    /* Source MAC */
    testPkt[6] = 0x02; testPkt[7] = 0x00; testPkt[8] = 0x00;
    testPkt[9] = 0x00; testPkt[10] = 0x00; testPkt[11] = 0x01;
    /* EtherType: EtherCAT (0x88A4) */
    testPkt[12] = 0x88; testPkt[13] = 0xA4;

    ETH_BufferTypeDef txBuf;
    ETH_TxPacketConfigTypeDef txCfg;
    txBuf.buffer = testPkt;
    txBuf.len = 64;
    txBuf.next = NULL;
    txCfg.Length = 64;
    txCfg.TxBuffer = &txBuf;
    txCfg.Attributes = ETH_TX_PACKETS_FEATURES_CRCPAD;
    txCfg.ChecksumCtrl = ETH_CHECKSUM_DISABLE;
    txCfg.CRCPadCtrl = ETH_CRC_PAD_INSERT;
    txCfg.SrcAddrCtrl = ETH_SRC_ADDR_CONTROL_DISABLE;

    /* Clean up any partially-configured TX descriptors before loop */
    for (uint32_t i = 0; i < ETH_TX_DESC_CNT; i++)
    {
      (void)HAL_ETH_ReleaseTxPacket(&heth);
    }
    
    /* Force reset of TX descriptor ring to start from desc[0] */
    heth.TxDescList.CurTxDesc = 0;
    heth.TxDescList.BuffersInUse = 0;
    heth.TxDescList.releaseIndex = 0;

    while (1)
    {
      static uint32_t tbuStreak = 0U;
      static uint32_t ethRestartCount = 0U;
      static uint32_t lastCode = 0U;
      static uint32_t uartMsgCount = 0U;

      /* Release any completed TX descriptors before transmitting */
      (void)HAL_ETH_ReleaseTxPacket(&heth);
      
      /* Check descriptor availability before transmit */
      if (heth.TxDescList.BuffersInUse >= ETH_TX_DESC_CNT)
      {
        /* All descriptors in use - force release */
        for (uint32_t i = 0; i < ETH_TX_DESC_CNT; i++)
        {
          (void)HAL_ETH_ReleaseTxPacket(&heth);
        }
      }
      
      HAL_StatusTypeDef txResult = HAL_ETH_Transmit(&heth, &txCfg, 500);
      uint32_t dmacsr = ETH->DMACSR;
      uint32_t txIrq = dmacsr & ETH_DMACSR_TI;
      uint32_t txBufUnavailable = dmacsr & ETH_DMACSR_TBU;
      uint32_t txProcStopped = dmacsr & ETH_DMACSR_TPS;
      uint32_t txEarlyIrq = dmacsr & ETH_DMACSR_ETI;
      uint32_t txFatalBusErr = dmacsr & ETH_DMACSR_FBE;
      uint32_t txErrBits = dmacsr & ETH_DMACSR_TEB;
      uint32_t maccr = ETH->MACCR;
      uint32_t dmactcr = ETH->DMACTCR;
      uint32_t dmacier = ETH->DMACIER;
      uint32_t dmactdlar = ETH->DMACTDLAR;
      uint32_t dmactdtpr = ETH->DMACTDTPR;
      uint32_t dmactdrlr = ETH->DMACTDRLR;
      uint32_t dmaccatdr = ETH->DMACCATDR;
      uint32_t curTxDesc = heth.TxDescList.CurTxDesc;
      uint32_t txInUse = heth.TxDescList.BuffersInUse;
      uint32_t txReleaseIdx = heth.TxDescList.releaseIndex;
      char line[512];

      if (txResult == HAL_OK)
      {
        (void)HAL_ETH_ReleaseTxPacket(&heth);
        g_soem_eth_state = heth.gState;

        /* Fatal or stopped TX path */
        if ((txFatalBusErr != 0U) || (txProcStopped != 0U) || (txErrBits != 0U))
        {
          g_soemErrorCode = 262;  /* DMA error while TX */
          g_soem_eth_error = dmacsr;
          HAL_GPIO_TogglePin(LD2_PORT, LD2_PIN);
        }
        else if (txBufUnavailable != 0U)
        {
          g_soemErrorCode = 263;  /* TX descriptor/buffer unavailable */
          g_soem_eth_error = dmacsr;
          HAL_GPIO_TogglePin(LD2_PORT, LD2_PIN);
        }
        else if (txIrq != 0U)
        {
          g_soemErrorCode = 281;  /* TX interrupt observed */
          HAL_GPIO_TogglePin(LD1_PORT, LD1_PIN);
        }
        else if (txEarlyIrq != 0U)
        {
          g_soemErrorCode = 283;  /* Early TX interrupt only */
          HAL_GPIO_TogglePin(LD1_PORT, LD1_PIN);
        }
        else
        {
          g_soemErrorCode = 282;  /* HAL OK but no DMA TX interrupt */
          HAL_GPIO_TogglePin(LD2_PORT, LD2_PIN);
        }
      }
      else
      {
        g_soemErrorCode = 261;  /* TX fail */
        g_soem_eth_error = heth.ErrorCode;
        g_soem_eth_state = heth.gState;
        HAL_GPIO_TogglePin(LD2_PORT, LD2_PIN);
      }

      if ((txBufUnavailable != 0U) && (txIrq == 0U))
      {
        tbuStreak++;
      }
      else
      {
        tbuStreak = 0U;
      }

      /* Recover from persistent TBU lock: restart MAC/DMA channel. */
      if (tbuStreak >= 20U)
      {
        (void)HAL_ETH_Stop(&heth);
        (void)HAL_ETH_Start(&heth);
        ethRestartCount++;
        tbuStreak = 0U;
        g_soemErrorCode = 264;
      }

      /* DMACSR bits are sticky W1C, clear bits we inspect each cycle. */
      ETH->DMACSR = (ETH_DMACSR_TI | ETH_DMACSR_TBU | ETH_DMACSR_ETI |
                     ETH_DMACSR_TPS | ETH_DMACSR_FBE | ETH_DMACSR_AIS |
                     ETH_DMACSR_NIS);

      /* Send UART diagnostic only once at startup */
      if (uartMsgCount == 0U)
      {
        uint32_t txd0 = DMATxDscrTab[0].DESC3;
        uint32_t txd1 = DMATxDscrTab[1].DESC3;
        uint32_t txd2 = DMATxDscrTab[2].DESC3;
        uint32_t txd3 = DMATxDscrTab[3].DESC3;
        
        int len = snprintf(line, sizeof(line),
                           "code   : %lu\r\n"
                           "dmacsr : 0x%08lX\r\n"
                           "flags  : TI=%lu TBU=%lu ETI=%lu TPS=%lu FBE=%lu TEB=0x%lX\r\n"
                           "regs   : MACCR=0x%08lX DMACTCR=0x%08lX DMACIER=0x%08lX\r\n"
                           "txreg  : TDLAR=0x%08lX TDTPR=0x%08lX TDRLR=0x%08lX CATDR=0x%08lX\r\n"
                           "ring   : cur=%lu inuse=%lu rel=%lu rst=%lu streak=%lu\r\n"
                           "desc3  : D0=0x%08lX D1=0x%08lX D2=0x%08lX D3=0x%08lX\r\n"
                           "\r\n",
                           (unsigned long)g_soemErrorCode,
                           (unsigned long)dmacsr,
                           (unsigned long)((txIrq != 0U) ? 1U : 0U),
                           (unsigned long)((txBufUnavailable != 0U) ? 1U : 0U),
                           (unsigned long)((txEarlyIrq != 0U) ? 1U : 0U),
                           (unsigned long)((txProcStopped != 0U) ? 1U : 0U),
                           (unsigned long)((txFatalBusErr != 0U) ? 1U : 0U),
                           (unsigned long)(txErrBits >> ETH_DMACSR_TEB_Pos),
                           (unsigned long)maccr,
                           (unsigned long)dmactcr,
                           (unsigned long)dmacier,
                           (unsigned long)dmactdlar,
                           (unsigned long)dmactdtpr,
                           (unsigned long)dmactdrlr,
                           (unsigned long)dmaccatdr,
                           (unsigned long)curTxDesc,
                           (unsigned long)txInUse,
                           (unsigned long)txReleaseIdx,
                           (unsigned long)ethRestartCount,
                           (unsigned long)tbuStreak,
                           (unsigned long)txd0,
                           (unsigned long)txd1,
                           (unsigned long)txd2,
                           (unsigned long)txd3);
        if (len > 0)
        {
          (void)HAL_UART_Transmit(&huart3, (uint8_t *)line, (uint16_t)len, 100);
        }
        uartMsgCount = 1U;  /* Prevent future output */
      }

      osDelay(100);
    }
  }
  /* === END ETH-ONLY TEST === */
#else
  /* === SOEM ETHERCAT MASTER === */
  /* Initialize DWT cycle counter for µs-resolution OSAL timing */
  extern void osal_dwt_init(void);
  osal_dwt_init();

  USART3_SendText("\r\n[SOEM] Initializing EtherCAT master...\r\n");
  
  /* Set up SOEM logging to UART */
  SOEM_PortSetLog(USART3_SendText);
  
  /* Initialize SOEM */
  SOEM_PortInit();
  Pc_CommandInit();
  UartProto_Init(USART3_SendBinary);

  /* Route SOEM log to SLIP-framed LOG packets so they appear in the Web HMI
   * LOG panel.  Must be called after UartProto_Init so g_tx_fn is valid. */
  SOEM_PortSetLog(UartProto_SendLog);

  /* Initialize Phase 1+2 modules */
  InterlockManager_Init();
  PressStateMachine_Init();
  RecipeManager_Init();
  CycleLogger_Init();
  AlarmManager_Init();
  UserAuth_Init();
  MaintCounter_Init();
  RecipeManager_ApplyActive();
  USART3_SendText("[PSM] Press SM + Recipe + Logger + Alarm + Auth + Maint initialized\r\n");

  /* Interpolation engine — must init after AxisConfig_InitDefaults() */
  Interp_Init();
  USART3_SendText("[INTERP] Interpolation engine ready\r\n");

  USART3_SendText("[SOEM] Starting cyclic processing\r\n");
  g_soemErrorCode = 310;  /* SOEM running */

  /* Precise 1ms cycle using vTaskDelayUntil */
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xPeriod = pdMS_TO_TICKS(1);  /* 1ms */

  /* DWT-based cycle time measurement */
  volatile uint32_t *DWT_CYCCNT_PTR  = (volatile uint32_t *)0xE0001004U;
  const uint32_t cpu_freq = SystemCoreClock;  /* 600 MHz */
  uint32_t cyc_prev = *DWT_CYCCNT_PTR;
  uint32_t cyc_min  = 0xFFFFFFFFU;
  uint32_t cyc_max  = 0U;
  uint64_t cyc_sum  = 0U;
  uint32_t cyc_cnt  = 0U;
  uint32_t cyc_poll_max = 0U;  /* max SOEM_PortPoll() time */

  /* Main EtherCAT cycle */
  while (1)
  {
    /* Handle PC-side command lines from UART4 (LCD clone control). */
    Pc_CommandPollUart();

    /* Run interpolator BEFORE SOEM poll so the new target positions are
     * already in g_rt[ax].target_hw_out when soem_update_target_output
     * writes them to the PDO.                                          */
    Interp_Tick();

    /* Measure SOEM_PortPoll execution time */
    uint32_t poll_start = *DWT_CYCCNT_PTR;

    /* Poll SOEM state machine */
    SOEM_PortPoll();
    Pc_CommandTick();

    /* Update interlock state and notify drive status */
    InterlockManager_Update();
    {
      const uint16_t sw = SOEM_GetStatusword_Legacy();
      uint8_t drive_op = (((sw & 0x006FU) == 0x0027U) &&
                          (SOEM_GetRunEnable_Legacy() != 0U) &&
                          (SOEM_GetPdoReady_Legacy() != 0U)) ? 1U : 0U;
      PressStateMachine_NotifyDriveState(drive_op);
    }

    /* Press state machine tick (runs only in AUTO mode) */
    PressStateMachine_Tick();

    /* ── Graph sampling (every GRAPH_SAMPLE_DIV ms) ─────────────────── */
    g_graphDivCnt++;
    if (g_graphDivCnt >= GRAPH_SAMPLE_DIV) {
        g_graphDivCnt = 0U;
        if (g_graphActive && (g_graphCount < GRAPH_BUF_MAX)) {
            uint32_t elapsed = HAL_GetTick() - g_graphStartMs;
            g_graphBuf[g_graphCount].pos      = (int32_t)SOEM_GetPositionActual();
            g_graphBuf[g_graphCount].torque   = (int16_t)SOEM_GetTorqueActual();
            g_graphBuf[g_graphCount].velocity = (int16_t)SOEM_GetVelocityActual();
            g_graphBuf[g_graphCount].ms       = (uint16_t)(elapsed > 0xFFFFU ? 0xFFFFU : elapsed);
            g_graphBuf[g_graphCount].step     = (uint8_t)PressStateMachine_GetState();
            g_graphCount++;
        }
    }

    /* Alarm manager tick: watch interlock + consecutive NG */
    AlarmManager_Tick();

    /* User auth session timeout tick */
    UserAuth_Tick();

    /* Auto-record cycle result when state transitions to CYCLE_END or CYCLE_NG */
    {
      static PressState_t s_prev_state = PRESS_STATE_IDLE;
      PressState_t s_now = PressStateMachine_GetState();
      if (s_prev_state != s_now)
      {
        /* ── Graph capture start/stop ─────────────────────────────────── */
        /* Start on any transition OUT of a "rest" state (IDLE or CYCLE_NG).
         * This handles the case where APPROACH→CONTACT happens in the same
         * PSM_Tick() call as CycleStart(), making APPROACH invisible here. */
        {
            uint8_t prev_rest = (s_prev_state == PRESS_STATE_IDLE) ||
                                (s_prev_state == PRESS_STATE_CYCLE_NG);
            uint8_t now_active = (s_now != PRESS_STATE_IDLE) &&
                                 (s_now != PRESS_STATE_CYCLE_NG) &&
                                 (s_now != PRESS_STATE_CYCLE_END);
            if (prev_rest && now_active) {
                {
                    char gdbg2[64];
                    snprintf(gdbg2, sizeof(gdbg2),
                             "[GRF] cap_start prev=%d now=%d old_cnt=%u pend=%u\r\n",
                             (int)s_prev_state, (int)s_now,
                             (unsigned)g_graphCount, (unsigned)g_graphSendPend);
                    USART3_SendText(gdbg2);
                }
                g_graphCount    = 0U;
                g_graphDivCnt   = 0U;
                g_graphActive   = 1U;
                g_graphSendPend = 0U;
                g_graphStartMs  = HAL_GetTick();
            }
        }
        if ((s_now == PRESS_STATE_CYCLE_END) ||
            (s_now == PRESS_STATE_CYCLE_NG)  ||
            (s_now == PRESS_STATE_ABORT)) {
            if (g_graphActive) {
                char gdbg3[48];
                snprintf(gdbg3, sizeof(gdbg3), "[GRF] cap_stop state=%d cnt=%u\r\n",
                         (int)s_now, (unsigned)g_graphCount);
                USART3_SendText(gdbg3);
            }
            g_graphActive = 0U;
        }

        /* Record when CYCLE_END exits: psm_tick_cycle_end() has already called
         * psm_record_result(), so g_last_result holds THIS cycle's values. */
        if (((s_prev_state == PRESS_STATE_CYCLE_END) &&
             ((s_now == PRESS_STATE_IDLE) || (s_now == PRESS_STATE_CYCLE_NG))) ||
            ((s_prev_state == PRESS_STATE_ABORT) && (s_now == PRESS_STATE_IDLE)))
        {
          const PressResult_t  *res = PressStateMachine_GetLastResult();
          const RecipeData_t   *rcp = RecipeManager_GetActive();
          CycleRecord_t rec;
          rec.boot_ms        = HAL_GetTick();
          rec.cycle_number   = res->cycle_number;
          rec.recipe_idx     = RecipeManager_GetActiveIdx();
          rec.recipe_id      = (uint8_t)rcp->recipe_id;
          rec.op_mode        = (uint8_t)PressStateMachine_GetMode();
          rec._pad           = 0U;
          rec.result         = res->result;
          rec.peak_force_pct = res->peak_force_pct;
          rec.end_position   = res->end_position;
          rec.cycle_time_ms  = res->cycle_time_ms;
          rec.alarm_id       = (uint16_t)AlarmManager_GetActive();
          CycleLogger_Record(&rec);
          MaintCounter_IncrementCycle();
          /* Send RST first so PC_GUI has _lastResult before GRFE arrives */
          Pc_CmdReply("RST,cycle=%lu,result=%d,force=%u,pos=%ld,ms=%lu",
                      (unsigned long)res->cycle_number,
                      (int)res->result,
                      (unsigned int)res->peak_force_pct,
                      (long)res->end_position,
                      (unsigned long)res->cycle_time_ms);
          /* Freeze sample count then start streaming AFTER RST */
          g_graphStreamCount = g_graphCount;
          g_graphSendPend    = 1U;
          g_graphSendIdx     = 0U;
          g_graphStreamDiv   = 0U;
          {
            char gdbg[48];
            snprintf(gdbg, sizeof(gdbg), "[GRF] stream n=%u\r\n", (unsigned)g_graphStreamCount);
            USART3_SendText(gdbg);
          }
        }
      }
      s_prev_state = s_now;
    }

    uint32_t poll_elapsed = *DWT_CYCCNT_PTR - poll_start;
    if (poll_elapsed > cyc_poll_max) cyc_poll_max = poll_elapsed;
    
    /* Monitor slave state changes */
    #ifdef SOEM_ENABLED
    static uint8_t last_slave_state[EC_MAXSLAVE] = {0};
    for (int i = 1; i <= soem_context.slavecount; i++)
    {
      uint8_t current_state = (uint8_t)soem_context.slavelist[i].state;
      if (current_state != last_slave_state[i])
      {
        last_slave_state[i] = current_state;
        char msg[128];
        snprintf(msg, sizeof(msg), "[SOEM] Slave %d: State=%d AL=0x%04X\r\n", 
                 i, current_state, soem_context.slavelist[i].ALstatuscode);
        USART3_SendText(msg);
        
        if (current_state == 8)  /* EC_STATE_OPERATIONAL */
        {
          USART3_SendText("[SOEM] Slave reached OPERATIONAL state\r\n");
          HAL_GPIO_WritePin(LD1_PORT, LD1_PIN, GPIO_PIN_SET);
        }
      }
    }
    
    g_slaveCount = (uint8_t)soem_context.slavecount;
    #endif
    
    /* ── Graph streaming (1 ASCII line per GRAPH_STREAM_DIV ms) ────────── */
    if (g_graphSendPend) {
        g_graphStreamDiv++;
        if (g_graphStreamDiv >= GRAPH_STREAM_DIV) {
            g_graphStreamDiv = 0U;
            char gline[48];
            if (g_graphSendIdx == 0U) {
                /* Header — use frozen count to avoid race with next cycle */
                snprintf(gline, sizeof(gline), "GRFS,n=%u\r\n", (unsigned)g_graphStreamCount);
                USART3_SendText(gline);
                g_graphSendIdx = 1U;
            } else if (g_graphSendIdx <= g_graphStreamCount) {
                /* Data line: ms,pos,torque,velocity,step */
                uint16_t i = g_graphSendIdx - 1U;
                snprintf(gline, sizeof(gline), "%u,%ld,%d,%d,%u\r\n",
                         (unsigned)g_graphBuf[i].ms,
                         (long)    g_graphBuf[i].pos,
                         (int)     g_graphBuf[i].torque,
                         (int)     g_graphBuf[i].velocity,
                         (unsigned)g_graphBuf[i].step);
                USART3_SendText(gline);
                g_graphSendIdx++;
            } else {
                /* Footer */
                USART3_SendText("GRFE\r\n");
                g_graphSendPend = 0U;
                g_graphSendIdx  = 0U;
            }
        }
    }

    /* Flush UART ring buffer (non-blocking via IT) */
    UART_LogFlush();

    /* Measure cycle time before sleep */
    uint32_t cyc_now = *DWT_CYCCNT_PTR;
    uint32_t cyc_delta = cyc_now - cyc_prev;
    cyc_prev = cyc_now;

    if (cyc_cnt > 0)  /* skip first cycle (invalid delta) */
    {
      if (cyc_delta < cyc_min) cyc_min = cyc_delta;
      if (cyc_delta > cyc_max) cyc_max = cyc_delta;
      cyc_sum += cyc_delta;
    }
    cyc_cnt++;

    /* Print timing stats every 5 seconds (5000 cycles @ 1ms) */
    if (cyc_cnt > 0 && (cyc_cnt % 5000U) == 0U)
    {
      uint32_t avg_us  = (uint32_t)(cyc_sum / (cyc_cnt - 1U) / (cpu_freq / 1000000U));
      uint32_t min_us  = cyc_min / (cpu_freq / 1000000U);
      uint32_t max_us  = cyc_max / (cpu_freq / 1000000U);
      uint32_t poll_us = cyc_poll_max / (cpu_freq / 1000000U);
      int32_t  jitter  = (int32_t)(max_us - min_us);
      char tbuf[180];
      snprintf(tbuf, sizeof(tbuf),
               "[TIMING] cycles=%lu  avg=%lu us  min=%lu us  max=%lu us  "
               "jitter=%ld us  poll_max=%lu us\r\n",
               (unsigned long)cyc_cnt,
               (unsigned long)avg_us, (unsigned long)min_us,
               (unsigned long)max_us, (long)jitter,
               (unsigned long)poll_us);
      UartProto_SendLog(tbuf);
      /* Reset stats for next window */
      cyc_min = 0xFFFFFFFFU;
      cyc_max = 0U;
      cyc_sum = 0U;
      cyc_cnt = 1U;
      cyc_poll_max = 0U;
    }

    HAL_GPIO_TogglePin(LD1_PORT, LD1_PIN);
    vTaskDelayUntil(&xLastWakeTime, xPeriod);  /* Precise 1ms period */
  }
#endif

  /* Note: The code below this point is unreachable due to infinite loops above */

  /* Initialize SOEM context to zero */
  memset(&ecatContext, 0, sizeof(ecatContext));

  g_soemErrorCode = 310;  /* Starting ecx_init (31%) */

  /* Initialize SOEM - interface name for STM32 */
  if (ecx_init(&ecatContext, "st_eth") <= 0)
  {
    g_soemInitStatus = 2;  /* Error - SOEM init failed */
    g_soemErrorCode = 320;  /* ecx_init failed (32%) */
    uint32_t cnt = 0;
    for(;;)
    {
      g_soemErrorCode = 321 + (cnt % 9);  /* 321-329 = ecx_init fail loop */
      cnt++;
      osDelay(1000);
    }
  }

  g_soemErrorCode = 350;  /* ecx_init OK, starting config_init (35%) */

  /* DEBUG: Check sockhandle right after ecx_init */
  g_soem_sockhandle_value = ecatContext.port.sockhandle;
  if (ecatContext.port.sockhandle <= 0)
  {
    g_soemErrorCode = 355;  /* sockhandle invalid after ecx_init! */
    for(;;)
    {
      g_soemErrorCode = 355;
      osDelay(1000);
    }
  }
  g_soemErrorCode = 356;  /* sockhandle OK */

  /* Check PHY link status before config_init */
  int32_t linkState = LAN8742_GetLinkState(&lan8742);
  g_soemErrorCode = 360;  /* PHY link check: 36% = link down */
  if (linkState > LAN8742_STATUS_LINK_DOWN)
  {
    g_soemErrorCode = 360 + linkState * 10;  /* 370=10M HD, 380=10M FD, 390=100M HD, 400=100M FD */
  }

  osDelay(100);  /* Small delay before config_init */

  /* Find and configure slaves */
  int slaveCount = ecx_config_init(&ecatContext);
  g_soemErrorCode = 450;  /* config_init returned (display 45%) */
  if (slaveCount > 0)
  {
    g_slaveCount = ecatContext.slavecount;
    g_soemInitStatus = 1;  /* OK */
    g_soemErrorCode = 470 + g_slaveCount;  /* 471=1 slave, 472=2 slaves, etc (47%) */

    /* === Step 1: Transition to PRE_OP (like Linux code) === */
    g_soemErrorCode = 480;  /* Requesting PRE_OP (48%) */
    ecatContext.slavelist[0].state = EC_STATE_PRE_OP;
    ecx_writestate(&ecatContext, 0);
    
    int preop_timeout = 2000;
    while (ecx_statecheck(&ecatContext, 0, EC_STATE_PRE_OP, 50000) != EC_STATE_PRE_OP && preop_timeout > 0)
    {
      preop_timeout--;
    }
    
    if (preop_timeout <= 0)
    {
      g_soemInitStatus = 2;
      g_soemErrorCode = 481;  /* PRE_OP timeout */
      goto error_loop;
    }
    
    g_soemErrorCode = 490;  /* PRE_OP reached, mapping PDO (49%) */
    
    /* === Step 2: Map process data (in PRE_OP state) === */
    ecx_config_map_group(&ecatContext, IOmap, 0);

    /* Configure DC if needed */
    ecx_configdc(&ecatContext);

    /* === Step 3: Transition to SAFE_OP === */
    g_soemErrorCode = 500;  /* Requesting SAFE_OP (50%) */

    /* Request all slaves to SAFE_OP state */
    ecatContext.slavelist[0].state = EC_STATE_SAFE_OP;
    ecx_writestate(&ecatContext, 0);

    /* Wait for SAFE_OP state */
    int timeout = 4000;
    while (ecx_statecheck(&ecatContext, 0, EC_STATE_SAFE_OP, 50000) != EC_STATE_SAFE_OP && timeout > 0)
    {
      timeout--;
    }

    if (timeout > 0)
    {
      g_soemErrorCode = 600;  /* SAFE_OP reached, requesting OP (60%) */

      /* Request OPERATIONAL state */
      ecatContext.slavelist[0].state = EC_STATE_OPERATIONAL;
      ecx_writestate(&ecatContext, 0);

      g_soemErrorCode = 700;  /* OP requested, entering loop (70%) */

      /* Cyclic processing loop */
      for(;;)
      {
        ecx_send_processdata(&ecatContext);
        ecx_receive_processdata(&ecatContext, EC_TIMEOUTRET);
        g_soemErrorCode = 800;  /* Running OK (80%) */
        osDelay(1);  /* ~1ms cycle time */
      }
    }
    else
    {
      g_soemInitStatus = 2;  /* Error - state transition failed */
      g_soemErrorCode = 550;  /* SAFE_OP timeout (55%) */
    }
  }
  else
  {
    g_slaveCount = 0;
    g_soemInitStatus = 2;  /* Error - no slaves found */
    g_soemErrorCode = 460;  /* config_init returned 0 - no slaves (46%) */
  }

error_loop:
  /* Stay in error loop - cycle through debug info */
  /* Phase 0: last error code (original)
   * Phase 1: detect_step (add 1300) - shows step within detect_slaves
   * Phase 2: sockhandle value (add 1400) - actual sockhandle value
   * Phase 3: BWR count (add 600)
   */
  {
    uint32_t last_err = g_soemErrorCode;
    uint32_t phase = 0;
    for(;;)
    {
      switch(phase)
      {
        case 0:
          g_soemErrorCode = last_err;  /* Original error */
          break;
        case 1:
          g_soemErrorCode = 1300 + (g_soem_detect_step % 100);  /* 1301-1399 = detect step */
          break;
        case 2:
          /* Show sockhandle: 1400 + value. If negative, show 1400 - abs(value) */
          if (g_soem_sockhandle_value >= 0)
            g_soemErrorCode = 1400 + (g_soem_sockhandle_value % 100);
          else
            g_soemErrorCode = 1400;  /* 1400 = still default -999 or 0 */
          break;
        case 3:
          g_soemErrorCode = 600 + (g_soem_bwr_count % 100);  /* 600-699 = BWR count */
          break;
      }
      phase = (phase + 1) % 4;
      osDelay(2000);  /* 2 seconds per phase */
    }
  }
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  uint32_t lastWebTelMs  = 0U;
  uint32_t lastWebPstMs  = 0U;
  uint32_t lastSentCycle = 0U;
  char webTel[128];
  char webPst[192];
  char webRst[128];

  /* Wi-Fi runtime is intentionally disabled.
   * Keep this task alive without touching EMW3080/UART7 paths. */
  // char wifiMsg[240];
  // const char* wifiDiag;
  // int32_t wifiDiagCode;
  //
  // #if WIFI_SPI4_LOOPBACK_TEST
  // USART3_SendText("[WIFI] SPI4 loopback mode start (set SB2/SB6/SB8 ON, short CN3 pin2(PE14 MOSI)<->pin3(PE13 MISO))\r\n");
  // WIFI_SPI4_LoopbackSelfTest();
  // USART3_SendText("[WIFI] SPI4 loopback mode done, Wi-Fi init skipped\r\n");
  // for(;;)
  // {
  //   osDelay(1000);
  // }
  // #endif
  //
  // /* MB1400 UART mode uses UART7 on STM32H7S78-DK STMod+ routing. */
  // USART3_SendText("[WIFI] init starting (UART7 AT sync ~14s timeout)...\r\n");
  // WIFI_UART7_LoopbackSelfTest();
  // WIFI_UART7_QuickAtProbe();
  // Emw3080WebServer_Init();
  //
  // wifiDiag = Emw3080WebServer_GetDiagMessage();
  // if (wifiDiag == NULL)
  // {
  //   wifiDiag = "n/a";
  // }
  //
  // wifiDiagCode = Emw3080WebServer_GetDiagCode();
  // if (Emw3080WebServer_IsRunning() != 0U)
  // {
  //   (void)snprintf(wifiMsg,
  //                  sizeof(wifiMsg),
  //                  "[WIFI] AP up code=%ld uart=%lu baud=%lu mux=%u msg=%s\r\n",
  //                  (long)wifiDiagCode,
  //                  (unsigned long)Emw3080WebServer_GetDiagActiveUart(),
  //                  (unsigned long)Emw3080WebServer_GetDiagBaud(),
  //                  (unsigned int)Emw3080WebServer_GetDiagMultiConnMode(),
  //                  wifiDiag);
  // }
  // else
  // {
  //   (void)snprintf(wifiMsg,
  //                  sizeof(wifiMsg),
  //                  "[WIFI] AP failed code=%ld uart=%lu msg=%s (check SB1/SB5/SB7 and UART7 PE7/PE8 route)\r\n",
  //                  (long)wifiDiagCode,
  //                  (unsigned long)Emw3080WebServer_GetDiagActiveUart(),
  //                  wifiDiag);
  // }
  // USART3_SendText(wifiMsg);

  USART3_SendText("[WEBUI] ST-LINK UART4 bridge active\r\n");

  /* Infinite loop */
  for(;;)
  {
    /* Drain binary SLIP RX ring and dispatch any complete packets (10ms budget) */
    UartProto_PollRx();

    /* Send 3-axis STATUS frame every 10ms (matches osDelay period below) */
    UartProto_SendStatus();

    /* ASCII telemetry disabled — binary SLIP STATUS replaces it */
    (void)lastWebTelMs; (void)lastWebPstMs; (void)lastSentCycle;
    (void)webTel; (void)webPst; (void)webRst;

    /* Async flash save — runs here in DefaultTask, NOT in EtherCAT_Task.
     * Flash sector erase can take >1s and would block the 1ms PDO loop.
     * All save types collapse into one RobotFlash_Save() call that atomically
     * writes the full 3-axis AxisParam_t[] image (home offsets included).   */
    if (g_flashSavePending != 0U)
    {
      g_flashSavePending = 0U;
      (void)RobotFlash_Save();
    }

    osDelay(10);
  }
  /* USER CODE END 5 */
}

/* MPU Configuration for NUCLEO-H753ZI
 * Region 0: ETH DMA descriptors at 0x24070000 (AXI SRAM, non-cacheable)
 *           ETH DMA uses AXI bus → can access AXI SRAM (0x24000000)
 *           Non-cacheable required for DMA coherency without cache flush.
 */
static void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_Init = {0};

  HAL_MPU_Disable();

  MPU_Init.Enable           = MPU_REGION_ENABLE;
  MPU_Init.Number           = MPU_REGION_NUMBER0;
  MPU_Init.BaseAddress      = 0x24070000U; /* .eth_dma section in linker script */
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

/* SystemClock_Config: HSE 8 MHz → PLL1 → 480 MHz SYSCLK (NUCLEO-H753ZI) */
static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /* HSE + PLL1: 8 / 4 * 480 / 2 = 480 MHz */
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
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) { Error_Handler(); }

  clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                       RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
  clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  clk.SYSCLKDivider  = RCC_SYSCLK_DIV1;   /* D1 core = 480 MHz */
  clk.AHBCLKDivider  = RCC_HCLK_DIV2;     /* AHB = 240 MHz */
  clk.APB3CLKDivider = RCC_APB3_DIV2;     /* 120 MHz */
  clk.APB1CLKDivider = RCC_APB1_DIV2;     /* 120 MHz */
  clk.APB2CLKDivider = RCC_APB2_DIV2;     /* 120 MHz */
  clk.APB4CLKDivider = RCC_APB4_DIV2;     /* 120 MHz */
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) { Error_Handler(); }
}

/* HAL_TIM_PeriodElapsedCallback defined in stm32h7xx_hal_timebase_tim.c */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
