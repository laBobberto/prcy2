
#ifndef __USBD_CONF_H__
#define __USBD_CONF_H__

#include "stm32f1xx_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USBD_MAX_NUM_INTERFACES     1
#define USBD_MAX_NUM_CONFIGURATION  1
#define USBD_MAX_STR_DESC_SIZ       512
#define USBD_SUPPORT_USER_STRING_DESC 1
#define USBD_SELF_POWERED           1

#define USBD_DEBUG_LEVEL            0

/* Memory management macros */
void *USBD_static_malloc(uint32_t size);
void  USBD_static_free(void *p);
#define USBD_malloc               USBD_static_malloc
#define USBD_free                 USBD_static_free
#define USBD_memset               memset
#define USBD_memcpy               memcpy

#endif /* __USBD_CONF_H__ */
