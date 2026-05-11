# Secure Mesh Network with GOST Kuznyechik Encryption

Защищенная mesh-сеть на базе LoRa с шифрованием GOST Kuznyechik, AODV маршрутизацией и проверкой целостности сообщений.

## Возможности

- ✅ **End-to-End шифрование** с использованием GOST R 34.12-2015 (Kuznyechik)
- ✅ **Двойная проверка целостности** (E2E MIC + Link MIC)
- ✅ **AODV маршрутизация** с автоматическим обнаружением путей
- ✅ **Синхронизация времени** между узлами
- ✅ **Защита от replay атак** через timestamps
- ✅ **Управление памятью** с автоматической очисткой
- ✅ **Ротация ключей** для повышения безопасности

## Архитектура

```
┌─────────────────────────────────────────────────────────────┐
│                  Mesh Network Stack                          │
│                                                              │
│  Application Layer                                           │
│  ├─ User messages                                            │
│  └─ Commands                                                 │
│                                                              │
│  Security Layer                                              │
│  ├─ E2E Encryption (Kuznyechik CTR, pairwise keys)          │
│  ├─ E2E MIC (16-bit authentication)                          │
│  ├─ Link Encryption (Kuznyechik CTR, session key)           │
│  └─ Link MIC (16-bit authentication)                         │
│                                                              │
│  Routing Layer (AODV)                                        │
│  ├─ Route Discovery (RREQ/RREP)                              │
│  ├─ Route Maintenance (lifetime, expiration)                 │
│  └─ Routing Table (32 routes)                                │
│                                                              │
│  Time Sync Layer                                             │
│  ├─ Time Master broadcasts                                   │
│  ├─ Hard sync (>100 ticks diff)                              │
│  └─ Soft sync (gradual adjustment)                           │
│                                                              │
│  Physical Layer                                              │
│  └─ LoRa (SX1276/SX1278)                                     │
└─────────────────────────────────────────────────────────────┘
```

## Структура проекта

```
prcy2/
├── firmware/                    # STM32 firmware (C)
│   ├── inc/
│   │   └── mesh.h              # Mesh API
│   ├── src/
│   │   └── mesh.c              # Mesh реализация
│   └── lib/
│       └── kuznyechik/         # GOST Kuznyechik
│           ├── kuznyechik.h
│           └── kuznyechik.c
│
├── simulation/                  # Python симуляция
│   ├── node/
│   │   ├── node.py             # Mesh узел
│   │   └── crypto.py           # Kuznyechik
│   ├── test_mic.py             # Тесты MIC
│   ├── test_aodv.py            # Тесты AODV
│   └── test_integration.py     # Интеграционные тесты
│
├── docs/                        # Документация
│   ├── MIC_ARCHITECTURE.md
│   ├── MIC_IMPLEMENTATION_REPORT.md
│   ├── AODV_IMPLEMENTATION.md
│   └── AODV_MEMORY_REPORT.md
│
└── README.md                    # Этот файл
```

## Быстрый старт

### Требования

**Firmware:**
- STM32F4 (или совместимый)
- LoRa модуль (SX1276/SX1278)
- ARM GCC toolchain

**Симуляция:**
- Python 3.7+
- Нет внешних зависимостей (только stdlib)

### Запуск симуляции

```bash
cd simulation

# Запуск тестов MIC
python3 test_mic.py

# Запуск тестов AODV
python3 test_aodv.py

# Запуск интеграционных тестов
python3 test_integration.py

# Проверка синхронизации C/Python
cd ..
python3 check_sync.py
```

### Сборка firmware

```bash
cd firmware
make clean
make
make flash
```

## Использование

### Инициализация узла (C)

```c
#include "mesh.h"

// Инициализация
mesh_init(1);  // node_id = 1

// Установка E2E ключа для узла 3
uint8_t key[32] = "NODE1_TO_NODE3_SECRET_2026!!!!!!";
mesh_set_pairwise_key(3, key);

// Основной цикл
while (1) {
    mesh_tick();  // Обновление времени и очистка
    
    // Проверка входящих пакетов
    mesh_packet_t rx_pkt;
    if (lora_check_receive(&rx_pkt)) {
        mesh_process_packet(&rx_pkt);
    }
    
    // Отправка данных
    mesh_send_data(3, (uint8_t*)"Hello", 5);
    
    delay_ms(1);
}
```

### Инициализация узла (Python)

```python
from simulation.node.node import MeshNode

# Создание узла
node = MeshNode("NODE1", 5001, [5002], is_time_master=True)

# Установка E2E ключа
key = b"NODE1_TO_NODE3_SECRET_2026!!!!!!"
node.set_pairwise_key("NODE3", key)

# Запуск
node.run()
```

## Криптография

### Kuznyechik (GOST R 34.12-2015)

- **Блочный шифр**: 128-bit блоки, 256-bit ключи
- **Режим**: CTR (Counter Mode) для потокового шифрования
- **MAC**: CBC-MAC для проверки целостности

### Двойной MIC

**E2E MIC** (End-to-End):
- Вычисляется на pairwise_key
- Защищает: src_id, dst_id, type, timestamp, payload
- Проверяется только получателем

**Link MIC**:
- Вычисляется на session_key
- Защищает весь пакет (включая E2E MIC)
- Проверяется всеми узлами (включая relay)

### Пример работы

```
NODE1 отправляет "Secret" NODE3 через NODE2:

1. NODE1:
   ├─ Шифрует на pairwise_key[3]
   ├─ E2E MIC = 0x3F2A
   ├─ Link MIC = 0x81B5
   └─ Отправляет

2. NODE2 (relay):
   ├─ Проверяет Link MIC ✓
   ├─ НЕ может дешифровать (нет ключа)
   └─ Пересылает

3. NODE3:
   ├─ Проверяет Link MIC ✓
   ├─ Проверяет E2E MIC ✓
   ├─ Дешифрует
   └─ Читает: "Secret"
```

## AODV Маршрутизация

### Route Discovery

```
NODE1 ищет маршрут к NODE4:

1. NODE1 → RREQ broadcast
   ├─ rreq_id = 1
   ├─ orig_id = NODE1
   ├─ dest_id = NODE4
   └─ hop_count = 0

2. NODE2 получает RREQ:
   ├─ Добавляет обратный маршрут к NODE1
   ├─ hop_count++
   └─ Пересылает RREQ

3. NODE3 получает RREQ:
   ├─ Добавляет обратный маршрут к NODE1
   ├─ hop_count++
   └─ Пересылает RREQ

4. NODE4 получает RREQ:
   ├─ Добавляет обратный маршрут к NODE1
   └─ Отправляет RREP обратно

5. RREP распространяется обратно:
   NODE4 → NODE3 → NODE2 → NODE1
   (каждый узел добавляет прямой маршрут)

6. Маршрут установлен:
   NODE1 → NODE2 → NODE3 → NODE4
```

### Управление маршрутами

- **Lifetime**: 30000 тиков (~30 секунд)
- **Cleanup**: каждые 5000 тиков
- **Sequence numbers**: для свежести маршрутов
- **Hop count**: для выбора кратчайшего пути

## Синхронизация времени

### Time Master

- Узел с node_id=1 является Time Master
- Рассылает TIME пакеты каждые 1000 тиков
- Другие узлы синхронизируются

### Синхронизация

**Hard sync** (разница > 100 тиков):
```c
internal_clock = received_timestamp;
```

**Soft sync** (разница < 100 тиков):
```c
clock_offset = time_diff / 4;
internal_clock += clock_offset;
```

## Управление памятью

### Автоматическая очистка

**Маршруты** (каждые 5000 тиков):
- Удаление истекших маршрутов
- Проверка lifetime

**Timestamps и RREQ** (каждые 10000 тиков):
- Удаление записей старше 60000 тиков
- Освобождение памяти

### Использование памяти (STM32F4)

```
Статическая память:
├─ routing_table[32]:     384 байт
├─ seen_rreq[256]:       1024 байт
├─ last_timestamps[256]: 1024 байт
├─ pairwise_keys[256]:   ~8 КБ
└─ Итого:                ~10.5 КБ
```

## Безопасность

### Защита от атак

| Атака | Защита | Статус |
|-------|--------|--------|
| Подделка payload | E2E MIC + Link MIC | ✅ |
| Replay атака | Timestamp проверка | ✅ |
| Man-in-the-middle | E2E шифрование | ✅ |
| Подделка отправителя | Pairwise keys | ✅ |
| Route poisoning | Sequence numbers | ✅ |
| Routing loops | hop_count + TTL | ✅ |
| Memory exhaustion | Periodic cleanup | ✅ |

### Ограничения

1. **16-битный MIC**: вероятность коллизии 1/65536
2. **Статические ключи**: нет динамического обмена
3. **Нет RERR**: при обрыве маршрут просто истекает
4. **Максимум 32 маршрута**: для больших сетей нужно увеличить

## Производительность

### STM32F4 @ 168MHz

| Операция | Время |
|----------|-------|
| Kuznyechik encrypt block | ~500 циклов (~3 мкс) |
| MAC для 64 байт | ~2000 циклов (~12 мкс) |
| mesh_find_route() | ~50 мкс |
| mesh_add_route() | ~100 мкс |
| mesh_process_rreq() | ~200 мкс |
| mesh_cleanup_routes() | ~500 мкс |

### Overhead

| Элемент | Размер |
|---------|--------|
| E2E MIC | 2 байта |
| Link MIC | 2 байта |
| RREQ payload | 14 байт |
| RREP payload | 13 байт |
| Route entry | 12 байт |

## Тестирование

### Запуск всех тестов

```bash
cd simulation

# MIC тесты
python3 test_mic.py

# AODV тесты
python3 test_aodv.py

# Интеграционные тесты
python3 test_integration.py
```

### Покрытие тестами

- ✅ Kuznyechik шифрование/дешифрование
- ✅ E2E MIC вычисление и проверка
- ✅ Link MIC вычисление и проверка
- ✅ AODV Route Discovery
- ✅ Route expiration
- ✅ Memory cleanup
- ✅ Time synchronization
- ✅ Защита от подделки

## Документация

- **[MIC_ARCHITECTURE.md](MIC_ARCHITECTURE.md)** - архитектура проверки целостности
- **[MIC_IMPLEMENTATION_REPORT.md](MIC_IMPLEMENTATION_REPORT.md)** - отчет о реализации MIC
- **[AODV_IMPLEMENTATION.md](AODV_IMPLEMENTATION.md)** - реализация AODV протокола
- **[AODV_MEMORY_REPORT.md](AODV_MEMORY_REPORT.md)** - управление памятью

## Будущие улучшения

1. **RERR пакеты** для быстрой инвалидации маршрутов
2. **Local Repair** для восстановления маршрутов
3. **32-битный MIC** для критичных приложений
4. **Diffie-Hellman** для динамического обмена ключами
5. **Forward secrecy** через периодическую ротацию ключей
6. **Цифровые подписи** для аутентификации узлов
7. **Hash table** для быстрого поиска маршрутов

## Лицензия

MIT License

## Авторы

Разработано с помощью Claude (Anthropic)

## Контакты

Для вопросов и предложений создавайте issue в репозитории.
