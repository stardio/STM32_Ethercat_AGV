#include "main.h"
#include "stm32h7xx_it.h"

extern ETH_HandleTypeDef heth;
extern UART_HandleTypeDef huart3;

void NMI_Handler(void)            { while(1) {} }
void HardFault_Handler(void)      { while(1) {} }
void MemManage_Handler(void)      { while(1) {} }
void BusFault_Handler(void)       { while(1) {} }
void UsageFault_Handler(void)     { while(1) {} }
void DebugMon_Handler(void)       {}

/* SVC / PendSV / SysTick are handled by FreeRTOS port */

void ETH_IRQHandler(void)
{
  HAL_ETH_IRQHandler(&heth);
}

void USART3_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart3);
}
