/* NUCLEO-H753ZI HAL MSP: USART3(VCP), ETH(LAN8742A), TIM6(timebase) */
#include "main.h"

/* ETH DMA handles declared in soem_port.c */
extern ETH_HandleTypeDef heth;
extern DMA_HandleTypeDef hdma_eth_tx;
extern DMA_HandleTypeDef hdma_eth_rx;

void HAL_MspInit(void)
{
  __HAL_RCC_SYSCFG_CLK_ENABLE();
}

/* ── USART3 (NUCLEO VCP: PD8=TX, PD9=RX) ─────────────────────────────── */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
  GPIO_InitTypeDef gpio = {0};
  if (huart->Instance == USART3)
  {
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    /* PD8=USART3_TX  PD9=USART3_RX */
    gpio.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOD, &gpio);

    HAL_NVIC_SetPriority(USART3_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART3)
  {
    __HAL_RCC_USART3_FORCE_RESET();
    __HAL_RCC_USART3_RELEASE_RESET();
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_8 | GPIO_PIN_9);
    HAL_NVIC_DisableIRQ(USART3_IRQn);
  }
}

/* ── ETH (LAN8742A RMII: NUCLEO-H753ZI standard pinout) ──────────────── */
void HAL_ETH_MspInit(ETH_HandleTypeDef *hethx)
{
  GPIO_InitTypeDef gpio = {0};
  (void)hethx;

  /* Enable clocks */
  __HAL_RCC_ETH1MAC_CLK_ENABLE();
  __HAL_RCC_ETH1TX_CLK_ENABLE();
  __HAL_RCC_ETH1RX_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*
   * RMII pin mapping (NUCLEO-H753ZI):
   *   PA1  = ETH_REF_CLK   AF11
   *   PA2  = ETH_MDIO      AF11
   *   PA7  = ETH_CRS_DV    AF11
   *   PB13 = ETH_TXD1      AF11
   *   PC1  = ETH_MDC       AF11
   *   PC4  = ETH_RXD0      AF11
   *   PC5  = ETH_RXD1      AF11
   *   PG11 = ETH_TX_EN     AF11
   *   PG13 = ETH_TXD0      AF11
   */
  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = GPIO_NOPULL;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF11_ETH;

  gpio.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = GPIO_PIN_13;
  HAL_GPIO_Init(GPIOB, &gpio);

  gpio.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
  HAL_GPIO_Init(GPIOC, &gpio);

  gpio.Pin = GPIO_PIN_11 | GPIO_PIN_13;
  HAL_GPIO_Init(GPIOG, &gpio);

  HAL_NVIC_SetPriority(ETH_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(ETH_IRQn);
}

void HAL_ETH_MspDeInit(ETH_HandleTypeDef *hethx)
{
  (void)hethx;
  __HAL_RCC_ETH1MAC_FORCE_RESET();
  __HAL_RCC_ETH1MAC_RELEASE_RESET();
  HAL_GPIO_DeInit(GPIOA, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7);
  HAL_GPIO_DeInit(GPIOB, GPIO_PIN_13);
  HAL_GPIO_DeInit(GPIOC, GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5);
  HAL_GPIO_DeInit(GPIOG, GPIO_PIN_11 | GPIO_PIN_13);
  HAL_NVIC_DisableIRQ(ETH_IRQn);
}

/* ── TIM6: HAL timebase (1ms tick) ───────────────────────────────────── */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6)
  {
    __HAL_RCC_TIM6_CLK_ENABLE();
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
  }
}

void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6)
  {
    __HAL_RCC_TIM6_FORCE_RESET();
    __HAL_RCC_TIM6_RELEASE_RESET();
    HAL_NVIC_DisableIRQ(TIM6_DAC_IRQn);
  }
}
