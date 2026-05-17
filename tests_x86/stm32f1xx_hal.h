// Mock STM32 HAL for x86 testing
#ifndef STM32F1XX_HAL_H
#define STM32F1XX_HAL_H

#include <stdint.h>
#include <stddef.h>

typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef struct { int dummy; } GPIO_TypeDef;
typedef struct { int dummy; } SPI_TypeDef;
typedef struct { int dummy; } UART_TypeDef;
typedef struct { int dummy; } RCC_TypeDef;
typedef struct { int dummy; } FLASH_TypeDef;

typedef struct {
    SPI_TypeDef *Instance;
    struct {
        uint32_t Mode;
        uint32_t Direction;
        uint32_t DataSize;
        uint32_t CLKPolarity;
        uint32_t CLKPhase;
        uint32_t NSS;
        uint32_t BaudRatePrescaler;
        uint32_t FirstBit;
    } Init;
} SPI_HandleTypeDef;

typedef struct {
    UART_TypeDef *Instance;
} UART_HandleTypeDef;

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
} GPIO_InitTypeDef;

typedef struct {
    uint32_t OscillatorType;
    uint32_t HSEState;
    uint32_t HSEPredivValue;
    struct {
        uint32_t PLLState;
        uint32_t PLLSource;
        uint32_t PLLMUL;
    } PLL;
} RCC_OscInitTypeDef;

typedef struct {
    uint32_t ClockType;
    uint32_t SYSCLKSource;
    uint32_t AHBCLKDivider;
    uint32_t APB1CLKDivider;
    uint32_t APB2CLKDivider;
} RCC_ClkInitTypeDef;

typedef struct {
    uint32_t PeriphClockSelection;
} RCC_PeriphCLKInitTypeDef;

typedef struct {
    uint32_t TypeErase;
    uint32_t PageAddress;
    uint32_t NbPages;
} FLASH_EraseInitTypeDef;

#define HAL_OK 0
#define HAL_BUSY 1

// RCC registers mock
extern RCC_TypeDef RCC_mock;
#define RCC (&RCC_mock)
#define RCC_CSR_PINRSTF 0
#define RCC_CSR_PORRSTF 0
#define RCC_CSR_SFTRSTF 0
#define RCC_CSR_IWDGRSTF 0
#define RCC_CSR_WWDGRSTF 0
#define RCC_CSR_LPWRRSTF 0
#define RCC_CSR_RMVF 0

// Function stubs
static inline void HAL_Init(void) {}
static inline void HAL_Delay(uint32_t d) { (void)d; }
static inline void HAL_IncTick(void) {}
static inline uint32_t HAL_GetTick(void) { return 0; }
static inline int HAL_RCC_OscConfig(void *o) { (void)o; return 0; }
static inline int HAL_RCC_ClockConfig(void *c, uint32_t f) { (void)c; (void)f; return 0; }
static inline void HAL_RCCEx_PeriphCLKConfig(void *c) { (void)c; }
static inline void __HAL_RCC_GPIOA_CLK_ENABLE(void) {}
static inline void __HAL_RCC_GPIOB_CLK_ENABLE(void) {}
static inline void __HAL_RCC_GPIOC_CLK_ENABLE(void) {}
static inline void __HAL_RCC_SPI1_CLK_ENABLE(void) {}
static inline void __HAL_RCC_USB_CLK_ENABLE(void) {}
static inline void __HAL_RCC_AFIO_CLK_ENABLE(void) {}

static inline void HAL_GPIO_Init(GPIO_TypeDef *g, GPIO_InitTypeDef *i) { (void)g; (void)i; }
static inline GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *g, uint32_t p) { (void)g; (void)p; return GPIO_PIN_RESET; }
static inline void HAL_GPIO_WritePin(GPIO_TypeDef *g, uint32_t p, GPIO_PinState s) { (void)g; (void)p; (void)s; }
static inline void HAL_GPIO_TogglePin(GPIO_TypeDef *g, uint32_t p) { (void)g; (void)p; }

static inline int HAL_SPI_Init(SPI_HandleTypeDef *h) { (void)h; return 0; }
static inline int HAL_SPI_Transmit(SPI_HandleTypeDef *h, uint8_t *d, uint16_t s, uint32_t t) { (void)h; (void)d; (void)s; (void)t; return 0; }
static inline int HAL_SPI_Receive(SPI_HandleTypeDef *h, uint8_t *d, uint16_t s, uint32_t t) { (void)h; (void)d; (void)s; (void)t; return 0; }

static inline int HAL_UART_Transmit(UART_HandleTypeDef *h, uint8_t *d, uint16_t s, uint32_t t) { (void)h; (void)d; (void)s; (void)t; return 0; }
static inline int HAL_UART_Receive(UART_HandleTypeDef *h, uint8_t *d, uint16_t s, uint32_t t) { (void)h; (void)d; (void)s; (void)t; return 1; }

static inline int HAL_FLASH_Unlock(void) { return 0; }
static inline int HAL_FLASH_Lock(void) { return 0; }
static inline int HAL_FLASHEx_Erase(void *e, uint32_t *err) { (void)e; (void)err; return 0; }
static inline int HAL_FLASH_Program(uint32_t t, uint32_t a, uint16_t d) { (void)t; (void)a; (void)d; return 0; }

#define FLASH_TYPEERASE_PAGES 0
#define FLASH_TYPEPROGRAM_HALFWORD 0
#define FLASH_LATENCY_2 0
#define GPIO_MODE_OUTPUT_PP 0
#define GPIO_MODE_AF_PP 0
#define GPIO_MODE_INPUT 0
#define GPIO_NOPULL 0
#define GPIO_PULLDOWN 0
#define GPIO_SPEED_FREQ_LOW 0
#define GPIO_SPEED_FREQ_HIGH 0
#define SPI_MODE_MASTER 0
#define SPI_DIRECTION_2LINES 0
#define SPI_DATASIZE_8BIT 0
#define SPI_POLARITY_LOW 0
#define SPI_PHASE_1EDGE 0
#define SPI_NSS_SOFT 0
#define SPI_BAUDRATEPRESCALER_16 0
#define SPI_FIRSTBIT_MSB 0
#define RCC_OSCILLATORTYPE_HSE 0
#define RCC_HSE_ON 0
#define RCC_HSE_PREDIV_DIV1 0
#define RCC_PLL_ON 0
#define RCC_PLLSOURCE_HSE 0
#define RCC_PLL_MUL9 0
#define RCC_SYSCLKSOURCE_PLLCLK 0
#define RCC_CLOCKTYPE_HCLK 0
#define RCC_CLOCKTYPE_SYSCLK 0
#define RCC_CLOCKTYPE_PCLK1 0
#define RCC_CLOCKTYPE_PCLK2 0
#define RCC_SYSCLK_DIV1 0
#define RCC_HCLK_DIV2 0
#define RCC_HCLK_DIV1 0

// SysTick mock
#define SysTick 0

// Mock MCU UID for x86 testing (avoid segfault on STM32 UID address)
static uint8_t mock_uid_data[12] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};
#define MCU_UID_BASE ((uint32_t)(uintptr_t)mock_uid_data)

#endif
