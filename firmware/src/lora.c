
#include "mesh.h"
#include "lora.h"
#include "debug.h"
#include "stm32f1xx_hal.h"

SPI_HandleTypeDef hspi1;

#define LORA_SS_PIN    GPIO_PIN_4
#define LORA_SS_PORT   GPIOA
#define LORA_RST_PIN   GPIO_PIN_0
#define LORA_RST_PORT  GPIOB

// LoRa radio parameters
#define LORA_FREQUENCY_HZ   433000000
#define LORA_SF             7        // Spreading Factor 7 (fastest, ~1km range)
#define LORA_BW_KHZ         125      // Bandwidth 125 kHz
#define LORA_CR             5        // Coding Rate 4/5
#define LORA_TX_POWER_DBM   20       // Max power for XL1278-SMT
#define LORA_SYNC_WORD      0x12     // LoRaWAN public network

// SX1278 Registers
#define REG_FIFO                 0x00
#define REG_OP_MODE              0x01
#define REG_FRF_MSB              0x06
#define REG_FRF_MID              0x07
#define REG_FRF_LSB              0x08
#define REG_PA_CONFIG            0x09
#define REG_PA_RAMP              0x0A
#define REG_FIFO_ADDR_PTR        0x0D
#define REG_FIFO_TX_BASE_ADDR    0x0E
#define REG_FIFO_RX_BASE_ADDR    0x0F
#define REG_FIFO_RX_CURRENT_ADDR 0x10
#define REG_IRQ_FLAGS_MASK       0x11
#define REG_IRQ_FLAGS            0x12
#define REG_RX_NB_BYTES          0x13
#define REG_PKT_SNR_VALUE        0x19
#define REG_PKT_RSSI_VALUE       0x1A
#define REG_MODEM_CONFIG_1       0x1D
#define REG_MODEM_CONFIG_2       0x1E
#define REG_SYMB_TIMEOUT_LSB     0x1F
#define REG_PREAMBLE_MSB         0x20
#define REG_PREAMBLE_LSB         0x21
#define REG_PAYLOAD_LENGTH       0x22
#define REG_MAX_PAYLOAD_LENGTH   0x23
#define REG_MODEM_CONFIG_3       0x26
#define REG_RSSI_WIDEBAND        0x2C
#define REG_DETECTION_OPTIMIZE   0x31
#define REG_INVERTIQ             0x33
#define REG_DETECTION_THRESHOLD  0x37
#define REG_SYNC_WORD            0x39
#define REG_INVERTIQ2            0x3B
#define REG_DIO_MAPPING_1        0x40
#define REG_DIO_MAPPING_2        0x41
#define REG_VERSION              0x42

// Modes
#define MODE_LONG_RANGE_MODE     0x80
#define MODE_SLEEP               0x00
#define MODE_STDBY               0x01
#define MODE_FSTX                0x02
#define MODE_TX                  0x03
#define MODE_FSRX                0x04
#define MODE_RX_CONTINUOUS       0x05
#define MODE_RX_SINGLE           0x06
#define MODE_CAD                 0x07

// IRQ flags
#define IRQ_TX_DONE              0x08
#define IRQ_RX_DONE              0x40
#define IRQ_RX_TIMEOUT           0x80
#define IRQ_CRC_ERROR            0x20
#define IRQ_CAD_DONE             0x04
#define IRQ_CAD_DETECTED         0x01

// Max packet size for LoRa
#define LORA_MAX_PACKET_LEN      255

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

    // Set frequency: FRF = freq_hz * 2^19 / 32MHz
    uint32_t frf = ((uint64_t)LORA_FREQUENCY_HZ << 19) / 32000000UL;
    lora_write_reg(REG_FRF_MSB, (frf >> 16) & 0xFF);
    lora_write_reg(REG_FRF_MID, (frf >> 8) & 0xFF);
    lora_write_reg(REG_FRF_LSB, frf & 0xFF);

    // TX power: PA_BOOST, MaxPower=0x7 (15dBm base), OutputPower=0xF (+5dBm = 20dBm)
    lora_write_reg(REG_PA_CONFIG, 0x8F | 0x70); // PA_BOOST, MaxPower=7, OutPower=15
    lora_write_reg(REG_PA_RAMP, 0x09); // 40us ramp time

    // Modem config: BW=125kHz, CR=4/5, explicit header
    // BW[7:4]=0111(125k), CR[3:1]=010(4/5), ImplicitHeader=0
    uint8_t bw = 0x70; // 125 kHz
    uint8_t cr = ((LORA_CR - 4) << 1); // CR 4/5=0, 4/6=2, 4/7=4, 4/8=6
    lora_write_reg(REG_MODEM_CONFIG_1, bw | cr);

    // SF=7, TXContinuous=0, RxPayloadCrcOn=1, SymbTimeout[9:8]=0
    uint8_t sf = (LORA_SF << 4);
    lora_write_reg(REG_MODEM_CONFIG_2, sf | 0x04); // CRC on

    // LowDataRateOptimize=0 (SF7/125k doesn't need it), AgcAutoOn=1
    lora_write_reg(REG_MODEM_CONFIG_3, 0x04);

    // Preamble length: 8 symbols (default for LoRa)
    lora_write_reg(REG_PREAMBLE_MSB, 0x00);
    lora_write_reg(REG_PREAMBLE_LSB, 0x08);

    // Sync word
    lora_write_reg(REG_SYNC_WORD, LORA_SYNC_WORD);

    // Max payload length
    lora_write_reg(REG_MAX_PAYLOAD_LENGTH, LORA_MAX_PACKET_LEN);

    // Set base addresses
    lora_write_reg(REG_FIFO_TX_BASE_ADDR, 0);
    lora_write_reg(REG_FIFO_RX_BASE_ADDR, 0);

    // DIO0 mapping: TxDone=00, RxDone=00 (default for LoRa)
    lora_write_reg(REG_DIO_MAPPING_1, 0x00);

    // Switch to standby, then start receiving
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS);
}

extern void led_blink(int times);

void lora_send_packet(mesh_packet_t *pkt) {
    led_blink(1);
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
    lora_write_reg(REG_FIFO_ADDR_PTR, 0);

    uint16_t pkt_size = sizeof(mesh_packet_t);
    if (pkt_size > LORA_MAX_PACKET_LEN) pkt_size = LORA_MAX_PACKET_LEN;

    uint8_t *ptr = (uint8_t *)pkt;
    for (int i = 0; i < pkt_size; i++) {
        lora_write_reg(REG_FIFO, ptr[i]);
    }
    lora_write_reg(REG_PAYLOAD_LENGTH, pkt_size);
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_TX);

    // Wait for TxDone IRQ
    while (!(lora_read_reg(REG_IRQ_FLAGS) & IRQ_TX_DONE));
    lora_write_reg(REG_IRQ_FLAGS, IRQ_TX_DONE);

    debug_puts("[LORA] TX done (");
    debug_puti(pkt_size);
    debug_puts(" bytes)\n");
    lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS);
}

int lora_check_receive(mesh_packet_t *pkt) {
    uint8_t irq = lora_read_reg(REG_IRQ_FLAGS);

    // Check for CRC error
    if (irq & IRQ_CRC_ERROR) {
        lora_write_reg(REG_IRQ_FLAGS, IRQ_CRC_ERROR);
        debug_puts("[LORA] CRC error\n");
        return 0;
    }

    // Check for RX timeout
    if (irq & IRQ_RX_TIMEOUT) {
        lora_write_reg(REG_IRQ_FLAGS, IRQ_RX_TIMEOUT);
        return 0;
    }

    if (!(irq & IRQ_RX_DONE)) return 0;

    lora_write_reg(REG_IRQ_FLAGS, IRQ_RX_DONE);

    uint8_t len = lora_read_reg(REG_RX_NB_BYTES);
    if (len != sizeof(mesh_packet_t)) {
        debug_puts("[LORA] RX size mismatch: ");
        debug_puti(len);
        debug_puts(" != ");
        debug_puti(sizeof(mesh_packet_t));
        debug_puts("\n");
        return 0;
    }

    lora_write_reg(REG_FIFO_ADDR_PTR, lora_read_reg(REG_FIFO_RX_CURRENT_ADDR));
    uint8_t *ptr = (uint8_t *)pkt;
    for (int i = 0; i < sizeof(mesh_packet_t); i++) {
        ptr[i] = lora_read_reg(REG_FIFO);
    }

    int8_t snr = (int8_t)lora_read_reg(REG_PKT_SNR_VALUE);
    int16_t rssi = (int16_t)lora_read_reg(REG_PKT_RSSI_VALUE) - 157 + (snr / 4);
    debug_puts("[LORA] RX OK RSSI=");
    debug_puti(rssi);
    debug_puts(" SNR=");
    debug_puti(snr);
    debug_puts("\n");

    return 1;
}
