#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "mesh_crypto.h"
#include "kuznyechik.h"
#include "x25519.h"
#include "edsign.h"

// Mock for debug_puts
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
    assert(memcmp(data, original, 32) != 0);
    mesh_crypto_ctr(&ctx, nonce, data, 32);
    assert(memcmp(data, original, 32) == 0);
    printf("✓ CTR mode works!\n");
}

void test_mic_32bit() {
    printf("Testing 32-bit MIC (CMAC)...\n");
    kuznyechik_ctx_t key_ctx;
    uint8_t key[32] = "PAIRWISE_KEY_FOR_MIC_TEST_!!!!!";
    kuznyechik_init(&key_ctx, key);
    mesh_crypto_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.src_id = 1; pkt.dst_id = 2; pkt.type = 0; pkt.timestamp = 100;
    pkt.payload_len = 16; strcpy((char*)pkt.payload, "Integrity check");
    uint32_t mic1 = mesh_crypto_compute_e2e_mic(&pkt, &key_ctx);
    pkt.payload[0] ^= 0xFF;
    uint32_t mic2 = mesh_crypto_compute_e2e_mic(&pkt, &key_ctx);
    assert(mic1 != mic2);
    printf("✓ 32-bit MIC works!\n");
}

void test_x25519() {
    printf("Testing X25519...\n");
    uint8_t alice_sk[32] = {
        0x77,0x07,0x6d,0x0a,0x73,0x18,0xa5,0x7d,0x3c,0x16,0xc1,0x72,0x51,0xb2,0x66,0x45,
        0xdf,0x4c,0x2f,0x87,0xeb,0xc0,0x99,0x2a,0xb1,0x77,0xfb,0xa5,0x1d,0xb9,0x2c,0x1c
    };
    uint8_t alice_pk[32];
    x25519_base(alice_pk, alice_sk);
    
    uint8_t bob_sk[32] = {
        0x5d,0xab,0x08,0x7e,0x62,0x4a,0x8a,0x4b,0x79,0xe1,0x7f,0x8b,0x83,0x80,0x0e,0xe6,
        0x6f,0x3b,0xb1,0x29,0x26,0x18,0xb6,0xfd,0x1c,0x2f,0x8b,0x27,0xff,0x88,0xe0,0xeb
    };
    uint8_t bob_pk[32];
    x25519_base(bob_pk, bob_sk);
    
    uint8_t shared_alice[32];
    uint8_t shared_bob[32];
    x25519(shared_alice, alice_sk, bob_pk);
    x25519(shared_bob, bob_sk, alice_pk);

    assert(memcmp(shared_alice, shared_bob, 32) == 0);
    printf("✓ DH Exchange consistent!\n");
}

void test_ed25519() {
    printf("Testing Ed25519 signatures...\n");
    uint8_t secret[32] = "SECRET_IDENTITY_FOR_TESTING_!!!!";
    uint8_t public[32];
    edsign_sec_to_pub(public, secret);
    uint8_t message[32] = "Authentication message 12345678";
    uint8_t signature[64];
    edsign_sign(signature, public, secret, message, 32);
    uint8_t ok = edsign_verify(signature, public, message, 32);
    assert(ok == 1);
    printf("✓ Signature verified!\n");
}

void test_cmac_compat_output() {
    printf("CMAC Compatibility Check:\n");
    kuznyechik_ctx_t ctx;
    uint8_t key[32] = "TEST_KEY_FOR_CMAC_COMPAT_!!!!!12";
    kuznyechik_init(&ctx, key);
    uint8_t data[] = "Hello, CMAC compatibility test! 12345";
    uint8_t mac[16];
    kuznyechik_mac(&ctx, data, sizeof(data) - 1, mac);
    printf("CMAC (C): ");
    for(int i=0; i<16; i++) printf("%02x", mac[i]); printf("\n");
}

int main() {
    test_ctr_mode();
    test_mic_32bit();
    test_x25519();
    test_ed25519();
    test_cmac_compat_output();
    printf("\nAll crypto tests passed!\n");
    return 0;
}
