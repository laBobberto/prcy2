// Full mesh protocol simulation with route establishment
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "mesh.h"
#include "mesh_crypto.h"
#include "kuznyechik.h"

// ============= Mocks =============
void debug_puts(const char *s) { printf("%s", s); }
void debug_puti(uint32_t i) { printf("%lu", (unsigned long)i); }
void debug_puti_signed(int32_t i) { printf("%ld", (long)i); }
void debug_putc(char c) { putchar(c); }

// Packet capture
static mesh_packet_t captured_pkts[16];
static int captured_count = 0;

void lora_send_packet(mesh_packet_t *pkt) {
    if (captured_count < 16) {
        memcpy(&captured_pkts[captured_count], pkt, sizeof(mesh_packet_t));
        captured_count++;
    }
}
uint16_t lora_read_battery(void) { return 3300; }

// Helper: print hex
static void hex_dump(const char *label, const uint8_t *data, int len) {
    printf("  %s: ", label);
    for (int i = 0; i < len && i < 32; i++) printf("%02X ", data[i]);
    printf("\n");
}

// Helper: print packet info
static void print_pkt(const char *tag, mesh_packet_t *p) {
    printf("  [%s] src=%d dst=%d type=%d ttl=%d e2e=%d len=%d\n",
           tag, p->src_id, p->dst_id, p->type, p->ttl, p->e2e_encrypted, p->payload_len);
}

// ============= Test: Full cycle with route =============
void test_full_cycle_with_route() {
    printf("\n=== TEST: Full Cycle With Route Establishment ===\n");
    captured_count = 0;

    // Shared secret (from DH handshake)
    uint8_t shared_secret[32] = "SHARED_SECRET_FOR_MESH_TEST_!!";

    // --- Phase 1: Init both nodes ---
    printf("\n--- Phase 1: Init nodes ---\n");
    mesh_init(1);
    mesh_set_pairwise_key(2, shared_secret);

    // Manually add route from node 1 to node 2 (simulating RREP)
    mesh_add_route(2, 2, 1, 1);

    printf("[NODE1] Route to 2 added\n");

    // --- Phase 2: Node 1 sends data ---
    printf("\n--- Phase 2: Node 1 sends 'hello' to node 2 ---\n");
    captured_count = 0;
    mesh_send_data(2, (const uint8_t *)"hello", 5);

    printf("[NODE1] Captured %d packet(s):\n", captured_count);
    for (int i = 0; i < captured_count; i++) {
        print_pkt("TX", &captured_pkts[i]);
    }

    // Find the DATA packet (type=0)
    mesh_packet_t *data_pkt = NULL;
    for (int i = 0; i < captured_count; i++) {
        if (captured_pkts[i].type == PACKET_TYPE_DATA) {
            data_pkt = &captured_pkts[i];
            break;
        }
    }

    if (!data_pkt) {
        printf("✗ FAIL: No DATA packet found!\n");
        return;
    }

    printf("\n  DATA packet details:\n");
    print_pkt("DATA", data_pkt);
    hex_dump("payload", data_pkt->payload, data_pkt->payload_len);
    printf("  e2e_encrypted: %d\n", data_pkt->e2e_encrypted);
    printf("  e2e_mic: 0x%08X\n", data_pkt->e2e_mic);
    printf("  link_mic: 0x%08X\n", data_pkt->link_mic);

    // --- Phase 3: Node 2 receives and decrypts ---
    printf("\n--- Phase 3: Node 2 receives packet ---\n");
    mesh_init(2);
    mesh_set_pairwise_key(1, shared_secret);

    mesh_packet_t rx_pkt;
    memcpy(&rx_pkt, data_pkt, sizeof(mesh_packet_t));

    printf("[NODE2] Before decryption:\n");
    hex_dump("payload", rx_pkt.payload, rx_pkt.payload_len);

    int result = mesh_process_packet(&rx_pkt);

    printf("[NODE2] mesh_process_packet returned: %d\n", result);
    printf("[NODE2] After decryption:\n");
    hex_dump("payload", rx_pkt.payload, rx_pkt.payload_len);

    // Print as string
    printf("[NODE2] As string: '");
    int print_len = 0;
    for (int i = 0; i < rx_pkt.payload_len && i < 64; i++) {
        if (rx_pkt.payload[i] == 0) break;
        putchar(rx_pkt.payload[i]);
        print_len++;
    }
    printf("' (len=%d)\n", print_len);

    if (print_len == 5 && memcmp(rx_pkt.payload, "hello", 5) == 0) {
        printf("\n✓ PASS: E2E encryption/decryption works!\n");
    } else {
        printf("\n✗ FAIL: Payload mismatch!\n");
        printf("  Expected: 'hello' (len=5)\n");
        printf("  Got:      '");
        for (int i = 0; i < rx_pkt.payload_len && i < 64; i++) {
            if (rx_pkt.payload[i] == 0) break;
            putchar(rx_pkt.payload[i]);
        }
        printf("' (len=%d)\n", print_len);
    }
}

// ============= Test: Encrypt/decrypt without mesh_init =============
void test_raw_encrypt_decrypt() {
    printf("\n=== TEST: Raw Encrypt/Decrypt ===\n");

    uint8_t key[32] = "TEST_KEY_12345678901234567890!!";
    kuznyechik_ctx_t ctx;
    kuznyechik_init(&ctx, key);

    // Original data
    uint8_t original[16] = "hello\0\0\0\0\0\0\0\0\0\0\0";
    uint8_t data[16];
    memcpy(data, original, 16);

    printf("Original: ");
    for (int i = 0; i < 16; i++) printf("%02X ", data[i]);
    printf(" = '%s'\n", data);

    // Encrypt
    uint32_t nonce = 12345;
    mesh_crypto_ctr(&ctx, nonce, data, 16);

    printf("Encrypted: ");
    for (int i = 0; i < 16; i++) printf("%02X ", data[i]);
    printf("\n");

    // Decrypt (same operation)
    kuznyechik_ctx_t ctx2;
    kuznyechik_init(&ctx2, key);
    mesh_crypto_ctr(&ctx2, nonce, data, 16);

    printf("Decrypted: ");
    for (int i = 0; i < 16; i++) printf("%02X ", data[i]);
    printf(" = '%s'\n", data);

    if (memcmp(data, original, 16) == 0) {
        printf("✓ PASS: Raw encrypt/decrypt works!\n");
    } else {
        printf("✗ FAIL: Raw encrypt/decrypt FAILED!\n");
    }
}

// ============= Test: Send and receive PING =============
void test_ping_pong() {
    printf("\n=== TEST: PING/PONG ===\n");
    captured_count = 0;

    uint8_t shared_secret[32] = "PING_PONG_SECRET_2026!!!!!!!!!!";

    // Init node 1
    mesh_init(1);
    mesh_set_pairwise_key(2, shared_secret);
    mesh_add_route(2, 2, 1, 1);

    // Send PING
    printf("[NODE1] Sending PING to node 2\n");
    mesh_send_data(2, (const uint8_t *)"PING", 4);

    // Find DATA packet
    mesh_packet_t *ping_pkt = NULL;
    for (int i = 0; i < captured_count; i++) {
        if (captured_pkts[i].type == PACKET_TYPE_DATA) {
            ping_pkt = &captured_pkts[i];
            break;
        }
    }

    if (!ping_pkt) {
        printf("✗ FAIL: No PING packet found!\n");
        return;
    }

    // Node 2 receives PING
    mesh_init(2);
    mesh_set_pairwise_key(1, shared_secret);
    mesh_add_route(1, 1, 1, 1);

    mesh_packet_t rx;
    memcpy(&rx, ping_pkt, sizeof(mesh_packet_t));
    captured_count = 0;

    printf("[NODE2] Processing PING...\n");
    int result = mesh_process_packet(&rx);
    printf("[NODE2] Result: %d\n", result);

    // Check if PONG was sent back
    printf("[NODE2] Captured %d packet(s):\n", captured_count);
    mesh_packet_t *pong_pkt = NULL;
    for (int i = 0; i < captured_count; i++) {
        print_pkt("TX", &captured_pkts[i]);
        if (captured_pkts[i].type == PACKET_TYPE_DATA) {
            pong_pkt = &captured_pkts[i];
        }
    }

    if (pong_pkt) {
        printf("[NODE2] PONG packet: src=%d dst=%d type=%d e2e=%d len=%d\n",
               pong_pkt->src_id, pong_pkt->dst_id, pong_pkt->type,
               pong_pkt->e2e_encrypted, pong_pkt->payload_len);

        // PONG is E2E encrypted, so we can't check plaintext directly
        // Instead verify it's a DATA packet from node 2 to node 1
        if (pong_pkt->type == PACKET_TYPE_DATA &&
            pong_pkt->src_id == 2 &&
            pong_pkt->dst_id == 1 &&
            pong_pkt->e2e_encrypted == 1) {
            printf("✓ PASS: PING/PONG works! (PONG sent E2E encrypted to node 1)\n");
        } else {
            printf("✗ FAIL: PONG packet wrong type/src/dst\n");
        }
    } else {
        printf("✗ FAIL: No PONG sent back\n");
    }
}

// ============= Main =============
int main() {
    printf("========================================\n");
    printf("  PRCY Mesh Protocol Test Suite\n");
    printf("========================================\n");

    test_raw_encrypt_decrypt();
    test_full_cycle_with_route();
    test_ping_pong();

    printf("\n========================================\n");
    printf("  All tests completed.\n");
    printf("========================================\n");

    return 0;
}
