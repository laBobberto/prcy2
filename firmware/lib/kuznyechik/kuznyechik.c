
#include "kuznyechik.h"
#include <string.h>

static const uint8_t SBOX[256] = {
    0xFC, 0xEE, 0xDD, 0x11, 0xCF, 0x6E, 0x31, 0x16, 0xFB, 0xC4, 0xFA, 0xDA, 0x23, 0xC5, 0x04, 0x4D,
    0xE9, 0x77, 0xF0, 0xDB, 0x93, 0x2E, 0x99, 0xBA, 0x17, 0x36, 0xF1, 0xBB, 0x14, 0xCD, 0x5F, 0xC1,
    0xF9, 0x18, 0x65, 0x5A, 0xE2, 0x5C, 0xEF, 0x21, 0x81, 0x1C, 0x3C, 0x42, 0x8B, 0x01, 0x8E, 0xAE,
    0x05, 0x84, 0x19, 0xCE, 0xAD, 0x08, 0xB1, 0x12, 0x10, 0x3B, 0xA4, 0x70, 0xF0, 0x08, 0xC2, 0x13,
    0x17, 0x8F, 0x64, 0x9B, 0x0E, 0x73, 0x39, 0x76, 0x0B, 0xD4, 0xB4, 0x51, 0x13, 0x02, 0x04, 0x18,
    0x61, 0x35, 0x12, 0x19, 0xE5, 0x6A, 0xA2, 0x17, 0x0A, 0x23, 0x4D, 0x52, 0xAD, 0x1B, 0x32, 0xA8,
    0x12, 0x2E, 0x2D, 0x18, 0x35, 0x19, 0x30, 0x44, 0x39, 0xC5, 0x38, 0x02, 0xA3, 0x05, 0x16, 0x1C,
    0xA9, 0x17, 0x04, 0x19, 0x32, 0x16, 0x1E, 0x12, 0x13, 0xA9, 0x1A, 0x0E, 0x12, 0x1D, 0x16, 0x1C,
    0x31, 0x4E, 0x52, 0x04, 0x69, 0x65, 0xB5, 0x0E, 0x7A, 0x33, 0x08, 0x2B, 0x0B, 0xD4, 0xB4, 0x51,
    0x17, 0x0A, 0x23, 0x4D, 0x52, 0xAD, 0x1B, 0x32, 0xA8, 0x12, 0x2E, 0x2D, 0x18, 0x35, 0x19, 0x30,
    0xE2, 0x5C, 0xEF, 0x21, 0x81, 0x1C, 0x3C, 0x42, 0x8B, 0x01, 0x8E, 0xAE, 0x05, 0x84, 0x19, 0xCE,
    0xAD, 0x08, 0xB1, 0x12, 0x10, 0x3B, 0xA4, 0x70, 0xF0, 0x08, 0xC2, 0x13, 0x17, 0x8F, 0x64, 0x9B,
    0xFC, 0xEE, 0xDD, 0x11, 0xCF, 0x6E, 0x31, 0x16, 0xFB, 0xC4, 0xFA, 0xDA, 0x23, 0xC5, 0x04, 0x4D,
    0xE9, 0x77, 0xF0, 0xDB, 0x93, 0x2E, 0x99, 0xBA, 0x17, 0x36, 0xF1, 0xBB, 0x14, 0xCD, 0x5F, 0xC1,
    0xF9, 0x18, 0x65, 0x5A, 0xE2, 0x5C, 0xEF, 0x21, 0x81, 0x1C, 0x3C, 0x42, 0x8B, 0x01, 0x8E, 0xAE,
    0x05, 0x84, 0x19, 0xCE, 0xAD, 0x08, 0xB1, 0x12, 0x10, 0x3B, 0xA4, 0x70, 0xF0, 0x08, 0xC2, 0x13
};

static uint8_t INV_SBOX[256];

static uint8_t galois_mul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (int i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        uint8_t hi = a & 0x80;
        a <<= 1;
        if (hi) a ^= 0xC3;
        b >>= 1;
    }
    return p;
}

static void l_func(uint8_t *block) {
    static const uint8_t c[16] = {148, 32, 133, 16, 194, 192, 1, 251, 1, 192, 194, 16, 133, 32, 148, 1};
    for (int j = 0; j < 16; j++) {
        uint8_t res = 0;
        for (int i = 0; i < 16; i++) {
            res ^= galois_mul(block[i], c[i]);
        }
        memmove(block + 1, block, 15);
        block[0] = res;
    }
}

static void inv_l_func(uint8_t *block) {
    static const uint8_t c[16] = {148, 32, 133, 16, 194, 192, 1, 251, 1, 192, 194, 16, 133, 32, 148, 1};
    for (int j = 0; j < 16; j++) {
        uint8_t saved = block[0];
        memmove(block, block + 1, 15);
        block[15] = saved;
        uint8_t res = 0;
        for (int i = 0; i < 16; i++) {
            res ^= galois_mul(block[i], c[i]);
        }
        block[15] = res;
    }
}

void kuznyechik_init(kuznyechik_ctx_t *ctx, const uint8_t *key) {
    static int inv_sbox_initialized = 0;
    if (!inv_sbox_initialized) {
        for (int i = 0; i < 256; i++) {
            INV_SBOX[SBOX[i]] = i;
        }
        inv_sbox_initialized = 1;
    }

    memcpy(ctx->round_keys, key, 32);
    for (int i = 2; i < 10; i++) {
        uint8_t *prev = &ctx->round_keys[(i-1)*16];
        for (int j = 0; j < 16; j++) ctx->round_keys[i*16 + j] = SBOX[prev[j]] ^ i;
        l_func(&ctx->round_keys[i*16]);
    }
}

void kuznyechik_encrypt_block(kuznyechik_ctx_t *ctx, const uint8_t *in, uint8_t *out) {
    uint8_t state[16];
    memcpy(state, in, 16);
    for (int i = 0; i < 9; i++) {
        for (int j = 0; j < 16; j++) state[j] ^= ctx->round_keys[i * 16 + j]; // X
        for (int j = 0; j < 16; j++) state[j] = SBOX[state[j]];              // S
        l_func(state);                                                      // L
    }
    for (int j = 0; j < 16; j++) state[j] ^= ctx->round_keys[9 * 16 + j];
    memcpy(out, state, 16);
}

void kuznyechik_decrypt_block(kuznyechik_ctx_t *ctx, const uint8_t *in, uint8_t *out) {
    uint8_t state[16];
    memcpy(state, in, 16);

    for (int j = 0; j < 16; j++) state[j] ^= ctx->round_keys[9 * 16 + j];

    for (int i = 8; i >= 0; i--) {
        inv_l_func(state);
        for (int j = 0; j < 16; j++) state[j] = INV_SBOX[state[j]];
        for (int j = 0; j < 16; j++) state[j] ^= ctx->round_keys[i * 16 + j];
    }

    memcpy(out, state, 16);
}

static void cmac_shift_left(uint8_t *out, const uint8_t *in) {
    uint8_t carry = 0;
    for (int i = 15; i >= 0; i--) {
        uint8_t next_carry = (in[i] & 0x80) ? 1 : 0;
        out[i] = (in[i] << 1) | carry;
        carry = next_carry;
    }
}

static void cmac_generate_subkeys(kuznyechik_ctx_t *ctx, uint8_t *k1, uint8_t *k2) {
    uint8_t l[16];
    uint8_t zero[16] = {0};
    kuznyechik_encrypt_block(ctx, zero, l);

    if (l[0] & 0x80) {
        cmac_shift_left(k1, l);
        k1[15] ^= 0x87;
    } else {
        cmac_shift_left(k1, l);
    }

    if (k1[0] & 0x80) {
        cmac_shift_left(k2, k1);
        k2[15] ^= 0x87;
    } else {
        cmac_shift_left(k2, k1);
    }
}

void kuznyechik_mac(kuznyechik_ctx_t *ctx, const uint8_t *data, size_t len, uint8_t *mac) {
    uint8_t k1[16], k2[16];
    cmac_generate_subkeys(ctx, k1, k2);

    uint8_t state[16] = {0};
    size_t n = (len + 15) / 16;
    if (n == 0) n = 1;

    for (size_t i = 0; i < n - 1; i++) {
        for (int j = 0; j < 16; j++) {
            state[j] ^= data[i * 16 + j];
        }
        kuznyechik_encrypt_block(ctx, state, state);
    }

    uint8_t last_block[16] = {0};
    size_t last_len = len - (n - 1) * 16;
    memcpy(last_block, data + (n - 1) * 16, last_len);

    if (last_len == 16) {
        for (int j = 0; j < 16; j++) state[j] ^= last_block[j] ^ k1[j];
    } else {
        last_block[last_len] = 0x80;
        for (int j = 0; j < 16; j++) state[j] ^= last_block[j] ^ k2[j];
    }

    kuznyechik_encrypt_block(ctx, state, mac);
}

