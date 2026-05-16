
#include <stdint.h>
#include <string.h>
#include "mesh.h"
#include "debug.h"
#include "stm32f1xx_hal.h"

void lora_init(void);
void lora_send_packet(mesh_packet_t *pkt);
int lora_check_receive(mesh_packet_t *pkt);

void SysTick_Handler(void) {
    HAL_IncTick();
}

void SystemClock_Config(void) {
    // Basic HSI clock config for F103
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL12; // 4MHz * 12 = 48MHz
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1);
}

// Loopback function for node 2
void handle_loopback(mesh_packet_t *rx_pkt) {
    if (rx_pkt->type == PACKET_TYPE_DATA && rx_pkt->dst_id == 2) {
        debug_puts("[LOOPBACK] Echoing data back to ");
        debug_puti(rx_pkt->src_id);
        debug_puts("\n");
        
        // Decrypt if needed, but here we can just send it back as is (it will be re-encrypted by mesh_send_data)
        // Or we can just swap src/dst and send. 
        // Better use mesh_send_data to ensure proper sequence numbers/MICs.
        
        // Extract plain text if possible (for debug)
        char echo_buf[MAX_PAYLOAD_SIZE];
        memcpy(echo_buf, rx_pkt->payload, rx_pkt->payload_len);
        
        mesh_send_data(rx_pkt->src_id, (uint8_t*)echo_buf, rx_pkt->payload_len);
    }
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    debug_init();
    lora_init();

    // In a real scenario, node_id could be read from DIP switches or Flash
    // For now, let's assume node 1 is the sender and node 2 is the loopback
    // You can change this per board before flashing
    uint8_t node_id = 1; 
#ifdef WORK_AS_LOOPBACK_FOR_NODE_2
    node_id = 2;
#endif

    mesh_init(node_id);
    debug_puts("\n--- MESH NODE ");
    debug_puti(node_id);
    debug_puts(" STARTED ---\n");

    mesh_packet_t rx_pkt;
    char cmd_buf[64];
    int cmd_idx = 0;

    while(1) {
        mesh_tick();

        // 1. Radio Receive
        if (lora_check_receive(&rx_pkt)) {
            if (rx_pkt.dst_id == node_id || rx_pkt.dst_id == 255) {
                if (node_id == 2) {
                    handle_loopback(&rx_pkt);
                }
                mesh_process_packet(&rx_pkt);
            } else {
                // Relay logic is inside mesh_process_packet usually, 
                // but let's call it to handle routing
                mesh_process_packet(&rx_pkt);
            }
        }

        // 2. Serial Command Receive (for node 1 to send data)
        // Use non-blocking check for UART1
        uint8_t c;
        extern UART_HandleTypeDef huart1;
        if (HAL_UART_Receive(&huart1, &c, 1, 0) == HAL_OK) {
            if (c == '\n' || c == '\r') {
                cmd_buf[cmd_idx] = '\0';
                if (cmd_idx > 0 && cmd_buf[0] == 's') {
                    // s <dst> <msg>
                    int dst = cmd_buf[2] - '0';
                    char *msg = &cmd_buf[4];
                    mesh_send_data((uint8_t)dst, (uint8_t*)msg, strlen(msg));
                }
                cmd_idx = 0;
            } else if (cmd_idx < 60) {
                cmd_buf[cmd_idx++] = c;
            }
        }
        
        HAL_Delay(1);
    }
}
