
#ifndef STM32F1xx_HAL_CONF_H
#define STM32F1xx_HAL_CONF_H

#define HAL_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED

#define HSE_VALUE    ((uint32_t)8000000) 
#define HSE_STARTUP_TIMEOUT    ((uint32_t)100)
#define HSI_VALUE    ((uint32_t)8000000)
#define LSI_VALUE    ((uint32_t)40000)
#define LSE_VALUE    ((uint32_t)32768)

#define VDD_VALUE                    3300U
#define TICK_INT_PRIORITY            ((uint32_t)0x000FU)
#define USE_RTOS                     0U
#define PREFETCH_ENABLE              1U

#include "stm32f1xx_hal_gpio.h"
#include "stm32f1xx_hal_rcc.h"
#include "stm32f1xx_hal_flash.h"
#include "stm32f1xx_hal_pwr.h"
#include "stm32f1xx_hal_cortex.h"
#include "stm32f1xx_hal_uart.h"
#include "stm32f1xx_hal_spi.h"
#include "stm32f1xx_hal_dma.h"

#define assert_param(expr) ((void)0U)

#endif
