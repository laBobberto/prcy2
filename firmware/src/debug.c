
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
#endif

void debug_putc(char c) {
    HAL_UART_Transmit(&huart1, (uint8_t *)&c, 1, 10);
    /* USB CDC: single-char transfers are too slow, rely on debug_puts for bulk */
}

void debug_puts(const char *s) {
    if (!s) return;
    int len = strlen(s);
    HAL_UART_Transmit(&huart1, (uint8_t *)s, len, 100);
#ifdef USE_USB_CDC
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
