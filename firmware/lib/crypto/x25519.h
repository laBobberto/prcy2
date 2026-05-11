#ifndef X25519_H
#define X25519_H

#include <stdint.h>

/**
 * @brief Вычисление публичного ключа из приватного (base point multiplication)
 * @param out Выходной буфер (32 байта)
 * @param scalar Приватный ключ (32 байта)
 */
void x25519_base(uint8_t *out, const uint8_t *scalar);

/**
 * @brief Вычисление общего секрета (Point multiplication)
 * @param out Выходной буфер (32 байта)
 * @param scalar Свой приватный ключ (32 байта)
 * @param point Чужой публичный ключ (32 байта)
 */
void x25519(uint8_t *out, const uint8_t *scalar, const uint8_t *point);

#endif // X25519_H
