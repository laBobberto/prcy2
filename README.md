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
│  ├─ PING/PONG connectivity test               │
│  ├─ Heartbeat broadcast (30s interval)        │
│  └─ Loopback echo (Node 2)                   │
│                                               │
│           Security Layer                      │
│  ├─ E2E Encryption (Kuznyechik CTR)           │
│  ├─ Link Encryption (Kuznyechik CTR)          │
│  ├─ 32-bit MIC (E2E + Link)                   │
│  ├─ X25519 key exchange + EdDSA signatures    │
│  ├─ DH handshake with timeout & retry         │
│  └─ Replay protection (timestamp filtering)   │
│                                               │
│           Routing Layer (AODV)                │
│  ├─ Route Discovery (RREQ/RREP)               │
│  ├─ Route Error (RERR) propagation            │
│  ├─ Route Table (16 entries with RSSI)        │
│  └─ Link quality monitoring                   │
│                                               │
│           Transport Layer                     │
│  ├─ SX1278 SPI driver                         │
│  ├─ LoRa modulation (SF7/BW125kHz/CR4/5)     │
│  ├─ Listen Before Talk (CAD)                  │
│  └─ RSSI/SNR reporting                        │
│                                               │
│           Power Management                    │
│  ├─ LoRa sleep mode                           │
│  └─ Battery voltage monitoring (ADC)          │
└──────────────────────────────────────────────┘
```

## CLI Commands

Connect via serial (115200 baud) or USB CDC:

```
s <dst> <msg>  Send message to node
p <dst>        Ping node (auto-reply with PONG)
d <peer>       Initiate DH key exchange
r              Show routing table
i              Show statistics (TX/RX/RSSI/routes)
b              Show battery voltage
m              Show memory usage
f              Show Flash config status
v              Show firmware version & reset source
h              Show help
```

### Example Session

```
> s 2 hello world
[CMD] Sending to node 2: hello world

> p 2
[CMD] Pinging node 2
[MESH] PONG received from node 2 (RSSI=-45 dBm)

> i
=== PRCY MESH v1.0.0 ===
  TX packets:      15
  RX packets:      12
  Last RSSI:       -45 dBm
  Routes active:   1/16
    -> node 2 via 2 hops=1 RSSI=-45
=======================
```

## Project Structure

```
firmware/
├── inc/             # Headers
│   ├── mesh.h       # Packet types, routing, protocol defs
│   ├── lora.h       # LoRa driver interface
│   ├── debug.h      # UART/USB debug output
│   ├── config.h     # Flash configuration storage
│   └── ...
├── src/             # Sources
│   ├── main.c       # Entry point, main loop, CLI
│   ├── mesh.c       # Mesh protocol (AODV, crypto, stats)
│   ├── lora.c       # SX1278 SPI driver + LBT
│   ├── debug.c      # Debug output (UART + USB CDC)
│   └── config.c     # Flash config read/write/verify
├── lib/
│   ├── kuznyechik/  # GOST R 34.12-2015 cipher
│   └── crypto/      # X25519, EdDSA, SHA-512, CMAC
├── stm32f103.ld     # Linker script (64K Flash, 20K RAM)
└── Makefile         # Standalone build (optional)
```

## Resource Usage

| Resource | Node 1 | Node 2 |
|----------|--------|--------|
| RAM      | 46.7%  | 46.7%  |
| Flash    | 60.3%  | ~60%   |

## Security Notes

- **Identity keys**: Generated from MCU UID + master key (unique per chip)
- **Key exchange**: X25519 DH with EdDSA authentication
- **Encryption**: GOST Kuznyechik in CTR mode (link + E2E)
- **Integrity**: 32-bit CMAC MIC (link + E2E)
- **Replay protection**: Timestamp-based with outlier rejection
- **Master key**: Currently hardcoded (for development only)
