#ifndef MESH_CRYPTO_H
#define MESH_CRYPTO_H

#include <stdint.h>
#include <stddef.h>
#include "kuznyechik.h"

// Структура пакета для вычисления MIC.
// Должна ТОЧНО совпадать по layout с mesh_packet_t из mesh.h
#pragma pack(push, 1)
typedef struct {
    uint8_t  src_id;
    uint8_t  dst_id;
    uint8_t  type;
    uint8_t  ttl;
    uint32_t timestamp;
    uint8_t  payload_len;
    uint8_t  e2e_encrypted;
    uint8_t  payload[128];
    uint32_t e2e_mic;
    uint32_t link_mic;
} mesh_crypto_packet_t;
#pragma pack(pop)

/**
 * @brief Режим CTR для Кузнечика (портативный)
 */
void mesh_crypto_ctr(kuznyechik_ctx_t *ctx, uint32_t nonce, uint8_t *data, uint8_t len);

/**
 * @brief Вычисление E2E MIC (32 бита)
 */
uint32_t mesh_crypto_compute_e2e_mic(mesh_crypto_packet_t *pkt, kuznyechik_ctx_t *key);

/**
 * @brief Вычисление Link MIC (32 бита)
 */
uint32_t mesh_crypto_compute_link_mic(mesh_crypto_packet_t *pkt, kuznyechik_ctx_t *session_key);

#endif // MESH_CRYPTO_H
