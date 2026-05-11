
#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>

void debug_init(void);
void debug_putc(char c);
void debug_puts(const char *s);
void debug_puti(uint32_t n);

#endif
