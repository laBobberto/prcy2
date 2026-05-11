
#include <stdint.h>
#include "mesh.h"
#include "debug.h"

// Регистры USART3 (будет нашим виртуальным LoRa)
#define USART3_SR    (*(volatile uint32_t *)0x40004800)
#define USART3_DR    (*(volatile uint32_t *)0x40004804)
#define USART3_BRR   (*(volatile uint32_t *)0x40004808)
#define USART3_CR1   (*(volatile uint32_t *)0x4000480C)
#define RCC_APB1ENR  (*(volatile uint32_t *)0x40023840)

void lora_init(void) {
    RCC_APB1ENR |= (1 << 18); // Тактирование USART3
    USART3_BRR = 0x1112; 
    USART3_CR1 = (1 << 13) | (1 << 3) | (1 << 2); // UE, TE, RE
}

void lora_send_packet(mesh_packet_t *pkt) {
    uint8_t *ptr = (uint8_t *)pkt;
    for (int i = 0; i < sizeof(mesh_packet_t); i++) {
        while (!(USART3_SR & (1 << 7))); // Ждем TXE
        USART3_DR = ptr[i];
    }
    debug_puts("[V-RADIO] Packet sent to air hub\n");
}

int lora_check_receive(mesh_packet_t *pkt) {
    // Если в буфере USART3 есть данные
    if (!(USART3_SR & (1 << 5))) return 0;

    uint8_t *ptr = (uint8_t *)pkt;
    for (int i = 0; i < sizeof(mesh_packet_t); i++) {
        while (!(USART3_SR & (1 << 5))); // Ждем RXNE
        ptr[i] = (uint8_t)(USART3_DR & 0xFF);
    }
    debug_puts("[V-RADIO] Packet captured from hub!\n");
    return 1;
}
