#include "x25519.h"
#include "c25519.h"
#include "f25519.h"
#include <string.h>

void x25519(uint8_t *out, const uint8_t *scalar, const uint8_t *point) {
    uint8_t s[32];
    memcpy(s, scalar, 32);
    c25519_prepare(s);
    c25519_smult(out, point, s);
}

void x25519_base(uint8_t *out, const uint8_t *scalar) {
    uint8_t s[32];
    memcpy(s, scalar, 32);
    c25519_prepare(s);
    c25519_smult(out, c25519_base_x, s);
}
