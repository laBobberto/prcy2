
#include <stdint.h>
#include <string.h>
#include "mesh.h"
#include "debug.h"

extern uint32_t _etext, _sdata, _edata, _sbss, _ebss;

// Регистры UART2 (для связи с ПК)
#define USART2_SR    (*(volatile uint32_t *)0x40004400)
#define USART2_DR    (*(volatile uint32_t *)0x40004404)

void lora_init(void);
void lora_send_packet(mesh_packet_t *pkt);
int lora_check_receive(mesh_packet_t *pkt);
void mesh_tick(void);

uint32_t get_node_id_from_reg(void) {
    uint32_t id;
    __asm volatile ("mov %0, r11" : "=r" (id));
    return id;
}

void Reset_Handler(void) {
    uint32_t *src = &_etext;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    dst = &_sbss;
    while (dst < &_ebss) *dst++ = 0;
    debug_init();
    lora_init();
    extern int main(void);
    main();
    while(1);
}

__attribute__((section(".isr_vector")))
const void *isr_vector[] = { (void *)0x20020000, Reset_Handler };

// Универсальный обработчик команд для ЛЮБОГО узла
void handle_user_command(char *buf, uint8_t self_id) {
    if (buf[0] == 's') { // Формат: s <dst> <msg>
        int dst = buf[2] - '0';
        char *msg = &buf[4];
        debug_puts("\n[LOCAL] Node ");
        debug_puti(self_id);
        debug_puts(" is initiating send to ");
        debug_puti(dst);
        debug_puts("...\n");
        mesh_send_data((uint8_t)dst, (uint8_t*)msg, strlen(msg));
    }
}

int main(void) {
    for(volatile int i=0; i<1000000; i++); 
    uint8_t node_id = (uint8_t)get_node_id_from_reg();
    if (node_id == 0) node_id = 1; 
    
    mesh_init(node_id);
    debug_puts("\n--- NODE ");
    debug_puti(node_id);
    debug_puts(" READY (Client + Router) ---\n");

    mesh_packet_t rx_pkt;
    char cmd_buf[64];
    int cmd_idx = 0;

    while(1) {
        mesh_tick();

        // 1. Прием из радио-эфира (Роль Роутера)
        if (lora_check_receive(&rx_pkt)) {
            if (rx_pkt.src_id != node_id) mesh_process_packet(&rx_pkt);
        }

        // 2. Прием от пользователя (Роль Клиента)
        if (USART2_SR & (1 << 5)) {
            char c = (char)(USART2_DR & 0xFF);
            if (c == '\n' || c == '\r') {
                cmd_buf[cmd_idx] = '\0';
                handle_user_command(cmd_buf, node_id);
                cmd_idx = 0;
            } else if (cmd_idx < 60) {
                cmd_buf[cmd_idx++] = c;
            }
        }
        for(volatile int i=0; i<10; i++); 
    }
    return 0;
}
