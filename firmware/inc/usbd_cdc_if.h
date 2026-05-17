
#ifndef __USBD_CDC_IF_H__
#define __USBD_CDC_IF_H__

#include "usbd_cdc.h"

extern USBD_CDC_ItfTypeDef USBD_Interface_fopsFS;

uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len);
int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len);
int VCP_read(uint8_t* buf, uint16_t len);

#endif /* __USBD_CDC_IF_H__ */
