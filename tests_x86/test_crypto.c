#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "mesh_crypto.h"
#include "kuznyechik.h"

// Mock for debug_puts (not needed for crypto but for completeness if it were linked)
void debug_puts(const char *s) { printf("%s", s); }
void debug_puti(int i) { printf("%d", i); }
void debug_putc(char c) { printf("%c", c); }

void test_ctr_mode() {
    printf("Testing CTR mode...\n");
    kuznyechik_ctx_t ctx;
    uint8_t key[32] = "TEST_KEY_FOR_CTR_MODE_2026_!!!!";
    kuznyechik_init(&ctx, key);

    uint8_t data[32] = "Hello, this is a secret message!";
    uint8_t original[32];
    memcpy(original, data, 32);

    uint32_t nonce = 12345;
    mesh_crypto_ctr(&ctx, nonce, data, 32);

    assert(memcmp(data, original, 32) != 0); // Data should be encrypted

    mesh_crypto_ctr(&ctx, nonce, data, 32); // CTR is symmetric
    assert(memcmp(data, original, 32) == 0); // Data should be decrypted

    printf("✓ CTR mode works!\n");
}

void test_mic_32bit() {
    printf("Testing 32-bit MIC...\n");
    kuznyechik_ctx_t key_ctx;
    uint8_t key[32] = "PAIRWISE_KEY_FOR_MIC_TEST_!!!!!";
    kuznyechik_init(&key_ctx, key);

    mesh_crypto_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.src_id = 1;
    pkt.dst_id = 2;
    pkt.type = 0;
    pkt.timestamp = 100;
    pkt.payload_len = 16;
    strcpy((char*)pkt.payload, "Integrity check");

    uint32_t mic1 = mesh_crypto_compute_e2e_mic(&pkt, &key_ctx);
    printf("MIC: 0x%08X\n", mic1);

    // Tamper with payload
    pkt.payload[0] ^= 0xFF;
    uint32_t mic2 = mesh_crypto_compute_e2e_mic(&pkt, &key_ctx);
    assert(mic1 != mic2);

    // Restore and tamper with src_id
    pkt.payload[0] ^= 0xFF;
    pkt.src_id = 3;
    uint32_t mic3 = mesh_crypto_compute_e2e_mic(&pkt, &key_ctx);
    assert(mic1 != mic3);

    printf("✓ 32-bit MIC works!\n");
}

int main() {
    test_ctr_mode();
    test_mic_32bit();
    printf("\nAll crypto tests passed on x86!\n");
    return 0;
}
