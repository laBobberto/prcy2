#ifndef USBD_CDC_IF_H
#define USBD_CDC_IF_H
#include <stdint.h>
static inline int VCP_read(uint8_t *buf, uint16_t len) { (void)buf; (void)len; return 0; }
static inline int CDC_Transmit_FS(uint8_t *Buf, uint16_t Len) { (void)Buf; (void)Len; return 0; }
#endif
