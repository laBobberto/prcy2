
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "mesh.h"
#include "lora.h"
#include "debug.h"
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
extern int VCP_read(uint8_t* buf, uint16_t len);
#endif

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

    // Use NODE_ID from build flags, default to 1
#ifndef NODE_ID
#define NODE_ID 1
#endif
    uint8_t node_id = NODE_ID;

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

        // 2. Serial Command Receive (for node 1 to send data)
        // Use non-blocking check for UART1 and USB
        uint8_t c;
        int has_char = 0;
        if (HAL_UART_Receive(&huart1, &c, 1, 0) == HAL_OK) {
            has_char = 1;
        }
#ifdef USE_USB_CDC
        else if (VCP_read(&c, 1) > 0) {
            has_char = 1;
        }
#endif

        if (has_char) {
            if (c == '\n' || c == '\r') {
                if (cmd_idx > 0) {
                    cmd_buf[cmd_idx] = '\0';
                    debug_puts("[CMD] Processing: ");
                    debug_puts(cmd_buf);
                    debug_puts("\n");

                    if (cmd_buf[0] == 's' && cmd_buf[1] == ' ') {
                        // s <dst> <msg>
                        int dst_val = atoi(&cmd_buf[2]);
                        char *second_space = strchr(&cmd_buf[2], ' ');
                        if (second_space) {
                            char *msg_ptr = second_space + 1;
                            debug_puts("[CMD] Sending to node ");
                            debug_puti(dst_val);
                            debug_puts(": ");
                            debug_puts(msg_ptr);
                            debug_puts("\n");
                            mesh_send_data((uint8_t)dst_val, (uint8_t*)msg_ptr, strlen(msg_ptr));
                        }
                    } else if (cmd_buf[0] == 'i') {
                        mesh_print_stats();
                    } else if (cmd_buf[0] == 'h') {
                        debug_puts("\n=== COMMANDS ===\n");
                        debug_puts("  s <dst> <msg>  Send message to node\n");
                        debug_puts("  i              Show statistics\n");
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
