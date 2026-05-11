# Message Integrity Code (MIC) в Mesh-сети

## Обзор

Система использует **двойной MIC** для максимальной защиты:

1. **E2E MIC** — защита end-to-end сообщений (только отправитель и получатель могут проверить)
2. **Link MIC** — защита от подделки в эфире (все узлы могут проверить)

## Архитектура

```
┌─────────────────────────────────────────────────────────────┐
│                    Двойной MIC                               │
│                                                              │
│  E2E MIC (pairwise_key)                                     │
│  ├─ Вычисляется: src_id, dst_id, type, timestamp,          │
│  │                payload_len, encrypted_payload            │
│  ├─ Проверяется: только получателем                         │
│  └─ Защищает: содержимое сообщения от подделки             │
│                                                              │
│  Link MIC (session_key)                                     │
│  ├─ Вычисляется: src_id, dst_id, type, timestamp,          │
│  │                payload_len, e2e_encrypted,               │
│  │                encrypted_payload, e2e_mic                │
│  ├─ Проверяется: всеми узлами (отправитель, relay,         │
│  │                получатель)                               │
│  └─ Защищает: весь пакет от подделки в эфире               │
└─────────────────────────────────────────────────────────────┘
```

## Структура пакета

```c
typedef struct {
    uint8_t  src_id;           // ID отправителя
    uint8_t  dst_id;           // ID получателя
    uint8_t  type;             // Тип пакета
    uint8_t  ttl;              // Time To Live (НЕ входит в MIC!)
    uint32_t timestamp;        // Временная метка
    uint8_t  payload_len;      // Длина payload
    uint8_t  e2e_encrypted;    // 1 = E2E, 0 = link-only
    uint8_t  payload[64];      // Зашифрованные данные
    uint16_t e2e_mic;          // E2E MIC (16 бит)
    uint16_t link_mic;         // Link MIC (16 бит)
} mesh_packet_t;
```

**Важно:** TTL НЕ входит в Link MIC, так как он меняется при каждом hop.

## Алгоритм MIC

Используется **HMAC на базе Kuznyechik** (упрощенная версия):

```c
void kuznyechik_mac(kuznyechik_ctx_t *ctx, const uint8_t *data, 
                    size_t len, uint8_t *mac) {
    uint8_t state[16] = {0};
    
    // CBC-MAC режим
    for (size_t i = 0; i < len; i += 16) {
        uint8_t block[16] = {0};
        memcpy(block, data + i, min(16, len - i));
        
        // XOR с предыдущим состоянием
        for (int j = 0; j < 16; j++) {
            state[j] ^= block[j];
        }
        
        // Шифруем
        kuznyechik_encrypt_block(ctx, state, state);
    }
    
    memcpy(mac, state, 16);  // Берем первые 16 байт
}
```

MIC = первые 2 байта от MAC (16 бит).

## Процесс отправки сообщения

### 1. Подготовка данных

```c
mesh_packet_t pkt;
pkt.src_id = self_node_id;
pkt.dst_id = dst_id;
pkt.type = PACKET_TYPE_DATA;
pkt.timestamp = internal_clock;
pkt.payload_len = (len + 15) & ~15;  // Выравнивание до 16 байт

memcpy(pkt.payload, data, len);
```

### 2. E2E шифрование (если есть парный ключ)

```c
if (pairwise_keys_set[dst_id]) {
    // Вычисляем E2E MIC ДО шифрования
    pkt.e2e_mic = compute_e2e_mic(&pkt, &pairwise_keys[dst_id]);
    
    // Шифруем payload
    mesh_crypt_ctr(&pairwise_keys[dst_id], pkt.timestamp, 
                   pkt.payload, pkt.payload_len);
    pkt.e2e_encrypted = 1;
} else {
    // Link-layer шифрование
    pkt.e2e_mic = 0;
    mesh_crypt_ctr(&session_crypto, pkt.timestamp, 
                   pkt.payload, pkt.payload_len);
    pkt.e2e_encrypted = 0;
}
```

### 3. Link MIC

```c
// Вычисляем Link MIC от всего пакета (включая e2e_mic)
pkt.link_mic = compute_link_mic(&pkt);

// Отправляем
lora_send_packet(&pkt);
```

## Процесс получения сообщения

### Relay узел (промежуточный)

```c
void mesh_process_packet(mesh_packet_t *pkt) {
    // 1. Проверяем Link MIC
    uint16_t expected_link_mic = compute_link_mic(pkt);
    if (pkt->link_mic != expected_link_mic) {
        debug_puts("[SECURITY] Link MIC verification FAILED!\n");
        return;  // Отклоняем пакет
    }
    
    // 2. Если не для нас — пересылаем
    if (pkt->dst_id != self_node_id) {
        pkt->ttl--;
        lora_send_packet(pkt);
        return;
    }
    
    // 3. Для нас — проверяем E2E MIC и дешифруем
    // (см. ниже)
}
```

### Получатель (конечный узел)

```c
if (pkt->dst_id == self_node_id && pkt->type == PACKET_TYPE_DATA) {
    // 1. Link MIC уже проверен выше
    
    // 2. Проверяем E2E MIC (если E2E шифрование)
    if (pkt->e2e_encrypted && pairwise_keys_set[pkt->src_id]) {
        uint16_t expected_e2e_mic = compute_e2e_mic(pkt, &pairwise_keys[pkt->src_id]);
        if (pkt->e2e_mic != expected_e2e_mic) {
            debug_puts("[SECURITY] E2E MIC verification FAILED!\n");
            return;
        }
        
        // Дешифруем на парном ключе
        mesh_crypt_ctr(&pairwise_keys[pkt->src_id], pkt->timestamp, 
                       pkt->payload, pkt->payload_len);
    } else {
        // Дешифруем на session key
        mesh_crypt_ctr(&session_crypto, pkt->timestamp, 
                       pkt->payload, pkt->payload_len);
    }
    
    // Обрабатываем сообщение
    process_message(pkt->payload, pkt->payload_len);
}
```

## Вычисление MIC

### E2E MIC

```c
static uint16_t compute_e2e_mic(mesh_packet_t *pkt, kuznyechik_ctx_t *key) {
    uint8_t mac_data[128];
    int offset = 0;
    
    mac_data[offset++] = pkt->src_id;
    mac_data[offset++] = pkt->dst_id;
    mac_data[offset++] = pkt->type;
    memcpy(mac_data + offset, &pkt->timestamp, 4);
    offset += 4;
    mac_data[offset++] = pkt->payload_len;
    memcpy(mac_data + offset, pkt->payload, pkt->payload_len);
    offset += pkt->payload_len;
    
    uint8_t mac[16];
    kuznyechik_mac(key, mac_data, offset, mac);
    
    return (mac[0] << 8) | mac[1];  // Первые 2 байта
}
```

### Link MIC

```c
static uint16_t compute_link_mic(mesh_packet_t *pkt) {
    uint8_t mac_data[128];
    int offset = 0;
    
    mac_data[offset++] = pkt->src_id;
    mac_data[offset++] = pkt->dst_id;
    mac_data[offset++] = pkt->type;
    // TTL НЕ включаем!
    memcpy(mac_data + offset, &pkt->timestamp, 4);
    offset += 4;
    mac_data[offset++] = pkt->payload_len;
    mac_data[offset++] = pkt->e2e_encrypted;
    memcpy(mac_data + offset, pkt->payload, pkt->payload_len);
    offset += pkt->payload_len;
    memcpy(mac_data + offset, &pkt->e2e_mic, 2);
    offset += 2;
    
    uint8_t mac[16];
    kuznyechik_mac(&session_crypto, mac_data, offset, mac);
    
    return (mac[0] << 8) | mac[1];
}
```

## Защита от атак

### 1. Подделка payload

**Атака:** Злоумышленник перехватывает пакет и меняет payload.

**Защита:**
- Link MIC не совпадет → relay узел отклонит пакет
- Даже если Link MIC подделан, E2E MIC не совпадет → получатель отклонит

### 2. Replay атака

**Атака:** Злоумышленник повторно отправляет старый пакет.

**Защита:**
- Timestamp проверяется: `if (pkt->timestamp <= last_timestamps[src_id]) return;`
- Duplicate suppression: `seen_packets` отклоняет дубликаты

### 3. Man-in-the-middle

**Атака:** Промежуточный узел пытается прочитать E2E сообщение.

**Защита:**
- Промежуточный узел не имеет pairwise_key
- Может проверить только Link MIC, но не может дешифровать payload
- E2E MIC гарантирует, что только получатель может проверить целостность

### 4. Подделка отправителя

**Атака:** Злоумышленник отправляет пакет от имени другого узла.

**Защита:**
- Не имеет pairwise_key → не может вычислить правильный E2E MIC
- Не имеет session_key → не может вычислить правильный Link MIC

## Тестирование

### Тест 1: Валидное сообщение

```python
# Отправляем корректное E2E сообщение
msg = b"Secret message"
encrypted = node1.pairwise_keys["NODE3"].ctr_crypt(timestamp, msg)

pkt = {
    "src": "NODE1",
    "dst": "NODE3",
    "payload": base64.b64encode(encrypted).decode(),
    "e2e_encrypted": True
}

pkt['e2e_mic'] = node1.compute_e2e_mic(pkt, node1.pairwise_keys["NODE3"])
pkt['link_mic'] = node1.compute_link_mic(pkt)

# Результат: ✓ NODE3 получает и дешифрует сообщение
```

### Тест 2: Подделанное сообщение

```python
# Вычисляем MIC
pkt['e2e_mic'] = compute_e2e_mic(pkt)
pkt['link_mic'] = compute_link_mic(pkt)

# ПОДДЕЛКА: меняем payload после вычисления MIC
tampered = bytearray(base64.b64decode(pkt['payload']))
tampered[0] ^= 0xFF
pkt['payload'] = base64.b64encode(bytes(tampered)).decode()

# Результат: ✗ NODE2 отклоняет (Link MIC mismatch)
```

### Результаты тестов

```
Test 1: Valid E2E message with correct MIC
✓ NODE3 received and decrypted message

Test 2: Tampered message (modified payload)
✗ NODE2 rejected (Link MIC verification FAILED)
```

## Производительность

### Размер MIC

- E2E MIC: 2 байта (16 бит)
- Link MIC: 2 байта (16 бит)
- **Overhead:** 4 байта на пакет

### Вычислительная сложность

- **Отправка:** 2 вычисления MAC (E2E + Link)
- **Relay:** 1 проверка MAC (Link)
- **Получение:** 2 проверки MAC (Link + E2E)

### Время вычисления (STM32F4 @ 168MHz)

- Kuznyechik encrypt block: ~500 циклов
- MAC для 64-байт payload: ~2000 циклов (~12 мкс)
- **Общее время:** ~24 мкс на отправку, ~12 мкс на relay, ~24 мкс на получение

## Ограничения

1. **16-битный MIC** — вероятность коллизии 1/65536
   - Для критичных приложений можно увеличить до 32 бит
   
2. **Нет аутентификации узлов** — любой узел с session_key может отправлять
   - Решение: добавить цифровые подписи

3. **TTL не защищен** — злоумышленник может изменить TTL
   - Не критично, так как это только влияет на пересылку

## Будущие улучшения

1. **Увеличить MIC до 32 бит** для критичных приложений
2. **Добавить цифровые подписи** для аутентификации узлов
3. **AEAD режим** (Authenticated Encryption with Associated Data)
4. **Защита TTL** — включить в Link MIC с пересчетом на каждом hop

## API

### C (Firmware)

```c
// Вычисление MIC (внутренние функции)
static uint16_t compute_e2e_mic(mesh_packet_t *pkt, kuznyechik_ctx_t *key);
static uint16_t compute_link_mic(mesh_packet_t *pkt);

// Отправка с автоматическим вычислением MIC
void mesh_send_data(uint8_t dst_id, const uint8_t *data, uint8_t len);

// Обработка с автоматической проверкой MIC
void mesh_process_packet(mesh_packet_t *pkt);
```

### Python (Simulation)

```python
# Вычисление MIC
e2e_mic = node.compute_e2e_mic(packet, key_crypto)
link_mic = node.compute_link_mic(packet)

# Отправка с MIC
packet['e2e_mic'] = e2e_mic
packet['link_mic'] = link_mic
node.send_packet(packet)
```

## Пример использования

### Отправка E2E сообщения

```c
// Установить парный ключ
uint8_t key[32] = "NODE1_TO_NODE3_SECRET_2026!!!!!!";
mesh_set_pairwise_key(3, key);

// Отправить сообщение (MIC вычисляется автоматически)
mesh_send_data(3, (uint8_t*)"Secret message", 14);
```

### Получение сообщения

```c
// В main loop
mesh_packet_t rx_pkt;
if (lora_check_receive(&rx_pkt)) {
    // MIC проверяется автоматически
    mesh_process_packet(&rx_pkt);
}
```

Если MIC не совпадает, пакет автоматически отклоняется с выводом в debug.
