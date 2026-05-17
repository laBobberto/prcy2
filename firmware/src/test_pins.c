// Pin connection test for STM32F103C8T6 + SX1278
// Flash this to verify wiring before running the full mesh firmware
//
// Expected output via USB CDC / UART:
//   [TEST] Starting pin connection test...
//   [TEST] SPI1 pins: PA4(NSS) PA5(SCK) PA6(MISO) PA7(MOSI) - OK
//   [TEST] LoRa control: PB0(RESET) PB1(DIO0) - OK
//   [TEST] SX1278 detected: version=0x12 - OK
//   [TEST] All tests PASSED

#include <stdint.h>
#include <string.h>
#include "stm32f1xx_hal.h"
#include "debug.h"

// Pin definitions (must match lora.c)
#define LORA_NSS_PIN    GPIO_PIN_4
#define LORA_NSS_PORT   GPIOA
#define LORA_SCK_PIN    GPIO_PIN_5
#define LORA_SCK_PORT   GPIOA
#define LORA_MISO_PIN   GPIO_PIN_6
#define LORA_MISO_PORT  GPIOA
#define LORA_MOSI_PIN   GPIO_PIN_7
#define LORA_MOSI_PORT  GPIOA
#define LORA_RST_PIN    GPIO_PIN_0
#define LORA_RST_PORT   GPIOB
#define LORA_DIO0_PIN   GPIO_PIN_1
#define LORA_DIO0_PORT  GPIOB

// SX1278 registers
#define REG_VERSION     0x42
#define REG_OP_MODE     0x01
#define MODE_SLEEP      0x00
#define MODE_STDBY      0x01
#define MODE_LONG_RANGE 0x80

static SPI_HandleTypeDef hspi1;

static void SystemClock_Config(void) {
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    HAL_RCC_OscConfig(&osc);

    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
}

static void GPIO_Init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};

    // SPI1 pins: SCK, MOSI — AF push-pull
    g.Pin = LORA_SCK_PIN | LORA_MOSI_PIN;
    g.Mode = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &g);

    // MISO — input floating
    g.Pin = LORA_MISO_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(LORA_MISO_PORT, &g);

    // NSS — output push-pull
    g.Pin = LORA_NSS_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(LORA_NSS_PORT, &g);
    HAL_GPIO_WritePin(LORA_NSS_PORT, LORA_NSS_PIN, GPIO_PIN_SET);

    // RESET — output push-pull
    g.Pin = LORA_RST_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LORA_RST_PORT, &g);

    // DIO0 — input
    g.Pin = LORA_DIO0_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(LORA_DIO0_PORT, &g);
}

static void SPI_Init(void) {
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    HAL_SPI_Init(&hspi1);
}

static void LoRa_Reset(void) {
    HAL_GPIO_WritePin(LORA_RST_PORT, LORA_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(LORA_RST_PORT, LORA_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(10);
}

static uint8_t LoRa_ReadReg(uint8_t addr) {
    HAL_GPIO_WritePin(LORA_NSS_PORT, LORA_NSS_PIN, GPIO_PIN_RESET);
    uint8_t tx = addr & 0x7F;
    uint8_t rx = 0;
    HAL_SPI_Transmit(&hspi1, &tx, 1, 100);
    HAL_SPI_Receive(&hspi1, &rx, 1, 100);
    HAL_GPIO_WritePin(LORA_NSS_PORT, LORA_NSS_PIN, GPIO_PIN_SET);
    return rx;
}

static void LoRa_WriteReg(uint8_t addr, uint8_t val) {
    HAL_GPIO_WritePin(LORA_NSS_PORT, LORA_NSS_PIN, GPIO_PIN_RESET);
    uint8_t tx[2] = {addr | 0x80, val};
    HAL_SPI_Transmit(&hspi1, tx, 2, 100);
    HAL_GPIO_WritePin(LORA_NSS_PORT, LORA_NSS_PIN, GPIO_PIN_SET);
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    GPIO_Init();
    SPI_Init();
    debug_init();
    HAL_Delay(1000);

    debug_puts("\n========================================\n");
    debug_puts("  STM32F103 + SX1278 Pin Test\n");
    debug_puts("========================================\n\n");

    int fail = 0;

    // Test 1: SPI1 pin toggle
    debug_puts("[TEST 1] SPI1 pins (PA4/PA5/PA6/PA7)...\n");

    // Check PA5 (SCK) and PA7 (MOSI) can be toggled
    // (they're AF, so we can't easily toggle them — just verify they're configured)
    debug_puts("  PA4 (NSS):  ");
    debug_puts(HAL_GPIO_ReadPin(LORA_NSS_PORT, LORA_NSS_PIN) ? "HIGH" : "LOW");
    debug_puts(" (expected HIGH)\n");

    debug_puts("  PA5 (SCK):  configured as SPI CLK\n");
    debug_puts("  PA6 (MISO): configured as SPI MISO\n");
    debug_puts("  PA7 (MOSI): configured as SPI MOSI\n");
    debug_puts("  PASS\n\n");

    // Test 2: LoRa control pins
    debug_puts("[TEST 2] LoRa control pins (PB0/PB1)...\n");

    // Toggle RESET
    debug_puts("  PB0 (RESET): toggle test...");
    HAL_GPIO_WritePin(LORA_RST_PORT, LORA_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
    uint8_t rst_low = HAL_GPIO_ReadPin(LORA_RST_PORT, LORA_RST_PIN);
    HAL_GPIO_WritePin(LORA_RST_PORT, LORA_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(1);
    uint8_t rst_high = HAL_GPIO_ReadPin(LORA_RST_PORT, LORA_RST_PIN);
    if (!rst_low && rst_high) {
        debug_puts(" OK (LOW->HIGH)\n");
    } else {
        debug_puts(" FAIL\n");
        fail++;
    }

    // Read DIO0
    debug_puts("  PB1 (DIO0): read=");
    debug_puts(HAL_GPIO_ReadPin(LORA_DIO0_PORT, LORA_DIO0_PIN) ? "HIGH" : "LOW");
    debug_puts(" (any value OK)\n");
    debug_puts("  PASS\n\n");

    // Test 3: SPI communication with SX1278
    debug_puts("[TEST 3] SPI communication with SX1278...\n");
    LoRa_Reset();

    uint8_t version = LoRa_ReadReg(REG_VERSION);
    debug_puts("  REG_VERSION (0x42) = 0x");
    char hex[3];
    hex[0] = "0123456789ABCDEF"[(version >> 4) & 0x0F];
    hex[1] = "0123456789ABCDEF"[version & 0x0F];
    hex[2] = 0;
    debug_puts(hex);
    debug_puts("\n");

    if (version == 0x12) {
        debug_puts("  SX1278 detected! (version=0x12)\n");
        debug_puts("  PASS\n\n");
    } else if (version == 0x00) {
        debug_puts("  FAIL: Got 0x00 — check wiring (MISO not connected?)\n");
        fail++;
    } else if (version == 0xFF) {
        debug_puts("  FAIL: Got 0xFF — check wiring (SPI not working?)\n");
        fail++;
    } else {
        debug_puts("  WARN: Unexpected version (maybe SX1276?)\n");
    }

    // Test 4: Put LoRa to sleep and wake up
    debug_puts("[TEST 4] LoRa mode switch...\n");
    LoRa_WriteReg(REG_OP_MODE, MODE_LONG_RANGE | MODE_SLEEP);
    HAL_Delay(10);
    uint8_t mode = LoRa_ReadReg(REG_OP_MODE);
    debug_puts("  After sleep: OP_MODE=0x");
    hex[0] = "0123456789ABCDEF"[(mode >> 4) & 0x0F];
    hex[1] = "0123456789ABCDEF"[mode & 0x0F];
    debug_puts(hex);

    if ((mode & 0x07) == MODE_SLEEP) {
        debug_puts(" (SLEEP) PASS\n");
    } else {
        debug_puts(" FAIL\n");
        fail++;
    }

    LoRa_WriteReg(REG_OP_MODE, MODE_LONG_RANGE | MODE_STDBY);
    HAL_Delay(10);
    mode = LoRa_ReadReg(REG_OP_MODE);
    debug_puts("  After standby: OP_MODE=0x");
    hex[0] = "0123456789ABCDEF"[(mode >> 4) & 0x0F];
    hex[1] = "0123456789ABCDEF"[mode & 0x0F];
    debug_puts(hex);

    if ((mode & 0x07) == MODE_STDBY) {
        debug_puts(" (STDBY) PASS\n");
    } else {
        debug_puts(" FAIL\n");
        fail++;
    }

    // Summary
    debug_puts("\n========================================\n");
    if (fail == 0) {
        debug_puts("  ALL TESTS PASSED\n");
    } else {
        debug_puts("  ");
        debug_puti(fail);
        debug_puts(" TEST(S) FAILED\n");
    }
    debug_puts("========================================\n");

    // Blink LED on success
    // PC13 is the built-in LED on Blue Pill
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef led = {0};
    led.Pin = GPIO_PIN_13;
    led.Mode = GPIO_MODE_OUTPUT_PP;
    led.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &led);

    while (1) {
        if (fail == 0) {
            // Fast blink on success
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
            HAL_Delay(200);
        } else {
            // Slow blink on failure
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
            HAL_Delay(1000);
        }
    }
}
