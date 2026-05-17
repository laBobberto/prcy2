
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "mesh.h"
#include "lora.h"
#include "debug.h"
#include "config.h"
#include "stm32f1xx_hal.h"

void SysTick_Handler(void) {
    HAL_IncTick();
}

void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    // Use HSE (External Crystal) for stability required by USB
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9; // 8MHz * 9 = 72MHz
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);

    // USB clock must be exactly 48MHz. 72MHz / 1.5 = 48MHz
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
}

#ifdef WORK_AS_LOOPBACK_FOR_NODE_2
// Loopback function for node 2 — called AFTER mesh_process_packet decrypts the payload
static void handle_loopback(mesh_packet_t *rx_pkt) {
    if (rx_pkt->type == PACKET_TYPE_DATA && rx_pkt->dst_id == 2) {
        debug_puts("[LOOPBACK] Echoing data back to ");
        debug_puti(rx_pkt->src_id);
        debug_puts("\n");

        mesh_send_data(rx_pkt->src_id, rx_pkt->payload, rx_pkt->payload_len);
    }
}
#endif

#ifdef USE_USB_CDC
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
USBD_HandleTypeDef hUsbDeviceFS;
#endif

// Read one character from USB CDC or UART (blocking with timeout)
static int serial_read_char(char *out, uint32_t timeout_ms) {
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms || timeout_ms == 0) {
        uint8_t c;
#ifdef USE_USB_CDC
        if (VCP_read(&c, 1) > 0) {
            *out = (char)c;
            return 1;
        }
#endif
        if (HAL_UART_Receive(&huart1, &c, 1, 0) == HAL_OK) {
            *out = (char)c;
            return 1;
        }
    }
    return 0;
}

// Read a line from serial into buf. Returns length (excluding \n).
static int serial_read_line(char *buf, int max_len) {
    int idx = 0;
    while (1) {
        char c;
        if (!serial_read_char(&c, 0)) continue; // block forever

        if (c == '\n' || c == '\r') {
            if (idx > 0) {
                buf[idx] = '\0';
                return idx;
            }
            continue; // skip leading whitespace
        }
        if (c == '\b' || c == 127) { // backspace
            if (idx > 0) {
                idx--;
                debug_puts("\b \b"); // erase char on terminal
            }
            continue;
        }
        if (c >= 0x20 && c <= 0x7E && idx < max_len - 1) {
            buf[idx++] = c;
            debug_putc(c); // echo
        }
    }
}

// Parse hex string to bytes. Returns number of bytes parsed.
static int hex_to_bytes(const char *hex, uint8_t *out, int max_bytes) {
    int len = 0;
    while (*hex && len < max_bytes) {
        uint8_t hi = 0, lo = 0;
        char c;
        c = *hex++;
        if (c >= '0' && c <= '9') hi = c - '0';
        else if (c >= 'a' && c <= 'f') hi = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') hi = c - 'A' + 10;
        else break;
        c = *hex++;
        if (c >= '0' && c <= '9') lo = c - '0';
        else if (c >= 'a' && c <= 'f') lo = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') lo = c - 'A' + 10;
        else break;
        out[len++] = (hi << 4) | lo;
    }
    return len;
}

// Default network secret
static const uint8_t default_secret[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20
};

static void config_wizard(config_t *cfg) {
    char buf[128];

    debug_puts("\n==========================================\n");
    debug_puts("  PRCY Mesh Network - First Boot Setup\n");
    debug_puts("==========================================\n\n");

    // Network secret
    debug_puts("Enter network secret (32 bytes hex, 64 chars).\n");
    debug_puts("Press Enter for default:\n> ");

    memset(cfg, 0, sizeof(config_t));
    cfg->magic = CONFIG_MAGIC;

    int len = serial_read_line(buf, sizeof(buf));
    debug_puts("\n");

    if (len == 0 || (len == 1 && buf[0] == '\0')) {
        // Use default
        memcpy(cfg->network_secret, default_secret, 32);
        debug_puts("[CONFIG] Using default network secret\n");
    } else if (len >= 64) {
        // Parse hex
        int parsed = hex_to_bytes(buf, cfg->network_secret, 32);
        if (parsed < 32) {
            debug_puts("[CONFIG] Invalid hex, using default\n");
            memcpy(cfg->network_secret, default_secret, 32);
        } else {
            debug_puts("[CONFIG] Network secret set: ");
            for (int i = 0; i < 4; i++) {
                debug_puti(cfg->network_secret[i]);
                debug_puts(" ");
            }
            debug_puts("...\n");
        }
    } else {
        // Too short for hex, use as passphrase -> hash to 32 bytes
        debug_puts("[CONFIG] Passphrase mode (hashing to 32 bytes)\n");
        // Simple hash: repeat passphrase into 32 bytes
        memset(cfg->network_secret, 0, 32);
        for (int i = 0; i < 32; i++) {
            cfg->network_secret[i] = buf[i % len] ^ (uint8_t)(i * 37);
        }
    }

    // Node ID
    debug_puts("\nEnter node ID (1-254):\n> ");
    len = serial_read_line(buf, sizeof(buf));
    debug_puts("\n");

    int node_id = 0;
    if (len > 0) {
        node_id = atoi(buf);
    }
    if (node_id < 1 || node_id > 254) {
        node_id = 1;
        debug_puts("[CONFIG] Invalid, using default node ID: 1\n");
    }
    cfg->node_id = (uint8_t)node_id;
    cfg->is_time_master = (node_id == 1) ? 1 : 0;

    // Summary
    debug_puts("\n==========================================\n");
    debug_puts("  Node ID:         "); debug_puti(cfg->node_id); debug_puts("\n");
    debug_puts("  Time Master:     "); debug_puts(cfg->is_time_master ? "YES" : "NO"); debug_puts("\n");
    debug_puts("  Network Secret:  ");
    for (int i = 0; i < 4; i++) {
        debug_puti(cfg->network_secret[i]);
        debug_puts(" ");
    }
    debug_puts("...\n");
    debug_puts("==========================================\n");

    // Confirm
    debug_puts("\nSave to flash? (y/n): ");
    char confirm = 'n';
    serial_read_char(&confirm, 0);
    debug_putc(confirm);
    debug_puts("\n");

    if (confirm == 'y' || confirm == 'Y') {
        config_finalize_crc(cfg);
        if (config_save(cfg) == 0) {
            debug_puts("[CONFIG] Saved! Rebooting...\n");
            HAL_Delay(500);
            NVIC_SystemReset();
        } else {
            debug_puts("[CONFIG] ERROR: Failed to save!\n");
        }
    } else {
        debug_puts("[CONFIG] Not saved. Using runtime config.\n");
    }
}

void led_init(void) {
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET); // LED OFF (Active Low)
}

void led_blink(int times) {
    for (int i = 0; i < times * 2; i++) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(200); // 200ms is more visible
    }
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET); // Ensure OFF
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    debug_init();
    led_init();
    led_blink(3); // TEST BLINK AT STARTUP
    lora_init();

#ifdef USE_USB_CDC
    if (USBD_Init(&hUsbDeviceFS, &VCP_Desc, 0) == USBD_OK) {
        USBD_RegisterClass(&hUsbDeviceFS, &USBD_CDC);
        USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fopsFS);
        USBD_Start(&hUsbDeviceFS);
    }
#endif

    // Node ID and network secret from flash config or wizard
    uint8_t node_id;
    config_t cfg;

#ifndef NODE_ID
#define NODE_ID 0
#endif

#if NODE_ID == 0
    // Universal firmware: read config from flash
    if (config_load(&cfg) == 0) {
        node_id = cfg.node_id;
        mesh_set_network_secret(cfg.network_secret);
        debug_puts("[CONFIG] Loaded from flash: node_id=");
        debug_puti(node_id);
        debug_puts("\n");
    } else {
        // First boot — enter config wizard
        config_wizard(&cfg);
        // wizard saves + reboots, or uses runtime config
        node_id = cfg.node_id;
        mesh_set_network_secret(cfg.network_secret);
    }
#else
    // Fixed NODE_ID from build flags (legacy mode)
    node_id = NODE_ID;
#endif

    mesh_init(node_id);
    debug_puts("\n==========================================\n");
    debug_puts("PRCY Mesh Network v");
    debug_puts(MESH_FW_VERSION);
    debug_puts("\nNode ID: ");
    debug_puti(node_id);
    debug_puts("\n");
#ifdef WORK_AS_LOOPBACK_FOR_NODE_2
    debug_puts("Mode: LOOPBACK (ECHO)\n");
#else
    debug_puts("MODE: SENDER/RELAY\n");
#endif
    debug_puts("==========================================\n");

    mesh_packet_t rx_pkt;
    char cmd_buf[64];
    int cmd_idx = 0;
    uint32_t last_send_cmd_time = 0;
    #define CMD_SEND_COOLDOWN_MS 3000  // min 3s between send/ping commands

    while(1) {
        mesh_tick();

        // 1. Radio Receive
        if (lora_check_receive(&rx_pkt)) {
            if (rx_pkt.dst_id == node_id || rx_pkt.dst_id == 255) {
                if (mesh_process_packet(&rx_pkt)) {
#ifdef WORK_AS_LOOPBACK_FOR_NODE_2
                    handle_loopback(&rx_pkt);
#endif
                }
            } else {
                mesh_process_packet(&rx_pkt);
            }
        }

        // 2. Serial Command Receive
        // Read from USB CDC first (primary), then UART1 (fallback)
        uint8_t c;
        int has_char = 0;
#ifdef USE_USB_CDC
        if (VCP_read(&c, 1) > 0) {
            has_char = 1;
        } else
#endif
        if (HAL_UART_Receive(&huart1, &c, 1, 0) == HAL_OK) {
            has_char = 1;
        }

        if (has_char) {
            // Filter non-printable characters (noise from UART)
            if (c != '\n' && c != '\r' && (c < 0x20 || c > 0x7E)) {
                // Reset buffer on garbage
                cmd_idx = 0;
                continue;
            }
            if (c == '\n' || c == '\r') {
                if (cmd_idx > 0) {
                    cmd_buf[cmd_idx] = '\0';
                    // Strip leading whitespace (USB CDC artifact)
                    char *cmd = cmd_buf;
                    while (*cmd == ' ' || *cmd == '\t') cmd++;
                    if (*cmd == '\0') { cmd_idx = 0; continue; }
                    debug_puts("[CMD] ");
                    debug_puts(cmd);
                    debug_puts("\n");

                    if (cmd[0] == 's' && cmd[1] == ' ') {
                        // s <dst> <msg>
                        uint32_t now = HAL_GetTick();
                        if (now - last_send_cmd_time < CMD_SEND_COOLDOWN_MS) {
                            debug_puts("[CMD] Rate limit: wait ");
                            debug_puti(CMD_SEND_COOLDOWN_MS - (now - last_send_cmd_time));
                            debug_puts("ms\n");
                        } else {
                            int dst_val = atoi(&cmd[2]);
                            char *second_space = strchr(&cmd[2], ' ');
                            if (second_space) {
                                char *msg_ptr = second_space + 1;
                                debug_puts("[CMD] Sending to node ");
                                debug_puti(dst_val);
                                debug_puts(": ");
                                debug_puts(msg_ptr);
                                debug_puts("\n");
                                last_send_cmd_time = now;
                                mesh_send_data((uint8_t)dst_val, (uint8_t*)msg_ptr, strlen(msg_ptr));
                            }
                        }
                    } else if (cmd[0] == 'p' && cmd[1] == ' ') {
                        // p <node> — ping
                        uint32_t now = HAL_GetTick();
                        if (now - last_send_cmd_time < CMD_SEND_COOLDOWN_MS) {
                            debug_puts("[CMD] Rate limit: wait ");
                            debug_puti(CMD_SEND_COOLDOWN_MS - (now - last_send_cmd_time));
                            debug_puts("ms\n");
                        } else {
                            int dst_val = atoi(&cmd[2]);
                            debug_puts("[CMD] Pinging node ");
                            debug_puti(dst_val);
                            debug_puts("\n");
                            last_send_cmd_time = now;
                            mesh_send_data((uint8_t)dst_val, (uint8_t*)"PING", 4);
                        }
                    } else if (cmd[0] == 'd' && cmd[1] == ' ') {
                        // d <node> — initiate DH key exchange
                        int peer_val = atoi(&cmd[2]);
                        debug_puts("[CMD] Initiating DH with node ");
                        debug_puti(peer_val);
                        debug_puts("\n");
                        mesh_init_dh((uint8_t)peer_val);
                    } else if (cmd[0] == 'r') {
                        mesh_print_routes();
                    } else if (cmd[0] == 'i') {
                        mesh_print_stats();
                    } else if (cmd[0] == 'b') {
                        extern uint16_t lora_read_battery(void);
                        debug_puts("[CMD] Battery: ");
                        debug_puti(lora_read_battery());
                        debug_puts(" mV\n");
                    } else if (cmd[0] == 'm') {
                        // Memory info
                        extern char _ebss, _end;
                        uint32_t sp;
                        __asm volatile("mov %0, sp" : "=r"(sp));
                        debug_puts("\n=== MEMORY ===\n");
                        debug_puts("  Stack ptr: 0x"); debug_puti(sp); debug_puts("\n");
                        debug_puts("  BSS end:   0x"); debug_puti((uint32_t)&_ebss); debug_puts("\n");
                        debug_puts("  RAM end:   0x"); debug_puti((uint32_t)&_end); debug_puts("\n");
                        debug_puts("  Free RAM:  "); debug_puti((uint32_t)&_end - sp); debug_puts(" bytes\n");
                        debug_puts("=============\n\n");
                    } else if (cmd[0] == 'f') {
                        // Flash config status
                        config_t fcfg;
                        if (config_load(&fcfg) == 0) {
                            debug_puts("[CMD] Flash config: VALID\n");
                            debug_puts("  Node ID: "); debug_puti(fcfg.node_id); debug_puts("\n");
                            debug_puts("  Secret:  ");
                            for (int si = 0; si < 4; si++) {
                                debug_puti(fcfg.network_secret[si]);
                                debug_puts(" ");
                            }
                            debug_puts("...\n");
                        } else {
                            debug_puts("[CMD] Flash config: EMPTY\n");
                        }
                    } else if (cmd[0] == 'v') {
                        debug_puts("\n=== FIRMWARE INFO ===\n");
                        debug_puts("  Version:   "); debug_puts(MESH_FW_VERSION); debug_puts("\n");
                        debug_puts("  Node ID:   "); debug_puti(node_id); debug_puts("\n");
                        debug_puts("  Build:     "); debug_puts(__DATE__); debug_puts(" "); debug_puts(__TIME__); debug_puts("\n");
                        // Reset source
                        uint32_t csr = RCC->CSR;
                        debug_puts("  Reset:     ");
                        if (csr & RCC_CSR_LPWRRSTF) debug_puts("LPWR ");
                        if (csr & RCC_CSR_WWDGRSTF) debug_puts("WWDG ");
                        if (csr & RCC_CSR_IWDGRSTF) debug_puts("IWDG ");
                        if (csr & RCC_CSR_SFTRSTF) debug_puts("SFT ");
                        if (csr & RCC_CSR_PORRSTF) debug_puts("POR ");
                        if (csr & RCC_CSR_PINRSTF) debug_puts("PIN ");
                        debug_puts("\n");
                        RCC->CSR |= RCC_CSR_RMVF;  // clear reset flags
                        debug_puts("====================\n\n");
                    } else if (cmd[0] == 'c') {
                        // Re-enter config mode
                        debug_puts("[CMD] Entering config mode...\n");
                        config_wizard(&cfg);
                        node_id = cfg.node_id;
                        mesh_set_network_secret(cfg.network_secret);
                    } else if (cmd[0] == 'h') {
                        debug_puts("\n=== COMMANDS ===\n");
                        debug_puts("  s <dst> <msg>  Send message to node\n");
                        debug_puts("  p <dst>        Ping node\n");
                        debug_puts("  d <peer>       Initiate DH key exchange\n");
                        debug_puts("  c              Config mode (node ID, secret)\n");
                        debug_puts("  r              Show routing table\n");
                        debug_puts("  i              Show statistics\n");
                        debug_puts("  b              Show battery voltage\n");
                        debug_puts("  m              Show memory usage\n");
                        debug_puts("  f              Show Flash config status\n");
                        debug_puts("  v              Show firmware version\n");
                        debug_puts("  h              Show this help\n");
                        debug_puts("================\n\n");
                    } else {
                        debug_puts("[CMD] Unknown command. Type 'h' for help.\n");
                    }
                    cmd_idx = 0;
                }
            } else if (cmd_idx < 60) {
                cmd_buf[cmd_idx++] = c;
            }
        }
        
        HAL_Delay(1);
    }
}
