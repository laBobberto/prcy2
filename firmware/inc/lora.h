#ifndef LORA_H
#define LORA_H

#include "mesh.h"

void lora_init(void);
void lora_send_packet(mesh_packet_t *pkt);
int lora_check_receive(mesh_packet_t *pkt);

#endif
