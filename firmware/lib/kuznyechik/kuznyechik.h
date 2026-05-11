
#ifndef KUZNYECHIK_H
#define KUZNYECHIK_H

#include <stdint.h>
#include <stddef.h>

#define KUZ_BLOCK_SIZE 16
#define KUZ_KEY_SIZE   32
#define KUZ_ROUND_KEYS_SIZE 160

typedef struct {
    uint8_t round_keys[KUZ_ROUND_KEYS_SIZE];
} kuznyechik_ctx_t;

/**
 * @brief Инициализация контекста и развертывание ключей
 * @param ctx Контекст алгоритма
 * @param key Ключ 32 байта (256 бит)
 */
void kuznyechik_init(kuznyechik_ctx_t *ctx, const uint8_t *key);

/**
 * @brief Шифрование одного блока (16 байт)
 */
void kuznyechik_encrypt_block(kuznyechik_ctx_t *ctx, const uint8_t *in, uint8_t *out);

/**
 * @brief Дешифрование одного блока (16 байт)
 */
void kuznyechik_decrypt_block(kuznyechik_ctx_t *ctx, const uint8_t *in, uint8_t *out);

/**
 * @brief HMAC на базе Kuznyechik (упрощенная версия для МК)
 * @param ctx Контекст с ключом
 * @param data Данные для вычисления MAC
 * @param len Длина данных
 * @param mac Выходной MAC (16 байт)
 */
void kuznyechik_mac(kuznyechik_ctx_t *ctx, const uint8_t *data, size_t len, uint8_t *mac);

#endif // KUZNYECHIK_H
