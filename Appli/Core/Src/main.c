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
#include <string.h>
#include "settings_persistence.h"
#include "stm32h7rsxx_hal_eth.h"
#include "soem/soem.h"
#include "osal.h"
#include "lan8742.h"
#include "soem_port.h"
#include "task.h"  /* vTaskDelayUntil */
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
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
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
static void MX_GPU2D_Init(void);
static void MX_ICACHE_GPU2D_Init(void);
static void MX_GFXMMU_Init(void);
void StartDefaultTask(void *argument);
extern void TouchGFX_Task(void *argument);
static void MX_ETH_Init(void);

/* USER CODE BEGIN PFP */
void EtherCAT_Task(void *argument);
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

  /* Copy chunk out of ring buffer */
  uint16_t n = 0;
  while (t != h && n < sizeof(uart_dma_chunk))
  {
    uart_dma_chunk[n++] = uart_log_buf[t];
    t = (t + 1U) % UART_LOG_BUF_SIZE;
  }
  uart_log_tail = t;
  uart_dma_busy = 1;
  if (HAL_UART_Transmit_IT(&huart4, (uint8_t *)uart_dma_chunk, n) != HAL_OK)
  {
    /* IT not available — fall back to blocking */
    uart_dma_busy = 0;
    (void)HAL_UART_Transmit(&huart4, (uint8_t *)uart_dma_chunk, n, 50);
  }
}

/* Called by HAL when DMA TX completes */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART4)
  {
    uart_dma_busy = 0;
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
  __HAL_RCC_GPIOO_CLK_ENABLE();  /* For user LEDs LD1(PO1), LD2(PO5) */

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOF, FRAME_RATE_Pin|RENDER_TIME_Pin|MCU_ACTIVE_Pin|VSYNC_FREQ_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level - LCD DISABLED */
  HAL_GPIO_WritePin(LCD_EN_GPIO_Port, LCD_EN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level - LCD Backlight DISABLED */
  HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_RESET);
  
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
    /* Measure SOEM_PortPoll execution time */
    uint32_t poll_start = *DWT_CYCCNT_PTR;

    /* Poll SOEM state machine */
    SOEM_PortPoll();

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
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
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
