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
#include "gfxmmu_lut.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "app_touchgfx.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "settings_persistence.h"
#include "ui_flash_storage.h"
#include "stm32h7rsxx_hal_eth.h"
#include "soem/soem.h"
#include "osal.h"
#include "lan8742.h"
#include "soem_port.h"
// #include "emw3080_web_server.h"
#include "task.h"  /* vTaskDelayUntil */
#include "interlock_manager.h"
#include "press_state_machine.h"
#include "recipe_manager.h"
#include "cycle_logger.h"
#include "alarm_manager.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Non-blocking UART ring buffer for EtherCAT logging */
#define UART_LOG_BUF_SIZE 2048U
static volatile char uart_log_buf[UART_LOG_BUF_SIZE];
static volatile uint16_t uart_log_head = 0;  /* written by producer (EtherCAT task) */
static volatile uint16_t uart_log_tail = 0;  /* consumed by DMA/flush task */
static volatile uint8_t  uart_dma_busy = 0;  /* 1 = DMA transfer in flight */
static char uart_dma_chunk[256];  /* DMA source buffer in normal RAM */

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

CRC_HandleTypeDef hcrc;

DMA2D_HandleTypeDef hdma2d;

GFXMMU_HandleTypeDef hgfxmmu;

GPU2D_HandleTypeDef hgpu2d;

I2C_HandleTypeDef hi2c1;

JPEG_HandleTypeDef hjpeg;
DMA_HandleTypeDef handle_HPDMA1_Channel1;
DMA_HandleTypeDef handle_HPDMA1_Channel0;

LTDC_HandleTypeDef hltdc;

UART_HandleTypeDef huart4;
UART_HandleTypeDef huart7;

ETH_HandleTypeDef heth;
/* DMA descriptors in RAM_CMD (0x24068000), MPU non-cacheable region.
 * ETH DMA uses AXI bus — cannot access SRAMAHB (AHB-only). Must stay in AXI SRAM. */
__ALIGN_BEGIN ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT] __ALIGN_END __attribute__((section(".eth_dma"), aligned(32)));
__ALIGN_BEGIN ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT] __ALIGN_END __attribute__((section(".eth_dma"), aligned(32)));

/* Debug status variables (shared with TouchGFX) */
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
/* Definitions for TouchGFXTask */
osThreadId_t TouchGFXTaskHandle;
const osThreadAttr_t TouchGFXTask_attributes = {
  .name = "TouchGFXTask",
  .stack_size = 4096 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_HPDMA1_Init(void);
static void MX_LTDC_Init(void);
static void MX_CRC_Init(void);
static void MX_DMA2D_Init(void);
static void MX_JPEG_Init(void);
static void MX_FLASH_Init(void);
static void MX_I2C1_Init(void);
static void MX_UART4_Init(void);
static void MX_UART7_Init(void);
static void MX_GPU2D_Init(void);
static void MX_ICACHE_GPU2D_Init(void);
static void MX_GFXMMU_Init(void);
void StartDefaultTask(void *argument);
extern void TouchGFX_Task(void *argument);
static void MX_ETH_Init(void);

/* USER CODE BEGIN PFP */
void EtherCAT_Task(void *argument);
extern uint8_t TouchGFX_ModelReloadAllFromUiFlash(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Non-blocking UART: push text into ring buffer, flushed by UART_LogFlush() */
static void UART4_SendText(const char *text)
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
  if (HAL_UART_Transmit_IT(&huart4, (uint8_t *)uart_dma_chunk, n) == HAL_OK)
  {
    uart_log_tail = t;
    uart_dma_busy = 1;
  }
}

#define PC_CMD_LINE_MAX 192U
#define PC_PROGRAM_POSITION_TOLERANCE 100
#define PC_PROGRAM_RETURN_TOLERANCE_HW 5
#define PC_UART_RX_RING_SIZE 512U
#define PC_UART_RX_POLL_BUDGET 32U
#define PC_HOME_DIAG_LOG_DEFAULT 0U

enum
{
  PC_PARAM_JOG_SPEED = 0,
  PC_PARAM_ACC_TIME,
  PC_PARAM_DEC_TIME,
  PC_PARAM_LIMIT_PLUS,
  PC_PARAM_LIMIT_MINUS,
  PC_PARAM_UNIT_SCALE,
  PC_PARAM_HOME_OFFSET,
  PC_PARAM_POSITION_GAIN,
  PC_PARAM_COUNT
};

enum
{
  PC_PROG_POS1 = 0,
  PC_PROG_POS2,
  PC_PROG_POS3,
  PC_PROG_SPEED1,
  PC_PROG_SPEED2,
  PC_PROG_SPEED3,
  PC_PROG_TORQUE1,
  PC_PROG_TORQUE2,
  PC_PROG_TORQUE3,
  PC_PROG_RETURN_SPEED,
  PC_PROG_DELAY_MS,
  PC_PROG_COUNT
};

typedef enum
{
  PC_SEQ_IDLE = 0,
  PC_SEQ_MOVE_STEP1,
  PC_SEQ_MOVE_STEP2,
  PC_SEQ_MOVE_STEP3,
  PC_SEQ_DELAY_BEFORE_RETURN,
  PC_SEQ_RETURN_TO_ORIGIN
} PcProgramSequenceState;

typedef struct
{
  int32_t manualPosition;
  int32_t manualSpeed;
  int16_t manualTorque;
  uint8_t manualAbsMode;
  int32_t parameterValues[PC_PARAM_COUNT];
  int32_t programValues[PC_PROG_COUNT];

  uint8_t parameterReadPending;
  uint32_t parameterReadStartMs;

  PcProgramSequenceState seqState;
  int32_t seqStepPositions[3];
  int32_t seqStepSpeeds[3];
  uint16_t seqStepTorques[3];
  int32_t seqOriginPosition;
  int32_t seqOriginPositionHw;
  int32_t seqActiveTarget;
  uint32_t seqDelayMs;
  uint32_t seqDelayStartMs;
} PcCommandContext;

static PcCommandContext g_pcCmd;
static char g_pcCmdLine[PC_CMD_LINE_MAX];
static uint16_t g_pcCmdLen = 0U;
static volatile uint8_t g_pcUartRxRing[PC_UART_RX_RING_SIZE];
static volatile uint16_t g_pcUartRxHead = 0U;
static volatile uint16_t g_pcUartRxTail = 0U;
static uint8_t g_pcUartRxByte = 0U;
static uint8_t g_pcHomeDiagLog = PC_HOME_DIAG_LOG_DEFAULT;

static void Pc_ProcessCommandLine(char *line);

static uint8_t Pc_ToLowerAscii(uint8_t c)
{
  if (c >= (uint8_t)'A' && c <= (uint8_t)'Z')
  {
    return (uint8_t)(c + ((uint8_t)'a' - (uint8_t)'A'));
  }
  return c;
}

static uint8_t Pc_StrEqIgnoreCase(const char *a, const char *b)
{
  if (a == NULL || b == NULL)
  {
    return 0U;
  }

  while (*a != '\0' && *b != '\0')
  {
    if (Pc_ToLowerAscii((uint8_t)*a) != Pc_ToLowerAscii((uint8_t)*b))
    {
      return 0U;
    }
    a++;
    b++;
  }

  return ((*a == '\0') && (*b == '\0')) ? 1U : 0U;
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

static int32_t Pc_ParseInt32(const char *text, int32_t fallback)
{
  char *endPtr = NULL;
  long parsed;
  if (text == NULL)
  {
    return fallback;
  }

  parsed = strtol(text, &endPtr, 10);
  if (endPtr == text)
  {
    return fallback;
  }
  if (parsed > INT32_MAX)
  {
    parsed = INT32_MAX;
  }
  else if (parsed < INT32_MIN)
  {
    parsed = INT32_MIN;
  }
  return (int32_t)parsed;
}

static uint8_t Pc_ParseBool(const char *text, uint8_t fallback)
{
  if (text == NULL)
  {
    return fallback;
  }

  if (Pc_StrEqIgnoreCase(text, "1") || Pc_StrEqIgnoreCase(text, "true") || Pc_StrEqIgnoreCase(text, "on"))
  {
    return 1U;
  }
  if (Pc_StrEqIgnoreCase(text, "0") || Pc_StrEqIgnoreCase(text, "false") || Pc_StrEqIgnoreCase(text, "off"))
  {
    return 0U;
  }

  return fallback;
}

static int32_t Pc_AbsMin1(int32_t value)
{
  int32_t absValue = value;
  if (absValue < 0)
  {
    absValue = -absValue;
  }
  if (absValue <= 0)
  {
    absValue = 1;
  }
  return absValue;
}

static uint16_t Pc_ClampTorquePercent(int32_t value)
{
  int32_t absValue = Pc_AbsMin1(value);
  if (absValue > 100)
  {
    absValue = 100;
  }
  return (uint16_t)absValue;
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

  UART4_SendText(msg);
  UART4_SendText("\r\n");
}

static void Pc_HomeDiagLog(const char *tag)
{
  if (g_pcHomeDiagLog == 0U)
  {
    return;
  }

  Pc_CmdReply("[HOME_DIAG] tag=%s cur_u=%ld cur_hw=%ld home_hw=%ld home_user=%ld unit=%ld",
              (tag != NULL) ? tag : "?",
              (long)SOEM_GetPositionActual(),
              (long)SOEM_GetPositionActualHw(),
              (long)SOEM_GetHomeOffset(),
              (long)g_pcCmd.parameterValues[PC_PARAM_HOME_OFFSET],
              (long)g_pcCmd.parameterValues[PC_PARAM_UNIT_SCALE]);
}

static void Pc_ReplyConfigSnapshot(void)
{
  Pc_CmdReply("CFGM,pos=%ld,speed=%ld,torque=%d,abs=%u",
              (long)g_pcCmd.manualPosition,
              (long)g_pcCmd.manualSpeed,
              (int)g_pcCmd.manualTorque,
              (unsigned int)g_pcCmd.manualAbsMode);

  Pc_CmdReply("CFGP,jog=%ld,acc=%ld,dec=%ld,lplus=%ld,lminus=%ld,unit=%ld,home=%ld,gain=%ld",
              (long)g_pcCmd.parameterValues[PC_PARAM_JOG_SPEED],
              (long)g_pcCmd.parameterValues[PC_PARAM_ACC_TIME],
              (long)g_pcCmd.parameterValues[PC_PARAM_DEC_TIME],
              (long)g_pcCmd.parameterValues[PC_PARAM_LIMIT_PLUS],
              (long)g_pcCmd.parameterValues[PC_PARAM_LIMIT_MINUS],
              (long)g_pcCmd.parameterValues[PC_PARAM_UNIT_SCALE],
              (long)g_pcCmd.parameterValues[PC_PARAM_HOME_OFFSET],
              (long)g_pcCmd.parameterValues[PC_PARAM_POSITION_GAIN]);

  Pc_CmdReply("CFGR,p1=%ld,p2=%ld,p3=%ld,s1=%ld,s2=%ld,s3=%ld,t1=%ld,t2=%ld,t3=%ld,rs=%ld,delay=%ld",
              (long)g_pcCmd.programValues[PC_PROG_POS1],
              (long)g_pcCmd.programValues[PC_PROG_POS2],
              (long)g_pcCmd.programValues[PC_PROG_POS3],
              (long)g_pcCmd.programValues[PC_PROG_SPEED1],
              (long)g_pcCmd.programValues[PC_PROG_SPEED2],
              (long)g_pcCmd.programValues[PC_PROG_SPEED3],
              (long)g_pcCmd.programValues[PC_PROG_TORQUE1],
              (long)g_pcCmd.programValues[PC_PROG_TORQUE2],
              (long)g_pcCmd.programValues[PC_PROG_TORQUE3],
              (long)g_pcCmd.programValues[PC_PROG_RETURN_SPEED],
              (long)g_pcCmd.programValues[PC_PROG_DELAY_MS]);
}

static void Pc_SyncTouchGfxModel(void)
{
  (void)TouchGFX_ModelReloadAllFromUiFlash();
}

static void Pc_SaveManualToFlash(void)
{
  UiFlashManualData manualData;
  manualData.position = g_pcCmd.manualPosition;
  manualData.speed = g_pcCmd.manualSpeed;
  manualData.torque = g_pcCmd.manualTorque;
  manualData.absMode = g_pcCmd.manualAbsMode;
  memset(manualData.reserved, 0, sizeof(manualData.reserved));
  (void)UiFlashStorage_SaveManual(&manualData);
}

static void Pc_SaveParameterToFlash(void)
{
  UiFlashParameterData parameterData;
  uint8_t i;
  for (i = 0U; i < PC_PARAM_COUNT; i++)
  {
    parameterData.values[i] = g_pcCmd.parameterValues[i];
  }
  (void)UiFlashStorage_SaveParameter(&parameterData);
}

static void Pc_SaveProgramToFlash(void)
{
  UiFlashProgramData programData;
  uint8_t i;
  for (i = 0U; i < PC_PROG_COUNT; i++)
  {
    programData.values[i] = g_pcCmd.programValues[i];
  }
  (void)UiFlashStorage_SaveProgram(&programData);
}

static uint8_t Pc_IsMotionEnabled(void)
{
  const uint16_t statusword = SOEM_GetStatusword();
  if (SOEM_GetPdoReady() == 0U)
  {
    return 0U;
  }
  if (SOEM_GetRunEnable() == 0U)
  {
    return 0U;
  }
  return (((statusword & 0x006FU) == 0x0027U) ? 1U : 0U);
}

static uint8_t Pc_IsTargetReached(int32_t target)
{
  int64_t delta = (int64_t)SOEM_GetPositionActual() - (int64_t)target;
  if (delta < 0)
  {
    delta = -delta;
  }
  return (delta <= (int64_t)PC_PROGRAM_POSITION_TOLERANCE) ? 1U : 0U;
}

static uint8_t Pc_IsHardwareTargetReached(int32_t targetHw, int32_t toleranceHw)
{
  int32_t tolerance = toleranceHw;
  int64_t delta;
  if (tolerance < 0)
  {
    tolerance = -tolerance;
  }

  delta = (int64_t)SOEM_GetPositionActualHw() - (int64_t)targetHw;
  if (delta < 0)
  {
    delta = -delta;
  }
  return (delta <= (int64_t)tolerance) ? 1U : 0U;
}

static int32_t Pc_ClampInt64ToInt32(int64_t value)
{
  if (value > (int64_t)INT32_MAX)
  {
    return INT32_MAX;
  }
  if (value < (int64_t)INT32_MIN)
  {
    return INT32_MIN;
  }
  return (int32_t)value;
}

static void Pc_ProgramStop(void)
{
  if (g_pcCmd.seqState == PC_SEQ_IDLE)
  {
    return;
  }

  SOEM_SetTargetPositionAbs(SOEM_GetPositionActual());
  g_pcCmd.seqState = PC_SEQ_IDLE;
  g_pcCmd.seqDelayStartMs = 0U;
}

static void Pc_ProgramBeginStep(uint8_t stepIndex)
{
  if (stepIndex >= 3U)
  {
    return;
  }

  SOEM_SetProfileVelocity(g_pcCmd.seqStepSpeeds[stepIndex]);
  SOEM_SetTorqueLimitPercent(g_pcCmd.seqStepTorques[stepIndex]);
  g_pcCmd.seqActiveTarget = g_pcCmd.seqStepPositions[stepIndex];
  SOEM_SetTargetPositionAbs(g_pcCmd.seqActiveTarget);

  if (stepIndex == 0U)
  {
    g_pcCmd.seqState = PC_SEQ_MOVE_STEP1;
  }
  else if (stepIndex == 1U)
  {
    g_pcCmd.seqState = PC_SEQ_MOVE_STEP2;
  }
  else
  {
    g_pcCmd.seqState = PC_SEQ_MOVE_STEP3;
  }
}

static uint8_t Pc_ProgramStart(void)
{
  uint8_t i;
  int32_t delayValue;

  if (g_pcCmd.seqState != PC_SEQ_IDLE)
  {
    return 0U;
  }
  if (Pc_IsMotionEnabled() == 0U)
  {
    return 0U;
  }

  for (i = 0U; i < 3U; i++)
  {
    g_pcCmd.seqStepPositions[i] = g_pcCmd.programValues[PC_PROG_POS1 + i];
    g_pcCmd.seqStepSpeeds[i] = Pc_AbsMin1(g_pcCmd.programValues[PC_PROG_SPEED1 + i]);
    g_pcCmd.seqStepTorques[i] = Pc_ClampTorquePercent(g_pcCmd.programValues[PC_PROG_TORQUE1 + i]);
  }

  delayValue = g_pcCmd.programValues[PC_PROG_DELAY_MS];
  g_pcCmd.seqDelayMs = (delayValue > 0) ? (uint32_t)delayValue : 0U;

  g_pcCmd.seqOriginPosition = SOEM_GetPositionActual();
  g_pcCmd.seqOriginPositionHw = SOEM_GetPositionActualHw();
  g_pcCmd.seqDelayStartMs = 0U;

  Pc_ProgramBeginStep(0U);
  return 1U;
}

static void Pc_ProgramTick(void)
{
  if (g_pcCmd.seqState == PC_SEQ_IDLE)
  {
    return;
  }

  if (SOEM_GetRunEnable() == 0U)
  {
    Pc_ProgramStop();
    return;
  }

  if (g_pcCmd.seqState == PC_SEQ_MOVE_STEP1)
  {
    if (Pc_IsTargetReached(g_pcCmd.seqActiveTarget) != 0U)
    {
      Pc_ProgramBeginStep(1U);
    }
    return;
  }

  if (g_pcCmd.seqState == PC_SEQ_MOVE_STEP2)
  {
    if (Pc_IsTargetReached(g_pcCmd.seqActiveTarget) != 0U)
    {
      Pc_ProgramBeginStep(2U);
    }
    return;
  }

  if (g_pcCmd.seqState == PC_SEQ_MOVE_STEP3)
  {
    if (Pc_IsTargetReached(g_pcCmd.seqActiveTarget) != 0U)
    {
      g_pcCmd.seqDelayStartMs = HAL_GetTick();
      g_pcCmd.seqState = PC_SEQ_DELAY_BEFORE_RETURN;
    }
    return;
  }

  if (g_pcCmd.seqState == PC_SEQ_DELAY_BEFORE_RETURN)
  {
    uint32_t elapsed = HAL_GetTick() - g_pcCmd.seqDelayStartMs;
    if (elapsed >= g_pcCmd.seqDelayMs)
    {
      const int32_t returnSpeed = Pc_AbsMin1(g_pcCmd.programValues[PC_PROG_RETURN_SPEED]);
      SOEM_SetProfileVelocity(returnSpeed);
      SOEM_SetTorqueLimitPercent(g_pcCmd.seqStepTorques[2]);
      g_pcCmd.seqActiveTarget = g_pcCmd.seqOriginPosition;
      SOEM_SetTargetPositionAbsHw(g_pcCmd.seqOriginPositionHw);
      g_pcCmd.seqState = PC_SEQ_RETURN_TO_ORIGIN;
    }
    return;
  }

  if (g_pcCmd.seqState == PC_SEQ_RETURN_TO_ORIGIN)
  {
    if (Pc_IsHardwareTargetReached(g_pcCmd.seqOriginPositionHw, PC_PROGRAM_RETURN_TOLERANCE_HW) != 0U)
    {
      g_pcCmd.seqState = PC_SEQ_IDLE;
      g_pcCmd.seqDelayStartMs = 0U;
    }
  }
}

static void Pc_ApplyParametersToDrive(void)
{
  int32_t jogSpeed = g_pcCmd.parameterValues[PC_PARAM_JOG_SPEED];
  int32_t acc = g_pcCmd.parameterValues[PC_PARAM_ACC_TIME];
  int32_t dec = g_pcCmd.parameterValues[PC_PARAM_DEC_TIME];
  int32_t unitScale = g_pcCmd.parameterValues[PC_PARAM_UNIT_SCALE];
  int32_t posGain = g_pcCmd.parameterValues[PC_PARAM_POSITION_GAIN];

  if (jogSpeed < 0)
  {
    jogSpeed = -jogSpeed;
  }
  if (jogSpeed <= 0)
  {
    jogSpeed = 1;
  }
  if (acc < 0)
  {
    acc = -acc;
  }
  if (dec < 0)
  {
    dec = -dec;
  }
  if (unitScale <= 0)
  {
    unitScale = 1;
  }
  if (posGain < 0)
  {
    posGain = 0;
  }

  g_pcCmd.parameterValues[PC_PARAM_JOG_SPEED] = jogSpeed;
  g_pcCmd.parameterValues[PC_PARAM_ACC_TIME] = acc;
  g_pcCmd.parameterValues[PC_PARAM_DEC_TIME] = dec;
  g_pcCmd.parameterValues[PC_PARAM_UNIT_SCALE] = unitScale;
  g_pcCmd.parameterValues[PC_PARAM_POSITION_GAIN] = posGain;

  SOEM_SetProfileVelocity(jogSpeed);
  SOEM_SetProfileAcceleration(acc);
  SOEM_SetProfileDeceleration(dec);
  SOEM_SetUnitScale(unitScale);
  SOEM_SetSoftwareLimitPlus(g_pcCmd.parameterValues[PC_PARAM_LIMIT_PLUS]);
  SOEM_SetSoftwareLimitMinus(g_pcCmd.parameterValues[PC_PARAM_LIMIT_MINUS]);
  SOEM_SetPositionGain(posGain);
  Pc_HomeDiagLog("apply_params");
}

static void Pc_CommandInit(void)
{
  UiFlashManualData manualData;
  UiFlashParameterData parameterData;
  UiFlashProgramData programData;
  int32_t savedHomeHwOffset = 0;
  uint8_t i;

  memset(&g_pcCmd, 0, sizeof(g_pcCmd));
  g_pcCmd.manualAbsMode = 1U;
  g_pcCmd.parameterValues[PC_PARAM_UNIT_SCALE] = 1;
  g_pcCmd.programValues[PC_PROG_RETURN_SPEED] = 100;
  g_pcCmd.seqState = PC_SEQ_IDLE;

  if (UiFlashStorage_LoadManual(&manualData) != 0U)
  {
    g_pcCmd.manualPosition = manualData.position;
    g_pcCmd.manualSpeed = manualData.speed;
    g_pcCmd.manualTorque = manualData.torque;
    g_pcCmd.manualAbsMode = (manualData.absMode != 0U) ? 1U : 0U;
  }

  if (UiFlashStorage_LoadParameter(&parameterData) != 0U)
  {
    for (i = 0U; i < PC_PARAM_COUNT; i++)
    {
      g_pcCmd.parameterValues[i] = parameterData.values[i];
    }
  }

  if (UiFlashStorage_LoadProgram(&programData) != 0U)
  {
    for (i = 0U; i < PC_PROG_COUNT; i++)
    {
      g_pcCmd.programValues[i] = programData.values[i];
    }
  }

  /* Restore saved home origin at boot so absolute/user position remains calibrated
   * after power cycle, independent of TouchGFX model startup timing. */
  if (UiFlashStorage_LoadHome(&savedHomeHwOffset) != 0U)
  {
    SOEM_LoadHomeHwOffset(savedHomeHwOffset);
  }
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
  if (huart4.RxState == HAL_UART_STATE_READY)
  {
    (void)HAL_UART_Receive_IT(&huart4, &g_pcUartRxByte, 1U);
  }
}

static void Pc_ProcessCommandLine(char *line)
{
  char *payload = Pc_Trim(line);
  char *eq;
  char *key;
  char *value;
  int32_t parsed;

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
  value = Pc_Trim(eq + 1);

  if (Pc_StrEqIgnoreCase(key, "run"))
  {
    uint8_t runEnable = Pc_ParseBool(value, 0U);
    char msg[64];
    SOEM_SetRunEnable(runEnable);
    (void)snprintf(msg, sizeof(msg), "[CMD] run request=%u applied=%u", (unsigned int)runEnable, (unsigned int)SOEM_GetRunEnable());
    Pc_CmdReply(msg);
    if (runEnable == 0U)
    {
      Pc_ProgramStop();
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "diag_home_log"))
  {
    g_pcHomeDiagLog = Pc_ParseBool(value, g_pcHomeDiagLog);
    Pc_CmdReply("[CMD] diag_home_log=%u", (unsigned int)g_pcHomeDiagLog);
    Pc_HomeDiagLog("diag_toggle");
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "jog_delta"))
  {
    parsed = Pc_ParseInt32(value, 0);
    if (Pc_IsMotionEnabled() != 0U)
    {
      SOEM_SetTargetPositionDelta(parsed);
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "target_delta"))
  {
    parsed = Pc_ParseInt32(value, 0);
    g_pcCmd.manualPosition = parsed;
    if (Pc_IsMotionEnabled() != 0U)
    {
      SOEM_SetTargetPositionDelta(parsed);
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "target_abs"))
  {
    parsed = Pc_ParseInt32(value, SOEM_GetPositionActual());
    g_pcCmd.manualPosition = parsed;
    SOEM_SetTargetPositionAbs(parsed);
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "set_home"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      int32_t unitScale = g_pcCmd.parameterValues[PC_PARAM_UNIT_SCALE];
      int32_t homeOffsetUser = g_pcCmd.parameterValues[PC_PARAM_HOME_OFFSET];
      int64_t desiredHomeHw;
      int32_t homeOffsetHw;

      if (unitScale <= 0)
      {
        unitScale = 1;
        g_pcCmd.parameterValues[PC_PARAM_UNIT_SCALE] = unitScale;
      }

      /* User intent: after Set Home,
       * CurrentPosition(user) = 0 + HomeOffset(user).
       * With user = (actualHw - homeHw) / scale,
       * choose homeHw = actualHw - HomeOffset(user) * scale. */
      desiredHomeHw = (int64_t)SOEM_GetPositionActualHw() -
                      ((int64_t)homeOffsetUser * (int64_t)unitScale);
      homeOffsetHw = Pc_ClampInt64ToInt32(desiredHomeHw);

      SOEM_LoadHomeHwOffset(homeOffsetHw);
      (void)UiFlashStorage_SaveHome(homeOffsetHw);
      Pc_SaveParameterToFlash();
      Pc_HomeDiagLog("set_home");
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "manual_speed"))
  {
    parsed = Pc_AbsMin1(Pc_ParseInt32(value, g_pcCmd.manualSpeed));
    g_pcCmd.manualSpeed = parsed;
    SOEM_SetProfileVelocity(parsed);
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "manual_torque"))
  {
    uint16_t tq = Pc_ClampTorquePercent(Pc_ParseInt32(value, (int32_t)g_pcCmd.manualTorque));
    g_pcCmd.manualTorque = (int16_t)tq;
    SOEM_SetTorqueLimitPercent(tq);
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "manual_abs"))
  {
    g_pcCmd.manualAbsMode = Pc_ParseBool(value, g_pcCmd.manualAbsMode);
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "manual_pos"))
  {
    g_pcCmd.manualPosition = Pc_ParseInt32(value, g_pcCmd.manualPosition);
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "manual_apply"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      Pc_SaveManualToFlash();
      Pc_SyncTouchGfxModel();
      Pc_ReplyConfigSnapshot();
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "manual_start"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      int32_t targetUser = g_pcCmd.manualPosition;

      /* Match LCD Manual Start semantics:
       * - ABS: move to manualPosition
       * - INC: move to (actual + manualPosition) as absolute target */
      if (SOEM_GetRunEnable() == 0U)
      {
        SOEM_SetRunEnable(1U);
      }

      if (g_pcCmd.manualAbsMode != 0U)
      {
        SOEM_SetTargetPositionAbs(targetUser);
      }
      else
      {
        int64_t targetInc = (int64_t)SOEM_GetPositionActual() + (int64_t)g_pcCmd.manualPosition;
        targetUser = Pc_ClampInt64ToInt32(targetInc);
        SOEM_SetTargetPositionAbs(targetUser);
      }

      Pc_CmdReply("[CMD] manual_start abs=%u target=%ld run=%u",
                  (unsigned int)g_pcCmd.manualAbsMode,
                  (long)targetUser,
                  (unsigned int)SOEM_GetRunEnable());
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "manual_stop"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      SOEM_SetTargetPositionAbs(SOEM_GetPositionActual());
      Pc_ProgramStop();
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "manual_save"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      Pc_SaveManualToFlash();
      Pc_SyncTouchGfxModel();
      Pc_ReplyConfigSnapshot();
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "manual_load"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      UiFlashManualData manualData;
      if (UiFlashStorage_LoadManual(&manualData) != 0U)
      {
        g_pcCmd.manualPosition = manualData.position;
        g_pcCmd.manualSpeed = Pc_AbsMin1(manualData.speed);
        g_pcCmd.manualTorque = (int16_t)Pc_ClampTorquePercent((int32_t)manualData.torque);
        g_pcCmd.manualAbsMode = (manualData.absMode != 0U) ? 1U : 0U;
        SOEM_SetProfileVelocity(g_pcCmd.manualSpeed);
        SOEM_SetTorqueLimitPercent((uint16_t)g_pcCmd.manualTorque);
        Pc_SyncTouchGfxModel();
        Pc_ReplyConfigSnapshot();
      }
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "param_jog_speed"))         { g_pcCmd.parameterValues[PC_PARAM_JOG_SPEED] = Pc_ParseInt32(value, g_pcCmd.parameterValues[PC_PARAM_JOG_SPEED]); return; }
  if (Pc_StrEqIgnoreCase(key, "param_acc"))               { g_pcCmd.parameterValues[PC_PARAM_ACC_TIME] = Pc_ParseInt32(value, g_pcCmd.parameterValues[PC_PARAM_ACC_TIME]); return; }
  if (Pc_StrEqIgnoreCase(key, "param_dec"))               { g_pcCmd.parameterValues[PC_PARAM_DEC_TIME] = Pc_ParseInt32(value, g_pcCmd.parameterValues[PC_PARAM_DEC_TIME]); return; }
  if (Pc_StrEqIgnoreCase(key, "param_limit_plus"))        { g_pcCmd.parameterValues[PC_PARAM_LIMIT_PLUS] = Pc_ParseInt32(value, g_pcCmd.parameterValues[PC_PARAM_LIMIT_PLUS]); return; }
  if (Pc_StrEqIgnoreCase(key, "param_limit_minus"))       { g_pcCmd.parameterValues[PC_PARAM_LIMIT_MINUS] = Pc_ParseInt32(value, g_pcCmd.parameterValues[PC_PARAM_LIMIT_MINUS]); return; }
  if (Pc_StrEqIgnoreCase(key, "param_unit_scale"))        { g_pcCmd.parameterValues[PC_PARAM_UNIT_SCALE] = Pc_ParseInt32(value, g_pcCmd.parameterValues[PC_PARAM_UNIT_SCALE]); return; }
    if (Pc_StrEqIgnoreCase(key, "param_home_offset"))       { g_pcCmd.parameterValues[PC_PARAM_HOME_OFFSET] = Pc_ParseInt32(value, g_pcCmd.parameterValues[PC_PARAM_HOME_OFFSET]); return; }
  if (Pc_StrEqIgnoreCase(key, "param_position_gain"))     { g_pcCmd.parameterValues[PC_PARAM_POSITION_GAIN] = Pc_ParseInt32(value, g_pcCmd.parameterValues[PC_PARAM_POSITION_GAIN]); return; }

  if (Pc_StrEqIgnoreCase(key, "param_write_all"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      Pc_ApplyParametersToDrive();
      Pc_HomeDiagLog("param_write_all");
      Pc_ReplyConfigSnapshot();
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "param_apply"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      Pc_ApplyParametersToDrive();
      Pc_HomeDiagLog("param_apply");
      Pc_SaveParameterToFlash();
      Pc_SyncTouchGfxModel();
      Pc_ReplyConfigSnapshot();
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "cfg_read"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      SOEM_RequestParameterReadAll();
      g_pcCmd.parameterReadPending = 1U;
      g_pcCmd.parameterReadStartMs = HAL_GetTick();
      Pc_ReplyConfigSnapshot();
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "param_read_all"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      SOEM_RequestParameterReadAll();
      g_pcCmd.parameterReadPending = 1U;
      g_pcCmd.parameterReadStartMs = HAL_GetTick();
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "param_save"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      Pc_SaveParameterToFlash();
      Pc_SyncTouchGfxModel();
      Pc_ReplyConfigSnapshot();
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "param_load"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      UiFlashParameterData parameterData;
      uint8_t i;
      if (UiFlashStorage_LoadParameter(&parameterData) != 0U)
      {
        for (i = 0U; i < PC_PARAM_COUNT; i++)
        {
          g_pcCmd.parameterValues[i] = parameterData.values[i];
        }
        Pc_SyncTouchGfxModel();
        Pc_ReplyConfigSnapshot();
      }
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "prog_pos1"))          { g_pcCmd.programValues[PC_PROG_POS1] = Pc_ParseInt32(value, g_pcCmd.programValues[PC_PROG_POS1]); return; }
  if (Pc_StrEqIgnoreCase(key, "prog_pos2"))          { g_pcCmd.programValues[PC_PROG_POS2] = Pc_ParseInt32(value, g_pcCmd.programValues[PC_PROG_POS2]); return; }
  if (Pc_StrEqIgnoreCase(key, "prog_pos3"))          { g_pcCmd.programValues[PC_PROG_POS3] = Pc_ParseInt32(value, g_pcCmd.programValues[PC_PROG_POS3]); return; }
  if (Pc_StrEqIgnoreCase(key, "prog_speed1"))        { g_pcCmd.programValues[PC_PROG_SPEED1] = Pc_ParseInt32(value, g_pcCmd.programValues[PC_PROG_SPEED1]); return; }
  if (Pc_StrEqIgnoreCase(key, "prog_speed2"))        { g_pcCmd.programValues[PC_PROG_SPEED2] = Pc_ParseInt32(value, g_pcCmd.programValues[PC_PROG_SPEED2]); return; }
  if (Pc_StrEqIgnoreCase(key, "prog_speed3"))        { g_pcCmd.programValues[PC_PROG_SPEED3] = Pc_ParseInt32(value, g_pcCmd.programValues[PC_PROG_SPEED3]); return; }
  if (Pc_StrEqIgnoreCase(key, "prog_torque1"))       { g_pcCmd.programValues[PC_PROG_TORQUE1] = Pc_ParseInt32(value, g_pcCmd.programValues[PC_PROG_TORQUE1]); return; }
  if (Pc_StrEqIgnoreCase(key, "prog_torque2"))       { g_pcCmd.programValues[PC_PROG_TORQUE2] = Pc_ParseInt32(value, g_pcCmd.programValues[PC_PROG_TORQUE2]); return; }
  if (Pc_StrEqIgnoreCase(key, "prog_torque3"))       { g_pcCmd.programValues[PC_PROG_TORQUE3] = Pc_ParseInt32(value, g_pcCmd.programValues[PC_PROG_TORQUE3]); return; }
  if (Pc_StrEqIgnoreCase(key, "prog_return_speed"))  { g_pcCmd.programValues[PC_PROG_RETURN_SPEED] = Pc_ParseInt32(value, g_pcCmd.programValues[PC_PROG_RETURN_SPEED]); return; }
  if (Pc_StrEqIgnoreCase(key, "prog_delay_ms"))      { g_pcCmd.programValues[PC_PROG_DELAY_MS] = Pc_ParseInt32(value, g_pcCmd.programValues[PC_PROG_DELAY_MS]); return; }

  if (Pc_StrEqIgnoreCase(key, "program_save"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      Pc_SaveProgramToFlash();
      Pc_SyncTouchGfxModel();
      Pc_ReplyConfigSnapshot();
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "program_apply"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      Pc_SaveProgramToFlash();
      Pc_SyncTouchGfxModel();
      Pc_ReplyConfigSnapshot();
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "program_load"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      UiFlashProgramData programData;
      uint8_t i;
      if (UiFlashStorage_LoadProgram(&programData) != 0U)
      {
        for (i = 0U; i < PC_PROG_COUNT; i++)
        {
          g_pcCmd.programValues[i] = programData.values[i];
        }
        Pc_SyncTouchGfxModel();
        Pc_ReplyConfigSnapshot();
      }
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "program_start"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      if (Pc_ProgramStart() == 0U)
      {
        Pc_CmdReply("[CMD] program_start rejected");
      }
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "program_stop"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      Pc_ProgramStop();
    }
    return;
  }

  /* ---- Operation mode ---- */
  if (Pc_StrEqIgnoreCase(key, "mode_set"))
  {
    OperationMode_t req_mode = OP_MODE_MANUAL;
    if (Pc_StrEqIgnoreCase(value, "auto"))         { req_mode = OP_MODE_AUTO;     }
    else if (Pc_StrEqIgnoreCase(value, "setup"))   { req_mode = OP_MODE_SETUP;    }
    else if (Pc_StrEqIgnoreCase(value, "manual"))  { req_mode = OP_MODE_MANUAL;   }
    else if (Pc_StrEqIgnoreCase(value, "recovery")){ req_mode = OP_MODE_RECOVERY; }
    else
    {
      Pc_CmdReply("[CMD] mode_set unknown=%s", value);
      return;
    }

    if (PressStateMachine_SetMode(req_mode) != 0U)
    {
      Pc_CmdReply("[CMD] mode=%s ok", PressStateMachine_GetModeName(req_mode));
    }
    else
    {
      Pc_CmdReply("[CMD] mode_set rejected (interlock or active cycle)");
    }
    return;
  }

  /* ---- Cycle control ---- */
  if (Pc_StrEqIgnoreCase(key, "cycle_start"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      if (PressStateMachine_CycleStart() != 0U)
      {
        Pc_CmdReply("[CMD] cycle_start ok cycle=%lu", (unsigned long)PressStateMachine_GetCounters()->total + 1U);
      }
      else
      {
        Pc_CmdReply("[CMD] cycle_start rejected mode=%s step=%s",
                    PressStateMachine_GetModeName(PressStateMachine_GetMode()),
                    PressStateMachine_GetStateName(PressStateMachine_GetState()));
      }
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "cycle_stop"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      PressStateMachine_CycleStop();
      Pc_CmdReply("[CMD] cycle_stop requested");
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "cycle_abort"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      PressStateMachine_CycleAbort();
      Pc_CmdReply("[CMD] cycle_abort");
    }
    return;
  }

  /* ---- Interlock simulation (bench test without GPIO) ---- */
  if (Pc_StrEqIgnoreCase(key, "ilock_sim"))
  {
    InterlockManager_SetSimMode(Pc_ParseBool(value, 1U));
    Pc_CmdReply("[CMD] ilock_sim=%u", (unsigned int)Pc_ParseBool(value, 1U));
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "ilock_estop"))
  {
    InterlockManager_SimSetEstop(Pc_ParseBool(value, 1U));
    Pc_CmdReply("[CMD] ilock_estop=%u", (unsigned int)Pc_ParseBool(value, 1U));
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "ilock_door"))
  {
    InterlockManager_SimSetDoor(Pc_ParseBool(value, 1U));
    Pc_CmdReply("[CMD] ilock_door=%u", (unsigned int)Pc_ParseBool(value, 1U));
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "ilock_left"))
  {
    InterlockManager_SimSetTwoHandLeft(Pc_ParseBool(value, 0U));
    Pc_CmdReply("[CMD] ilock_left=%u", (unsigned int)Pc_ParseBool(value, 0U));
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "ilock_right"))
  {
    InterlockManager_SimSetTwoHandRight(Pc_ParseBool(value, 0U));
    Pc_CmdReply("[CMD] ilock_right=%u", (unsigned int)Pc_ParseBool(value, 0U));
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "ilock_home"))
  {
    InterlockManager_SetHomeComplete(Pc_ParseBool(value, 0U));
    Pc_CmdReply("[CMD] ilock_home=%u", (unsigned int)Pc_ParseBool(value, 0U));
    return;
  }

  /* ---- Press recipe parameters (Phase 1 single recipe via CMD) ---- */
  if (Pc_StrEqIgnoreCase(key, "press_approach_speed"))
  {
    PressStateMachine_GetConfig()->approach_speed = Pc_ParseInt32(value, PressStateMachine_GetConfig()->approach_speed);
    Pc_CmdReply("[CMD] press_approach_speed=%ld", (long)PressStateMachine_GetConfig()->approach_speed);
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "press_approach_pos"))
  {
    PressStateMachine_GetConfig()->approach_pos = Pc_ParseInt32(value, PressStateMachine_GetConfig()->approach_pos);
    Pc_CmdReply("[CMD] press_approach_pos=%ld", (long)PressStateMachine_GetConfig()->approach_pos);
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "press_contact_speed"))
  {
    PressStateMachine_GetConfig()->contact_speed = Pc_ParseInt32(value, PressStateMachine_GetConfig()->contact_speed);
    Pc_CmdReply("[CMD] press_contact_speed=%ld", (long)PressStateMachine_GetConfig()->contact_speed);
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "press_contact_th"))
  {
    int32_t v = Pc_ParseInt32(value, (int32_t)PressStateMachine_GetConfig()->contact_torque_th);
    PressStateMachine_GetConfig()->contact_torque_th = (v < 0) ? 0U : (v > 100) ? 100U : (uint16_t)v;
    Pc_CmdReply("[CMD] press_contact_th=%u", (unsigned int)PressStateMachine_GetConfig()->contact_torque_th);
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "press_speed"))
  {
    PressStateMachine_GetConfig()->press_speed = Pc_ParseInt32(value, PressStateMachine_GetConfig()->press_speed);
    Pc_CmdReply("[CMD] press_speed=%ld", (long)PressStateMachine_GetConfig()->press_speed);
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "press_target_pos"))
  {
    PressStateMachine_GetConfig()->press_target_pos = Pc_ParseInt32(value, PressStateMachine_GetConfig()->press_target_pos);
    Pc_CmdReply("[CMD] press_target_pos=%ld", (long)PressStateMachine_GetConfig()->press_target_pos);
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "press_max_force"))
  {
    int32_t v = Pc_ParseInt32(value, (int32_t)PressStateMachine_GetConfig()->press_max_force);
    PressStateMachine_GetConfig()->press_max_force = (v < 0) ? 0U : (v > 100) ? 100U : (uint16_t)v;
    Pc_CmdReply("[CMD] press_max_force=%u", (unsigned int)PressStateMachine_GetConfig()->press_max_force);
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "press_dwell_ms"))
  {
    int32_t v = Pc_ParseInt32(value, (int32_t)PressStateMachine_GetConfig()->dwell_time_ms);
    PressStateMachine_GetConfig()->dwell_time_ms = (v < 0) ? 0U : (uint32_t)v;
    Pc_CmdReply("[CMD] press_dwell_ms=%lu", (unsigned long)PressStateMachine_GetConfig()->dwell_time_ms);
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "press_return_speed"))
  {
    PressStateMachine_GetConfig()->return_speed = Pc_ParseInt32(value, PressStateMachine_GetConfig()->return_speed);
    Pc_CmdReply("[CMD] press_return_speed=%ld", (long)PressStateMachine_GetConfig()->return_speed);
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "press_return_pos"))
  {
    PressStateMachine_GetConfig()->return_pos = Pc_ParseInt32(value, PressStateMachine_GetConfig()->return_pos);
    Pc_CmdReply("[CMD] press_return_pos=%ld", (long)PressStateMachine_GetConfig()->return_pos);
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "press_timeout_ms"))
  {
    int32_t v = Pc_ParseInt32(value, (int32_t)PressStateMachine_GetConfig()->cycle_timeout_ms);
    PressStateMachine_GetConfig()->cycle_timeout_ms = (v < 0) ? 0U : (uint32_t)v;
    Pc_CmdReply("[CMD] press_timeout_ms=%lu", (unsigned long)PressStateMachine_GetConfig()->cycle_timeout_ms);
    return;
  }

  /* ---- Judgment limits ---- */
  if (Pc_StrEqIgnoreCase(key, "judge_force_max"))
  {
    int32_t v = Pc_ParseInt32(value, (int32_t)PressStateMachine_GetConfig()->judge_force_max);
    PressStateMachine_GetConfig()->judge_force_max = (v < 0) ? 0U : (v > 100) ? 100U : (uint16_t)v;
    Pc_CmdReply("[CMD] judge_force_max=%u", (unsigned int)PressStateMachine_GetConfig()->judge_force_max);
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "judge_force_min"))
  {
    int32_t v = Pc_ParseInt32(value, (int32_t)PressStateMachine_GetConfig()->judge_force_min);
    PressStateMachine_GetConfig()->judge_force_min = (v < 0) ? 0U : (v > 100) ? 100U : (uint16_t)v;
    Pc_CmdReply("[CMD] judge_force_min=%u", (unsigned int)PressStateMachine_GetConfig()->judge_force_min);
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "judge_pos_max"))
  {
    PressStateMachine_GetConfig()->judge_pos_max = Pc_ParseInt32(value, PressStateMachine_GetConfig()->judge_pos_max);
    Pc_CmdReply("[CMD] judge_pos_max=%ld", (long)PressStateMachine_GetConfig()->judge_pos_max);
    return;
  }
  if (Pc_StrEqIgnoreCase(key, "judge_pos_min"))
  {
    PressStateMachine_GetConfig()->judge_pos_min = Pc_ParseInt32(value, PressStateMachine_GetConfig()->judge_pos_min);
    Pc_CmdReply("[CMD] judge_pos_min=%ld", (long)PressStateMachine_GetConfig()->judge_pos_min);
    return;
  }

  /* ---- Status queries ---- */
  if (Pc_StrEqIgnoreCase(key, "press_status"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      const InterlockState_t *ilk = InterlockManager_GetState();
      const PressResult_t    *res = PressStateMachine_GetLastResult();
      const PressCounter_t   *cnt = PressStateMachine_GetCounters();
      Pc_CmdReply("PST,mode=%s,step=%s,estop=%u,door=%u,drive=%u,home=%u",
                  PressStateMachine_GetModeName(PressStateMachine_GetMode()),
                  PressStateMachine_GetStateName(PressStateMachine_GetState()),
                  (unsigned int)ilk->estop_ok,
                  (unsigned int)ilk->door_ok,
                  (unsigned int)ilk->drive_ready,
                  (unsigned int)ilk->home_complete);
      Pc_CmdReply("CNT,total=%lu,ok=%lu,ng=%lu,cng=%lu,rate=%u.%u",
                  (unsigned long)cnt->total,
                  (unsigned long)cnt->ok,
                  (unsigned long)cnt->ng,
                  (unsigned long)cnt->consecutive_ng,
                  (unsigned int)(cnt->ng_rate_x10 / 10U),
                  (unsigned int)(cnt->ng_rate_x10 % 10U));
      Pc_CmdReply("RST,cycle=%lu,result=%d,force=%u,pos=%ld,ms=%lu",
                  (unsigned long)res->cycle_number,
                  (int)res->result,
                  (unsigned int)res->peak_force_pct,
                  (long)res->end_position,
                  (unsigned long)res->cycle_time_ms);
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "counter_reset"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      PressStateMachine_ResetCounters();
      Pc_CmdReply("[CMD] counter_reset ok");
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "alarm_reset"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      AlarmManager_Reset();
      Pc_CmdReply("[CMD] alarm_reset ok active=%u mode=%s",
                  (unsigned int)AlarmManager_IsActive(),
                  PressStateMachine_GetModeName(PressStateMachine_GetMode()));
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "alarm_ack"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      AlarmManager_Ack();
      Pc_CmdReply("[CMD] alarm_ack ok");
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "alarm_status"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      AlarmManager_ReplyActive(Pc_CmdReply);
    }
    return;
  }

  /* ---- Recipe management ---- */
  if (Pc_StrEqIgnoreCase(key, "recipe_select"))
  {
    parsed = Pc_ParseInt32(value, -1);
    if ((parsed >= 0) && (parsed < (int32_t)RECIPE_MAX_COUNT))
    {
      if (RecipeManager_Activate((uint8_t)parsed) != 0U)
      {
        RecipeManager_ApplyActive();
        Pc_CmdReply("[CMD] recipe_select=%ld ok", (long)parsed);
        RecipeManager_ReplyConfig((uint8_t)parsed, Pc_CmdReply);
      }
      else
      {
        Pc_CmdReply("[CMD] recipe_select=%ld failed", (long)parsed);
      }
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "recipe_read"))
  {
    parsed = Pc_ParseInt32(value, (int32_t)RecipeManager_GetActiveIdx());
    if ((parsed >= 0) && (parsed < (int32_t)RecipeManager_GetCount()))
    {
      RecipeManager_ReplyConfig((uint8_t)parsed, Pc_CmdReply);
    }
    else
    {
      Pc_CmdReply("[CMD] recipe_read=%ld out of range count=%u", (long)parsed, (unsigned int)RecipeManager_GetCount());
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "recipe_apply"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      RecipeManager_ApplyActive();
      Pc_CmdReply("[CMD] recipe_apply ok idx=%u", (unsigned int)RecipeManager_GetActiveIdx());
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "recipe_clone"))
  {
    parsed = Pc_ParseInt32(value, -1);
    uint8_t src = RecipeManager_GetActiveIdx();
    if ((parsed >= 0) && (parsed < (int32_t)RECIPE_MAX_COUNT) && ((uint8_t)parsed != src))
    {
      if (RecipeManager_Clone(src, (uint8_t)parsed) != 0U)
      {
        Pc_CmdReply("[CMD] recipe_clone src=%u dst=%ld ok", (unsigned int)src, (long)parsed);
      }
      else
      {
        Pc_CmdReply("[CMD] recipe_clone failed");
      }
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "recipe_lock"))
  {
    uint8_t lock_val = Pc_ParseBool(value, 1U);
    if (RecipeManager_SetLock(RecipeManager_GetActiveIdx(), lock_val) != 0U)
    {
      Pc_CmdReply("[CMD] recipe_lock=%u ok", (unsigned int)lock_val);
    }
    else
    {
      Pc_CmdReply("[CMD] recipe_lock failed");
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "recipe_list"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      Pc_CmdReply("RCPL,count=%u,active=%u", (unsigned int)RecipeManager_GetCount(), (unsigned int)RecipeManager_GetActiveIdx());
      for (uint8_t ri = 0U; ri < RecipeManager_GetCount(); ri++)
      {
        const RecipeData_t *r = RecipeManager_GetSlot(ri);
        if (r != NULL)
        {
          Pc_CmdReply("RCPI,idx=%u,id=%u,name=%s,ver=%u,lock=%u,tpos=%ld",
                      (unsigned int)ri, (unsigned int)r->recipe_id,
                      r->product_name, (unsigned int)r->version,
                      (unsigned int)r->locked, (long)r->press_target_pos);
        }
      }
    }
    return;
  }

  /* ---- Save current press_* config to active recipe slot ---- */
  if (Pc_StrEqIgnoreCase(key, "recipe_save_active"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      RecipeData_t *r = RecipeManager_GetActive();
      if ((r != NULL) && (r->locked == 0U))
      {
        const PressConfig_t *cfg = PressStateMachine_GetConfig();
        r->approach_speed    = cfg->approach_speed;
        r->approach_pos      = cfg->approach_pos;
        r->contact_speed     = cfg->contact_speed;
        r->contact_torque_th = cfg->contact_torque_th;
        r->press_speed       = cfg->press_speed;
        r->press_target_pos  = cfg->press_target_pos;
        r->press_max_force   = cfg->press_max_force;
        r->dwell_time_ms     = cfg->dwell_time_ms;
        r->return_speed      = cfg->return_speed;
        r->return_pos        = cfg->return_pos;
        r->cycle_timeout_ms  = cfg->cycle_timeout_ms;
        r->judge_force_max   = cfg->judge_force_max;
        r->judge_force_min   = cfg->judge_force_min;
        r->judge_pos_max     = cfg->judge_pos_max;
        r->judge_pos_min     = cfg->judge_pos_min;
        r->judge_cycle_time_max = cfg->judge_cycle_time_max;
        r->version++;
        if (RecipeManager_FlushToFlash() != 0U)
        {
          Pc_CmdReply("[CMD] recipe_save_active ok idx=%u ver=%u", (unsigned int)RecipeManager_GetActiveIdx(), (unsigned int)r->version);
        }
        else
        {
          Pc_CmdReply("[CMD] recipe_save_active flash error");
        }
      }
      else
      {
        Pc_CmdReply("[CMD] recipe_save_active rejected (locked or null)");
      }
    }
    return;
  }

  /* ---- Cycle history queries ---- */
  if (Pc_StrEqIgnoreCase(key, "history_read"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      uint16_t cnt = CycleLogger_GetCount();
      Pc_CmdReply("HIST,count=%u", (unsigned int)cnt);
      uint16_t rows = (cnt > 10U) ? 10U : cnt;
      for (uint16_t hi = 0U; hi < rows; hi++)
      {
        const CycleRecord_t *rec = CycleLogger_GetLast((uint8_t)hi);
        if (rec == NULL) { break; }
        Pc_CmdReply("HSTI,n=%lu,result=%d,force=%u,pos=%ld,ms=%lu",
                    (unsigned long)rec->cycle_number,
                    (int)rec->result,
                    (unsigned int)rec->peak_force_pct,
                    (long)rec->end_position,
                    (unsigned long)rec->cycle_time_ms);
      }
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "result_read_last"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      const CycleRecord_t *rec = CycleLogger_GetLast(0U);
      if (rec != NULL)
      {
        Pc_CmdReply("RST,cycle=%lu,result=%d,force=%u,pos=%ld,ms=%lu",
                    (unsigned long)rec->cycle_number,
                    (int)rec->result,
                    (unsigned int)rec->peak_force_pct,
                    (long)rec->end_position,
                    (unsigned long)rec->cycle_time_ms);
      }
      else
      {
        Pc_CmdReply("RST,cycle=0,result=0,force=0,pos=0,ms=0");
      }
    }
    return;
  }

  if (Pc_StrEqIgnoreCase(key, "log_clear"))
  {
    if (Pc_ParseBool(value, 0U) != 0U)
    {
      CycleLogger_Clear();
      Pc_CmdReply("[CMD] log_clear ok");
    }
    return;
  }

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
  if (g_pcCmd.parameterReadPending != 0U)
  {
    int32_t values[UI_FLASH_PARAMETER_VALUE_COUNT] = {0};
    if (SOEM_FetchParameterReadAll(values, UI_FLASH_PARAMETER_VALUE_COUNT) != 0U)
    {
      g_pcCmd.parameterValues[PC_PARAM_ACC_TIME] = values[1];
      g_pcCmd.parameterValues[PC_PARAM_DEC_TIME] = values[2];
      g_pcCmd.parameterValues[PC_PARAM_LIMIT_PLUS] = values[3];
      g_pcCmd.parameterValues[PC_PARAM_LIMIT_MINUS] = values[4];
      g_pcCmd.parameterValues[PC_PARAM_UNIT_SCALE] = values[5];
      g_pcCmd.parameterValues[PC_PARAM_POSITION_GAIN] = values[7];
      g_pcCmd.parameterReadPending = 0U;
      Pc_HomeDiagLog("param_read_all");
      Pc_SaveParameterToFlash();
      Pc_SyncTouchGfxModel();
      Pc_CmdReply("[CMD] param_read_all ok");
      Pc_ReplyConfigSnapshot();
    }
    else if ((HAL_GetTick() - g_pcCmd.parameterReadStartMs) > 1800U)
    {
      g_pcCmd.parameterReadPending = 0U;
      Pc_CmdReply("[CMD] param_read_all timeout");
    }
  }

  Pc_ProgramTick();
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
  UART4_SendText(msg);
#endif
}

static void WIFI_UART7_QuickAtProbe(void)
{
  static const uint8_t atCmd[] = {'A', 'T', '\r', '\n'};
  uint8_t rxBuf[10] = {0};
  uint8_t rxByte = 0U;
  uint32_t probeOk = 0U;
  char msg[200];

  UART4_SendText("[WIFI] Switching to UART7 for MB1400...\r\n");

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
    UART4_SendText(msg);

    if (rxOk != 0U)
    {
      /* Keep working RTS level for subsequent AT init. */
      break;
    }
  }

  if (probeOk == 0U)
  {
    UART4_SendText("[WIFI] Still no response on UART7. Check PE7/PE8 AF config and SB1/SB5/SB7.\r\n");
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
  UART4_SendText(msg);
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
    UART4_SendText(msg);

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
    UART4_SendText(msg);
  }
  else
  {
    UART4_SendText("[WIFI] SPI4 loopback no valid route found\\r\\n");
  }
#endif
}
#endif
#endif /* Wi-Fi/UART7/SPI4 diagnostics disabled */

/* Called by HAL when DMA TX completes */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART4)
  {
    uart_dma_busy = 0;
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART4)
  {
    uint16_t nextHead = (uint16_t)((g_pcUartRxHead + 1U) % PC_UART_RX_RING_SIZE);
    if (nextHead != g_pcUartRxTail)
    {
      g_pcUartRxRing[g_pcUartRxHead] = g_pcUartRxByte;
      g_pcUartRxHead = nextHead;
    }

    (void)HAL_UART_Receive_IT(&huart4, &g_pcUartRxByte, 1U);
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Update SystemCoreClock variable according to RCC registers values. */
  SystemCoreClockUpdate();

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  SettingsPersistence_Init();

  /* USER CODE END Init */

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_HPDMA1_Init();
  MX_LTDC_Init();
  MX_CRC_Init();
  MX_DMA2D_Init();
  MX_JPEG_Init();
  MX_FLASH_Init();
  MX_I2C1_Init();
  MX_UART4_Init();
  Pc_CommandEnsureRxArmed();
  MX_UART7_Init();
  MX_GPU2D_Init();
  MX_ICACHE_GPU2D_Init();
  MX_GFXMMU_Init();
  MX_TouchGFX_Init();
  /* Call PreOsInit function */
  MX_TouchGFX_PreOSInit();

  /* Enable LCD panel power and backlight (both were left disabled for ETH-only tests). */
  HAL_GPIO_WritePin(LCD_EN_GPIO_Port, LCD_EN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_SET);
  /* USER CODE BEGIN 2 */
  // ETH init moved to EtherCAT_Task to avoid blocking
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of TouchGFXTask */
  TouchGFXTaskHandle = osThreadNew(TouchGFX_Task, NULL, &TouchGFXTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  static const osThreadAttr_t ethercatTask_attributes = {
    .name = "EtherCATTask",
    .stack_size = 4096 * 4,
    .priority = (osPriority_t) osPriorityRealtime,  /* Highest — 1ms cycle critical */
  };
  osThreadNew(EtherCAT_Task, NULL, &ethercatTask_attributes);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief CRC Initialization Function
  * @param None
  * @retval None
  */
static void MX_CRC_Init(void)
{

  /* USER CODE BEGIN CRC_Init 0 */

  /* USER CODE END CRC_Init 0 */

  /* USER CODE BEGIN CRC_Init 1 */

  /* USER CODE END CRC_Init 1 */
  hcrc.Instance = CRC;
  hcrc.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE;
  hcrc.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE;
  hcrc.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_NONE;
  hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
  hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRC_Init 2 */

  /* USER CODE END CRC_Init 2 */

}

/**
  * @brief UART4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART4_Init(void)
{
  huart4.Instance = UART4;
  huart4.Init.BaudRate = 115200;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX_RX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart4.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart4, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart4, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief UART7 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART7_Init(void)
{
  huart7.Instance = UART7;
  huart7.Init.BaudRate = 115200;
  huart7.Init.WordLength = UART_WORDLENGTH_8B;
  huart7.Init.StopBits = UART_STOPBITS_1;
  huart7.Init.Parity = UART_PARITY_NONE;
  huart7.Init.Mode = UART_MODE_TX_RX;
  huart7.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart7.Init.OverSampling = UART_OVERSAMPLING_16;
  huart7.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart7.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart7.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart7) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart7, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart7, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart7) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief DMA2D Initialization Function
  * @param None
  * @retval None
  */
static void MX_DMA2D_Init(void)
{

  /* USER CODE BEGIN DMA2D_Init 0 */

  /* USER CODE END DMA2D_Init 0 */

  /* USER CODE BEGIN DMA2D_Init 1 */

  /* USER CODE END DMA2D_Init 1 */
  hdma2d.Instance = DMA2D;
  hdma2d.Init.Mode = DMA2D_R2M;
  hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB888;
  hdma2d.Init.OutputOffset = 0;
  if (HAL_DMA2D_Init(&hdma2d) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DMA2D_Init 2 */

  /* USER CODE END DMA2D_Init 2 */

}

/**
  * @brief FLASH Initialization Function
  * @param None
  * @retval None
  */
static void MX_FLASH_Init(void)
{

  /* USER CODE BEGIN FLASH_Init 0 */

  /* USER CODE END FLASH_Init 0 */

  FLASH_OBProgramInitTypeDef pOBInit = {0};

  /* USER CODE BEGIN FLASH_Init 1 */

  /* USER CODE END FLASH_Init 1 */
  HAL_FLASHEx_OBGetConfig(&pOBInit);
  if ((pOBInit.USERConfig1 & OB_IWDG_SW) != OB_IWDG_SW||
(pOBInit.USERConfig1 & OB_XSPI1_HSLV_ENABLE) != OB_XSPI1_HSLV_ENABLE||
(pOBInit.USERConfig1 & OB_XSPI2_HSLV_ENABLE) != OB_XSPI2_HSLV_ENABLE||
(pOBInit.USERConfig2 & OB_I2C_NI3C_I2C) != OB_I2C_NI3C_I2C)
  {
  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_FLASH_OB_Unlock() != HAL_OK)
  {
    Error_Handler();
  }
  pOBInit.OptionType = OPTIONBYTE_USER;
  pOBInit.USERType = OB_USER_IWDG_SW|OB_USER_XSPI1_HSLV
                              |OB_USER_XSPI2_HSLV|OB_USER_I2C_NI3C;
  pOBInit.USERConfig1 = OB_IWDG_SW|OB_XSPI1_HSLV_ENABLE
                              |OB_XSPI2_HSLV_ENABLE;
  pOBInit.USERConfig2 = OB_I2C_NI3C_I2C;
  if (HAL_FLASHEx_OBProgram(&pOBInit) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_FLASH_OB_Lock() != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_FLASH_Lock() != HAL_OK)
  {
    Error_Handler();
  }
  }
  /* USER CODE BEGIN FLASH_Init 2 */

  /* USER CODE END FLASH_Init 2 */

}

/**
  * @brief GFXMMU Initialization Function
  * @param None
  * @retval None
  */
static void MX_GFXMMU_Init(void)
{

  /* USER CODE BEGIN GFXMMU_Init 0 */

  /* USER CODE END GFXMMU_Init 0 */

  GFXMMU_PackingTypeDef pPacking = {0};

  /* USER CODE BEGIN GFXMMU_Init 1 */

  /* USER CODE END GFXMMU_Init 1 */
  hgfxmmu.Instance = GFXMMU;
  hgfxmmu.Init.BlockSize = GFXMMU_12BYTE_BLOCKS;
  hgfxmmu.Init.DefaultValue = 0;
  hgfxmmu.Init.AddressTranslation = DISABLE;
  hgfxmmu.Init.Buffers.Buf0Address = 0x90000000;
  hgfxmmu.Init.Buffers.Buf1Address = 0x90200000;
  hgfxmmu.Init.Buffers.Buf2Address = 0;
  hgfxmmu.Init.Buffers.Buf3Address = 0;
  hgfxmmu.Init.Interrupts.Activation = DISABLE;
  if (HAL_GFXMMU_Init(&hgfxmmu) != HAL_OK)
  {
    Error_Handler();
  }
  pPacking.Buffer0Activation = ENABLE;
  pPacking.Buffer0Mode = GFXMMU_PACKING_MSB_REMOVE;
  pPacking.Buffer1Activation = ENABLE;
  pPacking.Buffer1Mode = GFXMMU_PACKING_MSB_REMOVE;
  pPacking.Buffer2Activation = DISABLE;
  pPacking.Buffer2Mode = GFXMMU_PACKING_MSB_REMOVE;
  pPacking.Buffer3Activation = DISABLE;
  pPacking.Buffer3Mode = GFXMMU_PACKING_MSB_REMOVE;
  pPacking.DefaultAlpha = 0xFF;
  if (HAL_GFXMMU_ConfigPacking(&hgfxmmu, &pPacking) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN GFXMMU_Init 2 */

  /* USER CODE END GFXMMU_Init 2 */

}

/**
  * @brief GPU2D Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPU2D_Init(void)
{

  /* USER CODE BEGIN GPU2D_Init 0 */

  /* USER CODE END GPU2D_Init 0 */

  /* USER CODE BEGIN GPU2D_Init 1 */

  /* USER CODE END GPU2D_Init 1 */
  hgpu2d.Instance = GPU2D;
  if (HAL_GPU2D_Init(&hgpu2d) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN GPU2D_Init 2 */

  /* USER CODE END GPU2D_Init 2 */

}

/**
  * @brief HPDMA1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_HPDMA1_Init(void)
{

  /* USER CODE BEGIN HPDMA1_Init 0 */

  /* USER CODE END HPDMA1_Init 0 */

  /* Peripheral clock enable */
  __HAL_RCC_HPDMA1_CLK_ENABLE();

  /* HPDMA1 interrupt Init */
    HAL_NVIC_SetPriority(HPDMA1_Channel0_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(HPDMA1_Channel0_IRQn);
    HAL_NVIC_SetPriority(HPDMA1_Channel1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(HPDMA1_Channel1_IRQn);

  /* USER CODE BEGIN HPDMA1_Init 1 */

  /* USER CODE END HPDMA1_Init 1 */
  /* USER CODE BEGIN HPDMA1_Init 2 */

  /* USER CODE END HPDMA1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00E063FF;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief ICACHE_GPU2D Initialization Function
  * @param None
  * @retval None
  */
static void MX_ICACHE_GPU2D_Init(void)
{

  /* USER CODE BEGIN ICACHE_GPU2D_Init 0 */

  /* USER CODE END ICACHE_GPU2D_Init 0 */

  /* USER CODE BEGIN ICACHE_GPU2D_Init 1 */

  /* USER CODE END ICACHE_GPU2D_Init 1 */

  /** Enable instruction cache (default 2-ways set associative cache)
  */
  if (HAL_ICACHE_Enable() != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ICACHE_GPU2D_Init 2 */

  /* USER CODE END ICACHE_GPU2D_Init 2 */

}

/**
  * @brief JPEG Initialization Function
  * @param None
  * @retval None
  */
static void MX_JPEG_Init(void)
{

  /* USER CODE BEGIN JPEG_Init 0 */

  /* USER CODE END JPEG_Init 0 */

  /* USER CODE BEGIN JPEG_Init 1 */

  /* USER CODE END JPEG_Init 1 */
  hjpeg.Instance = JPEG;
  if (HAL_JPEG_Init(&hjpeg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN JPEG_Init 2 */

  /* USER CODE END JPEG_Init 2 */

}

/**
  * @brief LTDC Initialization Function
  * @param None
  * @retval None
  */
static void MX_LTDC_Init(void)
{

  /* USER CODE BEGIN LTDC_Init 0 */

  /* USER CODE END LTDC_Init 0 */

  LTDC_LayerCfgTypeDef pLayerCfg = {0};

  /* USER CODE BEGIN LTDC_Init 1 */

  /* USER CODE END LTDC_Init 1 */
  hltdc.Instance = LTDC;
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  hltdc.Init.HorizontalSync = 4;
  hltdc.Init.VerticalSync = 4;
  hltdc.Init.AccumulatedHBP = 12;
  hltdc.Init.AccumulatedVBP = 12;
  hltdc.Init.AccumulatedActiveW = 812;
  hltdc.Init.AccumulatedActiveH = 492;
  hltdc.Init.TotalWidth = 820;
  hltdc.Init.TotalHeigh = 506;
  hltdc.Init.Backcolor.Blue = 0;
  hltdc.Init.Backcolor.Green = 0;
  hltdc.Init.Backcolor.Red = 0;
  if (HAL_LTDC_Init(&hltdc) != HAL_OK)
  {
    Error_Handler();
  }
  pLayerCfg.WindowX0 = 0;
  pLayerCfg.WindowX1 = 800;
  pLayerCfg.WindowY0 = 0;
  pLayerCfg.WindowY1 = 480;
  pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_ARGB8888;
  pLayerCfg.Alpha = 255;
  pLayerCfg.Alpha0 = 0;
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
  pLayerCfg.FBStartAdress = GFXMMU_VIRTUAL_BUFFER0_BASE;
  pLayerCfg.ImageWidth = 800;
  pLayerCfg.ImageHeight = 480;
  pLayerCfg.Backcolor.Blue = 0;
  pLayerCfg.Backcolor.Green = 0;
  pLayerCfg.Backcolor.Red = 0;
  if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LTDC_Init 2 */
  // Reconfigure pixelformat since TouchGFX project generator does not allow setting different format for LTDC and remaining configuration
  // This way TouchGFX runs 32BPP mode but the LTDC accesses the real framebuffer in 24BPP
  pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB888;
  if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE END LTDC_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOO_CLK_ENABLE();  /* For user LEDs LD1(PO1), LD2(PO5) */

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOF, FRAME_RATE_Pin|RENDER_TIME_Pin|MCU_ACTIVE_Pin|VSYNC_FREQ_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level - LCD DISABLED */
  HAL_GPIO_WritePin(LCD_EN_GPIO_Port, LCD_EN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level - LCD Backlight DISABLED */
  HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level - MB1400 reset deasserted (active low). */
  HAL_GPIO_WritePin(MB1400_RESET_GPIO_Port, MB1400_RESET_Pin, GPIO_PIN_SET);
  /*Configure GPIO pin Output Level - MB1400 RTS asserted (active low) to allow module TX. */
  HAL_GPIO_WritePin(MB1400_RTS_GPIO_Port, MB1400_RTS_Pin, GPIO_PIN_RESET);
  /*Configure GPIO pin Output Level - MB1400 CHIP_EN asserted (active high). */
  HAL_GPIO_WritePin(MB1400_CHIP_EN_GPIO_Port, MB1400_CHIP_EN_Pin, GPIO_PIN_SET);
  
  /*Configure GPIO pin Output Level - User LEDs OFF */
  HAL_GPIO_WritePin(GPIOO, GPIO_PIN_1 | GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pins : FRAME_RATE_Pin RENDER_TIME_Pin VSYNC_FREQ_Pin */
  GPIO_InitStruct.Pin = FRAME_RATE_Pin|RENDER_TIME_Pin|VSYNC_FREQ_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /*Configure GPIO pin : MCU_ACTIVE_Pin */
  GPIO_InitStruct.Pin = MCU_ACTIVE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MCU_ACTIVE_GPIO_Port, &GPIO_InitStruct);
  
  /*Configure GPIO pins : PO1 (LD1 Green), PO5 (LD2 Orange) - User LEDs */
  GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOO, &GPIO_InitStruct);

  /*Configure GPIO pin : LCD_EN_Pin */
  GPIO_InitStruct.Pin = LCD_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LCD_EN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : TP_IRQ_Pin */
  GPIO_InitStruct.Pin = TP_IRQ_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(TP_IRQ_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LCD_BL_CTRL_Pin */
  GPIO_InitStruct.Pin = LCD_BL_CTRL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LCD_BL_CTRL_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MB1400_RESET_Pin */
  GPIO_InitStruct.Pin = MB1400_RESET_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MB1400_RESET_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MB1400_RTS_Pin */
  GPIO_InitStruct.Pin = MB1400_RTS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MB1400_RTS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MB1400_CHIP_EN_Pin */
  GPIO_InitStruct.Pin = MB1400_CHIP_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MB1400_CHIP_EN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MB1400_INT_Pin */
  GPIO_InitStruct.Pin = MB1400_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(MB1400_INT_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(TP_IRQ_EXTI_IRQn, 7, 0);
  HAL_NVIC_EnableIRQ(TP_IRQ_EXTI_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
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

  /* Note: Clock configuration is done in HAL_ETH_MspInit (stm32h7rsxx_hal_msp.c) */
  
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

  /* User LED defines: LD1 (Green) = PO1, LD2 (Orange) = PO5 */
  #define LD1_PORT GPIOO
  #define LD1_PIN  GPIO_PIN_1
  #define LD2_PORT GPIOO
  #define LD2_PIN  GPIO_PIN_5

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

  UART4_SendText("[ETH] task start\r\n");

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
          (void)HAL_UART_Transmit(&huart4, (uint8_t *)line, (uint16_t)len, 100);
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

  UART4_SendText("\r\n[SOEM] Initializing EtherCAT master...\r\n");
  
  /* Set up SOEM logging to UART */
  SOEM_PortSetLog(UART4_SendText);
  
  /* Initialize SOEM */
  SOEM_PortInit();
  Pc_CommandInit();
  
  /* Initialize Phase 1+2 modules */
  InterlockManager_Init();
  PressStateMachine_Init();
  RecipeManager_Init();
  CycleLogger_Init();
  AlarmManager_Init();
  RecipeManager_ApplyActive();
  UART4_SendText("[PSM] Press SM + Recipe + Logger + Alarm initialized\r\n");

  UART4_SendText("[SOEM] Starting cyclic processing\r\n");
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

    /* Measure SOEM_PortPoll execution time */
    uint32_t poll_start = *DWT_CYCCNT_PTR;

    /* Poll SOEM state machine */
    SOEM_PortPoll();
    Pc_CommandTick();

    /* Update interlock state and notify drive status */
    InterlockManager_Update();
    {
      const uint16_t sw = SOEM_GetStatusword();
      uint8_t drive_op = (((sw & 0x006FU) == 0x0027U) &&
                          (SOEM_GetRunEnable() != 0U) &&
                          (SOEM_GetPdoReady() != 0U)) ? 1U : 0U;
      PressStateMachine_NotifyDriveState(drive_op);
    }

    /* Press state machine tick (runs only in AUTO mode) */
    PressStateMachine_Tick();

    /* Alarm manager tick: watch interlock + consecutive NG */
    AlarmManager_Tick();

    /* Auto-record cycle result when state transitions to CYCLE_END or CYCLE_NG */
    {
      static PressState_t s_prev_state = PRESS_STATE_IDLE;
      PressState_t s_now = PressStateMachine_GetState();
      if ((s_prev_state != s_now) &&
          ((s_now == PRESS_STATE_CYCLE_END) || (s_now == PRESS_STATE_CYCLE_NG)))
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
        Pc_CmdReply("RST,cycle=%lu,result=%d,force=%u,pos=%ld,ms=%lu",
                    (unsigned long)res->cycle_number,
                    (int)res->result,
                    (unsigned int)res->peak_force_pct,
                    (long)res->end_position,
                    (unsigned long)res->cycle_time_ms);
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
        UART4_SendText(msg);
        
        if (current_state == 8)  /* EC_STATE_OPERATIONAL */
        {
          UART4_SendText("[SOEM] Slave reached OPERATIONAL state\r\n");
          HAL_GPIO_WritePin(LD1_PORT, LD1_PIN, GPIO_PIN_SET);
        }
      }
    }
    
    g_slaveCount = (uint8_t)soem_context.slavecount;
    #endif
    
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
      UART4_SendText(tbuf);
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
  uint32_t lastWebTelMs = 0U;
  char webTel[128];

  /* Wi-Fi runtime is intentionally disabled.
   * Keep this task alive without touching EMW3080/UART7 paths. */
  // char wifiMsg[240];
  // const char* wifiDiag;
  // int32_t wifiDiagCode;
  //
  // #if WIFI_SPI4_LOOPBACK_TEST
  // UART4_SendText("[WIFI] SPI4 loopback mode start (set SB2/SB6/SB8 ON, short CN3 pin2(PE14 MOSI)<->pin3(PE13 MISO))\r\n");
  // WIFI_SPI4_LoopbackSelfTest();
  // UART4_SendText("[WIFI] SPI4 loopback mode done, Wi-Fi init skipped\r\n");
  // for(;;)
  // {
  //   osDelay(1000);
  // }
  // #endif
  //
  // /* MB1400 UART mode uses UART7 on STM32H7S78-DK STMod+ routing. */
  // UART4_SendText("[WIFI] init starting (UART7 AT sync ~14s timeout)...\r\n");
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
  // UART4_SendText(wifiMsg);

  UART4_SendText("[WEBUI] ST-LINK UART4 bridge active\r\n");

  /* Infinite loop */
  for(;;)
  {
    uint32_t now = HAL_GetTick();
    if ((now - lastWebTelMs) >= 200U)
    {
      lastWebTelMs = now;
      (void)snprintf(webTel,
                     sizeof(webTel),
                     "TEL,pos=%ld,vel=%ld,torque=%d,run=%u,status=0x%04X\r\n",
                     (long)SOEM_GetPositionActual(),
                     (long)SOEM_GetVelocityActual(),
                     (int)SOEM_GetTorqueActual(),
                     (unsigned int)SOEM_GetRunEnable(),
                     (unsigned int)SOEM_GetStatusword());
      UART4_SendText(webTel);
      UART_LogFlush();
    }

    /* Wi-Fi disabled: do not run EMW3080 process path. */
    osDelay(10);
  }
  /* USER CODE END 5 */
}

 /* MPU Configuration */

static void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /* Disables all MPU regions */
  for(uint8_t i=0; i<__MPU_REGIONCOUNT; i++)
  {
    HAL_MPU_DisableRegion(i);
  }

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0x70000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_128MB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER2;
  MPU_InitStruct.Size = MPU_REGION_SIZE_2MB;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER3;
  MPU_InitStruct.BaseAddress = 0x90000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_32MB;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER4;
  MPU_InitStruct.BaseAddress = 0x20000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_64KB;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER5;
  MPU_InitStruct.BaseAddress = 0x24000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER6;
  MPU_InitStruct.BaseAddress = 0x24068000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_32KB;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;  /* TEX=1: Normal memory */
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;   /* C=0 */
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE; /* B=0 */
  /* TEX=1,C=0,B=0 = Normal Non-cacheable (allows unaligned access) */

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER7;
  MPU_InitStruct.BaseAddress = 0x24070000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_8KB;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER8;
  MPU_InitStruct.BaseAddress = 0x25000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_16MB;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region for ETH DMA descriptors (SRAM_AHB)
  *   Must be Device memory or Non-Cacheable for DMA coherency
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER9;
  MPU_InitStruct.BaseAddress = 0x30000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_32KB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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
