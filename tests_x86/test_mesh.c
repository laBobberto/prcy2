// PC-based mesh protocol test
// Simulates two nodes and tests E2E encryption, command parsing, etc.
// Build: gcc -I../firmware/inc -I../firmware/lib/crypto -I../firmware/lib/kuznyechik \
//        test_mesh.c ../firmware/lib/crypto/mesh_crypto.c ../firmware/lib/kuznyechik/kuznyechik.c \
//        ../firmware/lib/curve25519/curve25519.c ../firmware/lib/edsign/edsign.c \
//        ../firmware/lib/sha512/sha512.c -o test_mesh -lm

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "mesh.h"
#include "mesh_crypto.h"
#include "kuznyechik.h"
#include "x25519.h"
#include "edsign.h"

// ============= Mocks =============
void debug_puts(const char *s) { printf("%s", s); }
void debug_puti(uint32_t i) { printf("%lu", (unsigned long)i); }
void debug_puti_signed(int32_t i) { printf("%ld", (long)i); }
void debug_putc(char c) { putchar(c); }

// Mock MCU UID (0x1FFFF7E8 on STM32)
uint8_t mock_uid_data[12] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};

// Mock lora - just stores the last sent packet
static mesh_packet_t last_tx_pkt;
static int tx_count = 0;
void lora_send_packet(mesh_packet_t *pkt) {
    memcpy(&last_tx_pkt, pkt, sizeof(mesh_packet_t));
    tx_count++;
}
uint16_t lora_read_battery(void) { return 3300; }

// ============= Test: E2E encrypt/decrypt cycle =============
void test_e2e_encrypt_decrypt() {
    printf("\n=== TEST: E2E Encrypt/Decrypt ===\n");

    // Init node 1 and node 2
    mesh_init(1);

    // Set up pairwise key (simulating DH handshake)
    uint8_t shared_secret[32] = "SHARED_SECRET_FOR_TEST_2026_!!!";
    mesh_set_pairwise_key(2, shared_secret);

    // Send data from node 1 to node 2
    const char *msg = "hello";
    uint8_t msg_len = strlen(msg);
    printf("[NODE1] Sending: '%s' (len=%d)\n", msg, msg_len);

    mesh_send_data(2, (const uint8_t *)msg, msg_len);

    // Check what was sent
    printf("[NODE1] TX packet: src=%d dst=%d type=%d e2e_encrypted=%d payload_len=%d\n",
           last_tx_pkt.src_id, last_tx_pkt.dst_id, last_tx_pkt.type,
           last_tx_pkt.e2e_encrypted, last_tx_pkt.payload_len);

    // Show first bytes of encrypted payload
    printf("[NODE1] Encrypted payload (first 16 bytes): ");
    for (int i = 0; i < 16 && i < last_tx_pkt.payload_len; i++) {
        printf("%02X ", last_tx_pkt.payload[i]);
    }
    printf("\n");

    // Now simulate node 2 receiving the packet
    // First, init node 2
    mesh_init(2);

    // Set up the same pairwise key for node 2 (from node 1)
    mesh_set_pairwise_key(1, shared_secret);

    // The packet we received is the one node 1 sent
    mesh_packet_t rx_pkt;
    memcpy(&rx_pkt, &last_tx_pkt, sizeof(mesh_packet_t));

    printf("[NODE2] Received packet: src=%d dst=%d e2e_encrypted=%d payload_len=%d\n",
           rx_pkt.src_id, rx_pkt.dst_id, rx_pkt.e2e_encrypted, rx_pkt.payload_len);

    // Show encrypted payload before decryption
    printf("[NODE2] Encrypted payload (first 16 bytes): ");
    for (int i = 0; i < 16 && i < rx_pkt.payload_len; i++) {
        printf("%02X ", rx_pkt.payload[i]);
    }
    printf("\n");

    // Process the packet (this should decrypt it)
    int result = mesh_process_packet(&rx_pkt);

    printf("[NODE2] mesh_process_packet returned: %d\n", result);

    // Show decrypted payload
    printf("[NODE2] Decrypted payload (first 16 bytes): ");
    for (int i = 0; i < 16 && i < rx_pkt.payload_len; i++) {
        printf("%02X ", rx_pkt.payload[i]);
    }
    printf("\n");

    // Try to print as string
    printf("[NODE2] Decrypted as string: '");
    for (int i = 0; i < rx_pkt.payload_len && i < 64; i++) {
        if (rx_pkt.payload[i] == 0) break;
        putchar(rx_pkt.payload[i]);
    }
    printf("'\n");

    // Check if it matches
    if (rx_pkt.payload_len >= msg_len && memcmp(rx_pkt.payload, msg, msg_len) == 0) {
        printf("✓ PASS: Decrypted payload matches original!\n");
    } else {
        printf("✗ FAIL: Decrypted payload does NOT match original!\n");
        printf("  Expected: '%s'\n", msg);
        printf("  Got:      '");
        for (int i = 0; i < rx_pkt.payload_len && i < 64; i++) {
            if (rx_pkt.payload[i] == 0) break;
            putchar(rx_pkt.payload[i]);
        }
        printf("'\n");
    }
}

// ============= Test: CTR mode roundtrip =============
void test_ctr_roundtrip() {
    printf("\n=== TEST: CTR Mode Roundtrip ===\n");

    kuznyechik_ctx_t ctx;
    uint8_t key[32] = "TEST_KEY_FOR_CTR_2026_!!!!!!!!!!";
    kuznyechik_init(&ctx, key);

    uint8_t original[32] = "Hello, this is a test message!!";
    uint8_t data[32];
    memcpy(data, original, 32);

    uint32_t nonce = 12345;

    printf("Original: ");
    for (int i = 0; i < 32; i++) printf("%02X ", original[i]);
    printf("\n");

    // Encrypt
    mesh_crypto_ctr(&ctx, nonce, data, 32);
    printf("Encrypted: ");
    for (int i = 0; i < 32; i++) printf("%02X ", data[i]);
    printf("\n");

    // Decrypt (same operation in CTR mode)
    mesh_crypto_ctr(&ctx, nonce, data, 32);
    printf("Decrypted: ");
    for (int i = 0; i < 32; i++) printf("%02X ", data[i]);
    printf("\n");

    if (memcmp(data, original, 32) == 0) {
        printf("✓ PASS: CTR roundtrip works!\n");
    } else {
        printf("✗ FAIL: CTR roundtrip FAILED!\n");
    }
}

// ============= Test: MIC computation =============
void test_mic_computation() {
    printf("\n=== TEST: MIC Computation ===\n");

    // Create a packet
    mesh_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.src_id = 1;
    pkt.dst_id = 2;
    pkt.type = PACKET_TYPE_DATA;
    pkt.ttl = 5;
    pkt.timestamp = 12345;
    pkt.payload_len = 16;
    memcpy(pkt.payload, "hello\0\0\0\0\0\0\0\0\0\0\0", 16);

    // Compute E2E MIC
    kuznyechik_ctx_t key;
    uint8_t key_data[32] = "TEST_KEY_FOR_MIC_2026_!!!!!!!!!!";
    kuznyechik_init(&key, key_data);

    // Cast to crypto packet type
    mesh_crypto_packet_t *cpkt = (mesh_crypto_packet_t *)&pkt;

    uint32_t mic1 = mesh_crypto_compute_e2e_mic(cpkt, &key);
    uint32_t mic2 = mesh_crypto_compute_e2e_mic(cpkt, &key);

    printf("MIC computation 1: 0x%08X\n", mic1);
    printf("MIC computation 2: 0x%08X\n", mic2);

    if (mic1 == mic2) {
        printf("✓ PASS: MIC is deterministic!\n");
    } else {
        printf("✗ FAIL: MIC is NOT deterministic!\n");
    }

    // Verify struct sizes
    printf("\nStruct sizes:\n");
    printf("  mesh_packet_t:      %zu bytes\n", sizeof(mesh_packet_t));
    printf("  mesh_crypto_packet_t: %zu bytes\n", sizeof(mesh_crypto_packet_t));

    if (sizeof(mesh_packet_t) == sizeof(mesh_crypto_packet_t)) {
        printf("✓ PASS: Struct sizes match!\n");
    } else {
        printf("✗ FAIL: Struct sizes do NOT match!\n");
    }

    // Verify field offsets
    printf("\nField offsets (mesh_packet_t):\n");
    printf("  src_id:      %zu\n", offsetof(mesh_packet_t, src_id));
    printf("  dst_id:      %zu\n", offsetof(mesh_packet_t, dst_id));
    printf("  type:        %zu\n", offsetof(mesh_packet_t, type));
    printf("  ttl:         %zu\n", offsetof(mesh_packet_t, ttl));
    printf("  timestamp:   %zu\n", offsetof(mesh_packet_t, timestamp));
    printf("  payload_len: %zu\n", offsetof(mesh_packet_t, payload_len));
    printf("  e2e_encrypted: %zu\n", offsetof(mesh_packet_t, e2e_encrypted));
    printf("  payload:     %zu\n", offsetof(mesh_packet_t, payload));
    printf("  e2e_mic:     %zu\n", offsetof(mesh_packet_t, e2e_mic));
    printf("  link_mic:    %zu\n", offsetof(mesh_packet_t, link_mic));

    printf("\nField offsets (mesh_crypto_packet_t):\n");
    printf("  src_id:      %zu\n", offsetof(mesh_crypto_packet_t, src_id));
    printf("  dst_id:      %zu\n", offsetof(mesh_crypto_packet_t, dst_id));
    printf("  type:        %zu\n", offsetof(mesh_crypto_packet_t, type));
    printf("  ttl:         %zu\n", offsetof(mesh_crypto_packet_t, ttl));
    printf("  timestamp:   %zu\n", offsetof(mesh_crypto_packet_t, timestamp));
    printf("  payload_len: %zu\n", offsetof(mesh_crypto_packet_t, payload_len));
    printf("  e2e_encrypted: %zu\n", offsetof(mesh_crypto_packet_t, e2e_encrypted));
    printf("  payload:     %zu\n", offsetof(mesh_crypto_packet_t, payload));
    printf("  e2e_mic:     %zu\n", offsetof(mesh_crypto_packet_t, e2e_mic));
    printf("  link_mic:    %zu\n", offsetof(mesh_crypto_packet_t, link_mic));
}

// ============= Test: Full mesh cycle =============
void test_full_mesh_cycle() {
    printf("\n=== TEST: Full Mesh Cycle ===\n");

    // Initialize both nodes
    mesh_init(1);
    mesh_set_pairwise_key(2, (uint8_t*)"SHARED_SECRET_2026!!!!!!!!!!!!!!");

    // Node 1 sends data
    const char *msg = "test message from node 1";
    printf("[NODE1] Sending: '%s'\n", msg);
    mesh_send_data(2, (const uint8_t *)msg, strlen(msg));

    // Show what was sent
    printf("[NODE1] Sent packet: type=%d e2e=%d len=%d\n",
           last_tx_pkt.type, last_tx_pkt.e2e_encrypted, last_tx_pkt.payload_len);

    // Now switch to node 2's perspective
    mesh_init(2);
    mesh_set_pairwise_key(1, (uint8_t*)"SHARED_SECRET_2026!!!!!!!!!!!!!!");

    // Receive the packet
    mesh_packet_t rx_pkt;
    memcpy(&rx_pkt, &last_tx_pkt, sizeof(mesh_packet_t));

    printf("[NODE2] Processing received packet...\n");
    int result = mesh_process_packet(&rx_pkt);
    printf("[NODE2] Result: %d\n", result);

    // Check payload
    printf("[NODE2] Payload: '");
    int len = 0;
    for (int i = 0; i < rx_pkt.payload_len && i < 64; i++) {
        if (rx_pkt.payload[i] == 0) break;
        putchar(rx_pkt.payload[i]);
        len++;
    }
    printf("' (len=%d)\n", len);

    if (len > 0 && memcmp(rx_pkt.payload, msg, strlen(msg)) == 0) {
        printf("✓ PASS: Full mesh cycle works!\n");
    } else {
        printf("✗ FAIL: Full mesh cycle FAILED!\n");
    }
}

// ============= Main =============
int main() {
    printf("========================================\n");
    printf("  PRCY Mesh Network PC Test Suite\n");
    printf("========================================\n");

    test_ctr_roundtrip();
    test_mic_computation();
    test_full_mesh_cycle();
    test_e2e_encrypt_decrypt();

    printf("\n========================================\n");
    printf("  All tests completed.\n");
    printf("========================================\n");

    return 0;
}
