# Криптографическая архитектура Mesh-сети

## Обзор

Система использует двухуровневое шифрование для защиты данных:

1. **Link-layer encryption** — защита от внешних наблюдателей
2. **End-to-end encryption** — защита от промежуточных узлов

## Алгоритм: Кузнечик (GOST R 34.12-2015)

- Блочный шифр, размер блока: 128 бит (16 байт)
- Размер ключа: 256 бит (32 байта)
- Режим работы: **CTR (Counter Mode)** для потокового шифрования

### Почему CTR?

- Не требует padding
- Одна и та же функция для шифрования и дешифрования
- Параллелизуемый
- Использует timestamp как nonce для уникальности

## Архитектура ключей

### 1. Master Key (предустановленный)

```c
static uint8_t master_key[32] = "MASTER_KEY_2026_STAY_SAFE_!!!!!!"
```

Используется для:
- Шифрования session key при ротации
- Начальной инициализации session_crypto

### 2. Session Key (динамический, link-layer)

- Общий для всех узлов сети
- Защищает пакеты от внешних наблюдателей
- Может ротироваться через `mesh_rotate_session_key()`
- Промежуточные узлы могут читать содержимое

### 3. Pairwise Keys (парные, E2E)

- Уникальный ключ для каждой пары узлов
- Устанавливается через `mesh_set_pairwise_key(peer_id, key)`
- Промежуточные узлы **НЕ МОГУТ** читать содержимое
- Предустановленные (pre-shared keys)

## Структура пакета

```c
typedef struct {
    uint8_t  src_id;
    uint8_t  dst_id;
    uint8_t  type;
    uint8_t  ttl;
    uint32_t timestamp;
    uint8_t  payload_len;
    uint8_t  e2e_encrypted;  // 1 = E2E, 0 = link-only
    uint8_t  payload[64];
    uint16_t mic;            // Message Integrity Code
} mesh_packet_t;
```

## Процесс отправки сообщения

### Сценарий 1: Link-layer encryption (нет парного ключа)

```
Node1 -> Node2 -> Node3
  |       |        |
  |       |        └─ Может читать
  |       └─ Может читать
  └─ Отправитель
```

1. Узел 1 шифрует payload на `session_crypto`
2. Устанавливает `e2e_encrypted = 0`
3. Промежуточные узлы могут дешифровать и читать

### Сценарий 2: End-to-end encryption (есть парный ключ)

```
Node1 -> Node2 -> Node3
  |       |        |
  |       |        └─ Может читать (получатель)
  |       └─ НЕ может читать (только пересылает)
  └─ Отправитель
```

1. Узел 1 шифрует payload на `pairwise_keys[3]`
2. Устанавливает `e2e_encrypted = 1`
3. Узел 2 пересылает, но не может дешифровать
4. Узел 3 дешифрует на своем `pairwise_keys[1]`

## Процесс получения сообщения

```c
if (pkt->e2e_encrypted && pairwise_keys_set[pkt->src_id]) {
    // E2E: используем парный ключ
    mesh_crypt_ctr(&pairwise_keys[pkt->src_id], pkt->timestamp, 
                   pkt->payload, pkt->payload_len);
} else {
    // Link-layer: используем session key
    mesh_crypt_ctr(&session_crypto, pkt->timestamp, 
                   pkt->payload, pkt->payload_len);
}
```

## Настройка парных ключей

### Firmware (C):

```c
void setup_keys(void) {
    // Узел 1 устанавливает ключ для связи с узлом 3
    uint8_t key_1_3[32] = "NODE1_TO_NODE3_PAIRWISE_KEY!!!!";
    mesh_set_pairwise_key(3, key_1_3);
}
```

### Simulation (Python):

```python
# Узел 1
node1.set_pairwise_key("NODE3", b"NODE1_TO_NODE3_PAIRWISE_KEY!!!!")

# Узел 3 (должен использовать тот же ключ)
node3.set_pairwise_key("NODE1", b"NODE1_TO_NODE3_PAIRWISE_KEY!!!!")
```

## Ротация Session Key

Только мастер-узел (обычно узел 1 или gateway) может инициировать:

```c
uint8_t new_session_key[32] = "NEW_SESSION_KEY_2026_SECURE!!!!";
mesh_rotate_session_key(new_session_key);
```

Процесс:
1. Новый ключ шифруется на master_key
2. Отправляется broadcast пакет `PACKET_TYPE_KEY_ROTATION`
3. Все узлы получают, дешифруют и обновляют `session_crypto`

## Защита от атак

### Replay Protection

- Каждый пакет содержит `timestamp`
- Узлы отклоняют пакеты с `timestamp <= last_timestamps[src_id]`
- Окно приема: 60 секунд (для компенсации рассинхронизации)

### Nonce в CTR режиме

- Используется `timestamp` как nonce
- Гарантирует уникальность keystream для каждого пакета
- Даже при повторной отправке того же сообщения

### Message Integrity Code (MIC)

**TODO**: Пока не реализовано, см. задачу #3

## Пример использования

### Отправка приватного сообщения (E2E):

```c
// На узле 1: установить парный ключ для узла 5
uint8_t key[32] = "SECRET_KEY_NODE1_NODE5_2026!!!!";
mesh_set_pairwise_key(5, key);

// Отправить сообщение
mesh_send_data(5, (uint8_t*)"Private message", 15);
// Автоматически использует E2E если ключ установлен
```

### Отправка публичного сообщения (Link-layer):

```c
// Не устанавливаем парный ключ
mesh_send_data(5, (uint8_t*)"Public message", 14);
// Использует session_crypto, все узлы могут читать
```

## Ограничения текущей реализации

1. **Парные ключи предустановлены** — нет динамического обмена (Diffie-Hellman)
2. **Нет forward secrecy** — компрометация ключа раскрывает всю историю
3. **MIC не реализован** — нет защиты от подделки пакетов
4. **Ротация session key вручную** — нет автоматической политики

## Будущие улучшения

1. Реализовать MIC на базе HMAC или CMAC
2. Добавить Diffie-Hellman для динамического обмена ключами
3. Реализовать forward secrecy через периодическую ротацию
4. Добавить certificate-based authentication
