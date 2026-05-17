
#include "kuznyechik.h"
#include <string.h>

// GOST R 34.12-2015 Kuznyechik Pi substitution (S-Box)
// From RFC 7801 — 256 unique values, valid bijection
static const uint8_t SBOX[256] = {
    0xFC, 0xEE, 0xDD, 0x11, 0xCF, 0x6E, 0x31, 0x16, 0xFB, 0xC4, 0xFA, 0xDA, 0x23, 0xC5, 0x04, 0x4D,
    0xE9, 0x77, 0xF0, 0xDB, 0x93, 0x2E, 0x99, 0xBA, 0x17, 0x36, 0xF1, 0xBB, 0x14, 0xCD, 0x5F, 0xC1,
    0xF9, 0x18, 0x65, 0x5A, 0xE2, 0x5C, 0xEF, 0x21, 0x81, 0x1C, 0x3C, 0x42, 0x8B, 0x01, 0x8E, 0x4F,
    0x05, 0x84, 0x02, 0xAE, 0xE3, 0x6A, 0x8F, 0xA0, 0x06, 0x0B, 0xED, 0x98, 0x7F, 0xD4, 0xD3, 0x1F,
    0xEB, 0x34, 0x2C, 0x51, 0xEA, 0xC8, 0x48, 0xAB, 0xF2, 0x2A, 0x68, 0xA2, 0xFD, 0x3A, 0xCE, 0xCC,
    0xB5, 0x70, 0x0E, 0x56, 0x08, 0x0C, 0x76, 0x12, 0xBF, 0x72, 0x13, 0x47, 0x9C, 0xB7, 0x5D, 0x87,
    0x15, 0xA1, 0x96, 0x29, 0x10, 0x7B, 0x9A, 0xC7, 0xF3, 0x91, 0x78, 0x6F, 0x9D, 0x9E, 0xB2, 0xB1,
    0x32, 0x75, 0x19, 0x3D, 0xFF, 0x35, 0x8A, 0x7E, 0x6D, 0x54, 0xC6, 0x80, 0xC3, 0xBD, 0x0D, 0x57,
    0xDF, 0xF5, 0x24, 0xA9, 0x3E, 0xA8, 0x43, 0xC9, 0xD7, 0x79, 0xD6, 0xF6, 0x7C, 0x22, 0xB9, 0x03,
    0xE0, 0x0F, 0xEC, 0xDE, 0x7A, 0x94, 0xB0, 0xBC, 0xDC, 0xE8, 0x28, 0x50, 0x4E, 0x33, 0x0A, 0x4A,
    0xA7, 0x97, 0x60, 0x73, 0x1E, 0x00, 0x62, 0x44, 0x1A, 0xB8, 0x38, 0x82, 0x64, 0x9F, 0x26, 0x41,
    0xAD, 0x45, 0x46, 0x92, 0x27, 0x5E, 0x55, 0x2F, 0x8C, 0xA3, 0xA5, 0x7D, 0x69, 0xD5, 0x95, 0x3B,
    0x07, 0x58, 0xB3, 0x40, 0x86, 0xAC, 0x1D, 0xF7, 0x30, 0x37, 0x6B, 0xE4, 0x88, 0xD9, 0xE7, 0x89,
    0xE1, 0x1B, 0x83, 0x49, 0x4C, 0x3F, 0xF8, 0xFE, 0x8D, 0x53, 0xAA, 0x90, 0xCA, 0xD8, 0x85, 0x61,
    0x20, 0x71, 0x67, 0xA4, 0x2D, 0x2B, 0x09, 0x5B, 0xCB, 0x9B, 0x25, 0xD0, 0xBE, 0xE5, 0x6C, 0x52,
    0x59, 0xA6, 0x74, 0xD2, 0xE6, 0xF4, 0xB4, 0xC0, 0xD1, 0x66, 0xAF, 0xC2, 0x39, 0x4B, 0x63, 0xB6
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

