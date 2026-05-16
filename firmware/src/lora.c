
#include "mesh.h"
#include "debug.h"
#include "stm32f1xx_hal.h"

SPI_HandleTypeDef hspi1;

#define LORA_SS_PIN    GPIO_PIN_4
#define LORA_SS_PORT   GPIOA
#define LORA_RST_PIN   GPIO_PIN_0
#define LORA_RST_PORT  GPIOB

// SX1278 Registers
#define REG_FIFO                 0x00
#define REG_OP_MODE              0x01
#define REG_FRF_MSB              0x06
#define REG_FRF_MID              0x07
#define REG_FRF_LSB              0x08
#define REG_PA_CONFIG            0x09
#define REG_FIFO_ADDR_PTR        0x0D
#define REG_FIFO_TX_BASE_ADDR    0x0E
#define REG_FIFO_RX_BASE_ADDR    0x0F
#define REG_FIFO_RX_CURRENT_ADDR 0x10
#define REG_IRQ_FLAGS            0x12
#define REG_RX_NB_BYTES          0x13
#define REG_PKT_RSSI_VALUE       0x1A
#define REG_MODEM_CONFIG_1       0x1D
#define REG_MODEM_CONFIG_2       0x1E
#define REG_PREAMBLE_MSB         0x20
#define REG_PREAMBLE_LSB         0x21
#define REG_PAYLOAD_LENGTH       0x22
#define REG_MODEM_CONFIG_3       0x26
#define REG_RSSI_WIDEBAND        0x2C
#define REG_DETECTION_OPTIMIZE   0x31
#define REG_DETECTION_THRESHOLD  0x37
#define REG_SYNC_WORD            0x39
#define REG_DIO_MAPPING_1        0x40
#define REG_VERSION              0x42

// Modes
#define MODE_LONG_RANGE_MODE     0x80
#define MODE_SLEEP               0x00
#define MODE_STDBY               0x01
#define MODE_TX                  0x03
#define MODE_RX_CONTINUOUS       0x05

void lora_write_reg(uint8_t addr, uint8_t val) {
    HAL_GPIO_WritePin(LORA_SS_PORT, LORA_SS_PIN, GPIO_PIN_RESET);
    uint8_t tx[2] = { addr | 0x80, val };
    HAL_SPI_Transmit(&hspi1, tx, 2, 10);
    HAL_GPIO_WritePin(LORA_SS_PORT, LORA_SS_PIN, GPIO_PIN_SET);
}

uint8_t lora_read_reg(uint8_t addr) {
    HAL_GPIO_WritePin(LORA_SS_PORT, LORA_SS_PIN, GPIO_PIN_RESET);
    uint8_t tx = addr & 0x7F;
    uint8_t rx = 0;
    HAL_SPI_Transmit(&hspi1, &tx, 1, 10);
    HAL_SPI_Receive(&hspi1, &rx, 1, 10);
    HAL_GPIO_WritePin(LORA_SS_PORT, LORA_SS_PIN, GPIO_PIN_SET);
    return rx;
}

void lora_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    // SPI Pins: PA5 (SCK), PA6 (MISO), PA7 (MOSI)
    GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // NSS Pin: PA4
    GPIO_InitStruct.Pin = LORA_SS_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(LORA_SS_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(LORA_SS_PORT, LORA_SS_PIN, GPIO_PIN_SET);

    // Reset Pin: PB0
    GPIO_InitStruct.Pin = LORA_RST_PIN;
    HAL_GPIO_Init(LORA_RST_PORT, &GPIO_InitStruct);

    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    HAL_SPI_Init(&hspi1);

    // Reset LoRa
    HAL_GPIO_WritePin(LORA_RST_PORT, LORA_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(LORA_RST_PORT, LORA_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(10);

    uint8_t version = lora_read_reg(REG_VERSION);
    debug_puts("[LORA] Version: 0x");
    debug_puti(version);
    debug_puts("\n");

    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP);
    
    // Set frequency to 433 MHz (433 / 32 * 2^19 = 7094272 = 0x6C4000)
    lora_write_reg(REG_FRF_MSB, 0x6C);
    lora_write_reg(REG_FRF_MID, 0x80);
    lora_write_reg(REG_FRF_LSB, 0x00);

    lora_write_reg(REG_PA_CONFIG, 0xCF); // 20dBm
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
}

void lora_send_packet(mesh_packet_t *pkt) {
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
    lora_write_reg(REG_FIFO_ADDR_PTR, 0);
    
    uint8_t *ptr = (uint8_t *)pkt;
    for (int i = 0; i < sizeof(mesh_packet_t); i++) {
        lora_write_reg(REG_FIFO, ptr[i]);
    }
    lora_write_reg(REG_PAYLOAD_LENGTH, sizeof(mesh_packet_t));
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_TX);
    
    while (!(lora_read_reg(REG_IRQ_FLAGS) & 0x08)); // Wait for TxDone
    lora_write_reg(REG_IRQ_FLAGS, 0x08); // Clear
    
    debug_puts("[LORA] Packet sent\n");
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS);
}

int lora_check_receive(mesh_packet_t *pkt) {
    uint8_t irq = lora_read_reg(REG_IRQ_FLAGS);
    if (!(irq & 0x40)) return 0; // RxDone?

    lora_write_reg(REG_IRQ_FLAGS, 0x40); // Clear

    uint8_t len = lora_read_reg(REG_RX_NB_BYTES);
    if (len != sizeof(mesh_packet_t)) {
        debug_puts("[LORA] Invalid packet size: ");
        debug_puti(len);
        debug_puts("\n");
        return 0;
    }

    lora_write_reg(REG_FIFO_ADDR_PTR, lora_read_reg(REG_FIFO_RX_CURRENT_ADDR));
    uint8_t *ptr = (uint8_t *)pkt;
    for (int i = 0; i < sizeof(mesh_packet_t); i++) {
        ptr[i] = lora_read_reg(REG_FIFO);
    }

    debug_puts("[LORA] Packet received!\n");
    return 1;
}
