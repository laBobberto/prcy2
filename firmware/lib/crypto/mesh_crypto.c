#include "mesh_crypto.h"
#include <string.h>

void mesh_crypto_ctr(kuznyechik_ctx_t *ctx, uint32_t nonce, uint8_t *data, uint8_t len) {
    uint8_t counter_block[16];
    uint8_t keystream[16];
    for (int i = 0; i < len; i += 16) {
        memset(counter_block, 0, 16);
        memcpy(counter_block, &nonce, 4);
        uint32_t block_idx = i / 16;
        memcpy(counter_block + 4, &block_idx, 4);
        
        kuznyechik_encrypt_block(ctx, counter_block, keystream);
        
        for (int j = 0; j < 16 && (i + j) < len; j++) {
            data[i + j] ^= keystream[j];
        }
    }
}

uint32_t mesh_crypto_compute_e2e_mic(mesh_crypto_packet_t *pkt, kuznyechik_ctx_t *key) {
    uint8_t mac_data[128];
    int offset = 0;

    mac_data[offset++] = pkt->src_id;
    mac_data[offset++] = pkt->dst_id;
    mac_data[offset++] = pkt->type;
    memcpy(mac_data + offset, &pkt->timestamp, 4);
    offset += 4;
    mac_data[offset++] = pkt->payload_len;
    memcpy(mac_data + offset, pkt->payload, pkt->payload_len);
    offset += pkt->payload_len;

    uint8_t mac[16];
    kuznyechik_mac(key, mac_data, offset, mac);

    // Возвращаем 32 бита (4 байта) согласно плану
    return (uint32_t)mac[0] << 24 | (uint32_t)mac[1] << 16 | (uint32_t)mac[2] << 8 | (uint32_t)mac[3];
}

uint32_t mesh_crypto_compute_link_mic(mesh_crypto_packet_t *pkt, kuznyechik_ctx_t *session_key) {
    uint8_t mac_data[128];
    int offset = 0;

    mac_data[offset++] = pkt->src_id;
    mac_data[offset++] = pkt->dst_id;
    mac_data[offset++] = pkt->type;
    // TTL НЕ включаем — он меняется при пересылке
    memcpy(mac_data + offset, &pkt->timestamp, 4);
    offset += 4;
    mac_data[offset++] = pkt->payload_len;
    mac_data[offset++] = pkt->e2e_encrypted;
    memcpy(mac_data + offset, pkt->payload, pkt->payload_len);
    offset += pkt->payload_len;
    
    // Включаем e2e_mic в расчет link_mic
    memcpy(mac_data + offset, &pkt->e2e_mic, 4);
    offset += 4;

    uint8_t mac[16];
    kuznyechik_mac(session_key, mac_data, offset, mac);

    // Возвращаем 32 бита
    return (uint32_t)mac[0] << 24 | (uint32_t)mac[1] << 16 | (uint32_t)mac[2] << 8 | (uint32_t)mac[3];
}
