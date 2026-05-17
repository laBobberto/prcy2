#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// Configuration stored in Flash (last page)
#define CONFIG_FLASH_ADDR  ((uint32_t)0x0800FC00)  // Last 1KB page of 64KB flash
#define CONFIG_MAGIC       0x50524359  // "PRCY"

typedef struct {
    uint32_t magic;           // CONFIG_MAGIC if valid
    uint8_t  node_id;         // This node's ID
    uint8_t  is_time_master;  // 1 if this node is time master
    uint8_t  reserved[2];     // padding
    uint8_t  identity_priv[32]; // Ed25519 private key
    uint8_t  identity_pub[32];  // Ed25519 public key
    uint32_t crc32;           // CRC of all above
} config_t;

// Save config to Flash
int config_save(const config_t *cfg);

// Load config from Flash (returns 0 if valid, -1 if no config)
int config_load(config_t *cfg);

// Erase config from Flash
int config_erase(void);

#endif
