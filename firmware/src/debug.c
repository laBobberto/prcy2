
#include "debug.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>
#include <string.h>

#ifdef USE_USB_CDC
#include "usbd_cdc_if.h"
#endif

UART_HandleTypeDef huart1;

void debug_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // USB Re-enumeration
    GPIO_InitStruct.Pin = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, GPIO_PIN_RESET);
    HAL_Delay(100);

    // UART1 fallback
    __HAL_RCC_USART1_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.Mode = UART_MODE_TX_RX;
    HAL_UART_Init(&huart1);
}

#ifdef USE_USB_CDC
static uint8_t usb_buf[256];
extern USBD_HandleTypeDef hUsbDeviceFS;

// Buffered CDC output — avoids per-char USB overhead
static uint8_t cdc_tx_buf[64];
static uint8_t cdc_tx_len = 0;

static void cdc_flush(void) {
    if (cdc_tx_len == 0) return;
    if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) {
        USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;
        int timeout = 500;
        while (hcdc->TxState != 0 && timeout--) { __NOP(); }
        memcpy(usb_buf, cdc_tx_buf, cdc_tx_len);
        CDC_Transmit_FS(usb_buf, cdc_tx_len);
    }
    cdc_tx_len = 0;
}
#endif

void debug_putc(char c) {
    HAL_UART_Transmit(&huart1, (uint8_t *)&c, 1, 10);
#ifdef USE_USB_CDC
    cdc_tx_buf[cdc_tx_len++] = (uint8_t)c;
    if (cdc_tx_len >= sizeof(cdc_tx_buf) || c == '\n') {
        cdc_flush();
    }
#endif
}

void debug_puts(const char *s) {
    if (!s) return;
    int len = strlen(s);
    HAL_UART_Transmit(&huart1, (uint8_t *)s, len, 100);
#ifdef USE_USB_CDC
    // Flush any pending putc buffer first
    cdc_flush();
    if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) {
        USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;
        int timeout = 1000;
        while (hcdc->TxState != 0 && timeout--) { __NOP(); }

        int send_len = len > 255 ? 255 : len;
        memcpy(usb_buf, s, send_len);
        CDC_Transmit_FS(usb_buf, send_len);
    }
#endif
}

void debug_puti(uint32_t n) {
    char buf[12];
    sprintf(buf, "%lu", (unsigned long)n);
    debug_puts(buf);
}

void debug_puti_signed(int32_t n) {
    char buf[14];
    sprintf(buf, "%ld", (long)n);
    debug_puts(buf);
}
