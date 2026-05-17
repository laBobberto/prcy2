
#include "usbd_cdc_if.h"

#define APP_RX_DATA_SIZE  256
#define APP_TX_DATA_SIZE  256

extern USBD_HandleTypeDef hUsbDeviceFS;

uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];
uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

/* Circular buffer for received data */
#define RX_BUFFER_SIZE 256
static uint8_t rx_ring_buf[RX_BUFFER_SIZE];
static uint32_t rx_head = 0;
static uint32_t rx_tail = 0;

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length);
int8_t CDC_Receive_FS(uint8_t* pbuf, uint32_t *Len);

USBD_CDC_ItfTypeDef USBD_Interface_fopsFS = {
  CDC_Init_FS,
  CDC_DeInit_FS,
  CDC_Control_FS,
  CDC_Receive_FS
};

static int8_t CDC_Init_FS(void) {
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS); // Start receiving
  return (USBD_OK);
}

static int8_t CDC_DeInit_FS(void) {
  return (USBD_OK);
}

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length) {
  return (USBD_OK);
}

int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len) {
  for (uint32_t i = 0; i < *Len; i++) {
    uint32_t next = (rx_head + 1) % RX_BUFFER_SIZE;
    if (next != rx_tail) {
      rx_ring_buf[rx_head] = Buf[i];
      rx_head = next;
    }
  }
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return (USBD_OK);
}

uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len) {
  uint8_t result = USBD_OK;
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;
  if (hcdc->TxState != 0) return USBD_BUSY;
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
  result = USBD_CDC_TransmitPacket(&hUsbDeviceFS);
  return result;
}

/* API for main.c to read from USB */
int VCP_read(uint8_t* buf, uint16_t len) {
  int count = 0;
  while (count < len && rx_tail != rx_head) {
    buf[count++] = rx_ring_buf[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUFFER_SIZE;
  }
  return count;
}
