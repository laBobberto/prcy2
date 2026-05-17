#ifndef LORA_H
#define LORA_H

#include "mesh.h"

void lora_init(void);
void lora_send_packet(mesh_packet_t *pkt);
int lora_check_receive(mesh_packet_t *pkt);
void lora_sleep(void);
void lora_wakeup(void);
uint16_t lora_read_battery(void);

#endif
