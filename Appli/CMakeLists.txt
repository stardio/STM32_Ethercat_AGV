cmake_minimum_required(VERSION 3.22)

project(CartesianRobot_H753 C CXX ASM)

# Application sources
set(APPLI_SOURCES
    Core/Src/main.c
    Core/Src/stm32h7xx_hal_msp.c
    Core/Src/stm32h7xx_hal_timebase_tim.c
    Core/Src/stm32h7xx_it.c
    Core/Src/system_stm32h7xx.c
    Core/Src/freertos.c
    Core/Src/settings_persistence.c
    Core/Src/ui_flash_storage.c
    Core/Src/soem_port.c
    Core/Src/axis_config.c
    Core/Src/interpolator.c
    Core/Src/uart_protocol.c
    Core/Src/interlock_manager.c
    Core/Src/press_state_machine.c
    Core/Src/recipe_manager.c
    Core/Src/cycle_logger.c
    Core/Src/alarm_manager.c
    Core/Src/user_auth.c
    Core/Src/maint_counter.c
    Core/Src/syscalls.c
    ${CMAKE_CURRENT_SOURCE_DIR}/startup_stm32h753xx.s
)

# STM32H7xx HAL Driver sources
set(HAL_DIR ${CMAKE_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src)
set(HAL_SOURCES
    ${HAL_DIR}/stm32h7xx_hal.c
    ${HAL_DIR}/stm32h7xx_hal_cortex.c
    ${HAL_DIR}/stm32h7xx_hal_dma.c
    ${HAL_DIR}/stm32h7xx_hal_dma_ex.c
    ${HAL_DIR}/stm32h7xx_hal_eth.c
    ${HAL_DIR}/stm32h7xx_hal_eth_ex.c
    ${HAL_DIR}/stm32h7xx_hal_exti.c
    ${HAL_DIR}/stm32h7xx_hal_flash.c
    ${HAL_DIR}/stm32h7xx_hal_flash_ex.c
    ${HAL_DIR}/stm32h7xx_hal_gpio.c
    ${HAL_DIR}/stm32h7xx_hal_pwr.c
    ${HAL_DIR}/stm32h7xx_hal_pwr_ex.c
    ${HAL_DIR}/stm32h7xx_hal_rcc.c
    ${HAL_DIR}/stm32h7xx_hal_rcc_ex.c
    ${HAL_DIR}/stm32h7xx_hal_tim.c
    ${HAL_DIR}/stm32h7xx_hal_tim_ex.c
    ${HAL_DIR}/stm32h7xx_hal_uart.c
    ${HAL_DIR}/stm32h7xx_hal_uart_ex.c
)

# FreeRTOS sources
file(GLOB FREERTOS_SOURCES
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/*.c
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1/*.c
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/*.c
)

# SOEM EtherCAT sources
set(SOEM_DIR ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/SOEM)
file(GLOB SOEM_SOURCES
    ${SOEM_DIR}/src/*.c
    ${SOEM_DIR}/osal/stm32/*.c
    ${SOEM_DIR}/oshw/stm32/*.c
)

# LAN8742 PHY driver
set(LAN8742_SOURCES
    ${CMAKE_SOURCE_DIR}/Drivers/BSP/Components/lan8742.c
)

add_executable(${PROJECT_NAME}
    ${APPLI_SOURCES}
    ${HAL_SOURCES}
    ${FREERTOS_SOURCES}
    ${SOEM_SOURCES}
    ${LAN8742_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    Core/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Include
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/RTOS2/Include
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/include
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
    ${SOEM_DIR}/include
    ${SOEM_DIR}/osal
    ${SOEM_DIR}/osal/stm32
    ${SOEM_DIR}/oshw/stm32
    ${CMAKE_SOURCE_DIR}/Drivers/BSP/Components
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32H753xx
    EC_VER4
    SOEM_ENABLED
)

# Linker script
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/stm32h753zi_flash.ld)
target_link_options(${PROJECT_NAME} PRIVATE
    -T${LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,-Map=${PROJECT_NAME}.map,--cref
)

# Generate binary
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
    COMMENT "Generating ${PROJECT_NAME}.bin"
)
