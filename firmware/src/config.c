#include "config.h"
#include "stm32f1xx_hal.h"
#include <string.h>

// Simple CRC32 implementation
static uint32_t crc32_calc(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

int config_save(const config_t *cfg) {
    // Unlock Flash
    HAL_FLASH_Unlock();

    // Erase the page
    FLASH_EraseInitTypeDef erase;
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = CONFIG_FLASH_ADDR;
    erase.NbPages = 1;
    uint32_t error;
    if (HAL_FLASHEx_Erase(&erase, &error) != HAL_OK) {
        HAL_FLASH_Lock();
        return -1;
    }

    // Write config word by word (16-bit for STM32F1)
    const uint16_t *data = (const uint16_t *)cfg;
    uint32_t addr = CONFIG_FLASH_ADDR;
    uint32_t words = (sizeof(config_t) + 1) / 2;

    for (uint32_t i = 0; i < words; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, data[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return -1;
        }
        addr += 2;
    }

    HAL_FLASH_Lock();
    return 0;
}

int config_load(config_t *cfg) {
    const config_t *flash = (const config_t *)CONFIG_FLASH_ADDR;

    // Check magic
    if (flash->magic != CONFIG_MAGIC) {
        return -1;
    }

    // Verify CRC
    uint32_t calc_crc = crc32_calc((const uint8_t *)flash,
                                    offsetof(config_t, crc32));
    if (calc_crc != flash->crc32) {
        return -1;
    }

    memcpy(cfg, flash, sizeof(config_t));
    return 0;
}

int config_erase(void) {
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase;
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = CONFIG_FLASH_ADDR;
    erase.NbPages = 1;
    uint32_t error;
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &error);

    HAL_FLASH_Lock();
    return (status == HAL_OK) ? 0 : -1;
}

void config_prepare(config_t *cfg, uint8_t node_id) {
    memset(cfg, 0, sizeof(config_t));
    cfg->magic = CONFIG_MAGIC;
    cfg->node_id = node_id;
    cfg->is_time_master = 0;
    // CRC will be set by caller after filling identity keys
}

void config_finalize_crc(config_t *cfg) {
    cfg->crc32 = crc32_calc((const uint8_t *)cfg, offsetof(config_t, crc32));
}
