
#include "debug.h"

#define RCC_APB1ENR  (*(volatile uint32_t *)0x40023840)
#define USART2_SR    (*(volatile uint32_t *)0x40004400)
#define USART2_DR    (*(volatile uint32_t *)0x40004404)
#define USART2_BRR   (*(volatile uint32_t *)0x40004408)
#define USART2_CR1   (*(volatile uint32_t *)0x4000440C)

void debug_init(void) {
    RCC_APB1ENR |= (1 << 17);
    USART2_BRR = 0x1112; 
    USART2_CR1 = (1 << 13) | (1 << 3) | (1 << 2);
}

void debug_putc(char c) {
    while (!(USART2_SR & (1 << 7)));
    USART2_DR = c;
}

void debug_puts(const char *s) {
    while (*s) debug_putc(*s++);
}

void debug_puti(uint32_t n) {
    if (n == 0) { debug_putc('0'); return; }
    char buf[10];
    int i = 0;
    while (n > 0) { buf[i++] = (n % 10) + '0'; n /= 10; }
    while (i > 0) debug_putc(buf[--i]);
}
