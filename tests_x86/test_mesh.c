// Basic crypto primitive tests (no mesh protocol needed)
// Build: gcc -I tests_x86 -I firmware/inc -I firmware/lib/crypto -I firmware/lib/kuznyechik \
//        -DSTM32F103xB test_mesh.c firmware/lib/crypto/mesh_crypto.c \
//        firmware/lib/kuznyechik/kuznyechik.c firmware/lib/crypto/x25519_wrapper.c \
//        firmware/lib/crypto/c25519.c firmware/lib/crypto/f25519.c firmware/lib/crypto/fprime.c \
//        firmware/lib/crypto/ed25519.c firmware/lib/crypto/edsign.c firmware/lib/crypto/sha512.c \
//        -o test_mesh -lm

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "mesh.h"
#include "mesh_crypto.h"
#include "kuznyechik.h"

void debug_puts(const char *s) { printf("%s", s); }
void debug_puti(uint32_t i) { printf("%lu", (unsigned long)i); }
void debug_puti_signed(int32_t i) { printf("%ld", (long)i); }
void debug_putc(char c) { putchar(c); }

// Stubs for mesh.c dependencies
void lora_send_packet(mesh_packet_t *p) { (void)p; }
uint16_t lora_read_battery(void) { return 3300; }

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
    assert(memcmp(data, original, 32) != 0);
    mesh_crypto_ctr(&ctx, nonce, data, 32);
    assert(memcmp(data, original, 32) == 0);
    printf("✓ PASS: CTR mode works!\n");
}

void test_mic_32bit() {
    printf("Testing 32-bit MIC...\n");
    mesh_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.src_id = 1;
    pkt.dst_id = 2;
    pkt.type = PACKET_TYPE_DATA;
    pkt.ttl = 5;
    pkt.timestamp = 12345;
    pkt.payload_len = 16;

    kuznyechik_ctx_t key;
    uint8_t key_data[32] = "TEST_KEY_FOR_MIC_2026_!!!!!!!!!!";
    kuznyechik_init(&key, key_data);

    mesh_crypto_packet_t *cpkt = (mesh_crypto_packet_t *)&pkt;
    uint32_t mic1 = mesh_crypto_compute_e2e_mic(cpkt, &key);
    uint32_t mic2 = mesh_crypto_compute_e2e_mic(cpkt, &key);

    assert(mic1 == mic2);
    printf("✓ PASS: MIC is deterministic (0x%08X)\n", mic1);
}

void test_struct_layout() {
    printf("Testing struct layout...\n");
    assert(sizeof(mesh_packet_t) == sizeof(mesh_crypto_packet_t));
    assert(offsetof(mesh_packet_t, e2e_mic) == offsetof(mesh_crypto_packet_t, e2e_mic));
    assert(offsetof(mesh_packet_t, link_mic) == offsetof(mesh_crypto_packet_t, link_mic));
    printf("✓ PASS: Struct sizes match (%zu bytes)\n", sizeof(mesh_packet_t));
    printf("  e2e_mic at offset %zu\n", offsetof(mesh_packet_t, e2e_mic));
    printf("  link_mic at offset %zu\n", offsetof(mesh_packet_t, link_mic));
}

int main() {
    printf("========================================\n");
    printf("  PRCY Crypto Primitive Tests\n");
    printf("========================================\n\n");

    test_ctr_mode();
    test_mic_32bit();
    test_struct_layout();

    printf("\n========================================\n");
    printf("  All tests passed.\n");
    printf("========================================\n");
    return 0;
}
