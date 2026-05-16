
# PlatformIO Project for STM32F103C8T6 (Blue Pill)

This project is tailored for PlatformIO in VS Code. It implements a secure Mesh network over LoRa using STM32F103C8T6 and DRF1278F (SX1278) modules.

## Hardware Connections (Blue Pill)

### LoRa (SX1278)
- **NSS**: PA4
- **SCK**: PA5
- **MISO**: PA6
- **MOSI**: PA7
- **RESET**: PB0
- **DIO0**: PB1 (currently used for polling, but can be interrupt-driven)
- **VCC**: 3.3V
- **GND**: GND

### Debug UART (USB-TTL)
- **TX**: PA9
- **RX**: PA10
- **Baudrate**: 115200

## How to use

1.  Open this folder in VS Code with the PlatformIO extension installed.
2.  The `platformio.ini` is configured for the `bluepill_f103c8` board.
3.  To flash Node 1 (Sender):
    -   Ensure `WORK_AS_LOOPBACK_FOR_NODE_2` is NOT defined in `build_flags` (or comment it out).
    -   Build and Upload.
4.  To flash Node 2 (Loopback):
    -   Ensure `-DWORK_AS_LOOPBACK_FOR_NODE_2` is present in `build_flags` in `platformio.ini`.
    -   Build and Upload.
5.  Use a Serial Monitor (like PlatformIO's built-in one) on Node 1:
    -   Type `s 2 hello` to send "hello" to Node 2.
    -   Node 2 should receive it and echo it back.

## Key Features
- **Kuznyechik Encryption**: E2E and Link-layer.
- **AODV Routing**: Automatic route discovery.
- **SX1278 Driver**: Native HAL-based SPI driver.
- **Loopback**: Node 2 automatically echoes data back to the sender.
