# Secure Mesh Network with GOST Kuznyechik Encryption

Защищённая mesh-сеть на базе STM32F103C8T6 и SX1278 (XL1278-SMT) с шифрованием ГОСТ Кузьмичёк, AODV-маршрутизацией и проверкой целостности сообщений.

## Hardware

- **MCU**: STM32F103C8T6 (Blue Pill, Cortex-M3, 72 MHz)
- **Radio**: SX1278 (XL1278-SMT), 410–525 MHz, до 20 дБм, дальность до 5 км
- **Debug**: UART1 (PA9/PA10, 115200) + USB CDC

### Wiring (LoRa SX1278)

| Signal | Pin  |
|--------|------|
| NSS    | PA4  |
| SCK    | PA5  |
| MISO   | PA6  |
| MOSI   | PA7  |
| RESET  | PB0  |
| DIO0   | PB1  |
| VCC    | 3.3V |
| GND    | GND  |

## Building

Requires [PlatformIO](https://platformio.org/).

```bash
# Build both nodes
pio run

# Flash Node 1 (sender)
pio run -e node1 -t upload

# Flash Node 2 (loopback/echo)
pio run -e node2 -t upload

# Or use the interactive script
./flash.sh
```

## Architecture

```
┌──────────────────────────────────────────────┐
│           Application Layer                   │
│  ├─ User messages (serial/USB commands)       │
│  └─ Loopback echo (Node 2)                   │
│                                               │
│           Security Layer                      │
│  ├─ E2E Encryption (Kuznyechik CTR)           │
│  ├─ Link Encryption (Kuznyechik CTR)          │
│  ├─ 32-bit MIC (E2E + Link)                   │
│  └─ X25519 key exchange + EdDSA signatures    │
│                                               │
│           Routing Layer (AODV)                │
│  ├─ Route Discovery (RREQ/RREP)               │
│  └─ Route Table (16 entries)                  │
│                                               │
│           Transport Layer                     │
│  ├─ SX1278 SPI driver                         │
│  └─ LoRa modulation (configurable BW/SF)      │
└──────────────────────────────────────────────┘
```

## Usage

Connect to Node 1 via serial (115200 baud):

```
s 2 hello       → send "hello" to Node 2
s 2 test msg    → send "test msg" to Node 2
```

Node 2 (flashed with `-DWORK_AS_LOOPBACK_FOR_NODE_2`) echoes data back to the sender.

## Project Structure

```
firmware/
├── inc/             # Headers
│   ├── mesh.h       # Packet types, routing structs
│   ├── lora.h       # LoRa driver interface
│   ├── debug.h      # UART/USB debug output
│   └── ...
├── src/             # Sources
│   ├── main.c       # Entry point, main loop, commands
│   ├── mesh.c       # Mesh protocol (AODV, crypto, routing)
│   ├── lora.c       # SX1278 SPI driver
│   └── debug.c      # Debug output (UART + USB CDC)
├── lib/
│   ├── kuznyechik/  # GOST R 34.12-2015 cipher
│   └── crypto/      # X25519, EdDSA, SHA-512, CMAC
├── stm32f103.ld     # Linker script (64K Flash, 20K RAM)
└── Makefile         # Standalone build (optional)
```
