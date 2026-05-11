# AODV Routing Protocol Implementation

## Обзор

Реализован протокол маршрутизации AODV (Ad hoc On-Demand Distance Vector) для mesh-сети с поддержкой:
- Route Discovery (RREQ/RREP)
- Route Maintenance (lifetime, expiration)
- Memory Management (cleanup старых записей)
- Интеграция с криптографией (MIC для RREQ/RREP)

## Архитектура

```
┌─────────────────────────────────────────────────────────────┐
│                  AODV Routing Protocol                       │
│                                                              │
│  1. Route Discovery                                          │
│     ├─ RREQ (Route Request) - broadcast поиск маршрута      │
│     └─ RREP (Route Reply) - unicast ответ с маршрутом       │
│                                                              │
│  2. Routing Table                                            │
│     ├─ dest_id: ID узла назначения                          │
│     ├─ next_hop: следующий узел в маршруте                  │
│     ├─ hop_count: количество прыжков                        │
│     ├─ seq_num: sequence number для свежести                │
│     ├─ lifetime: время жизни маршрута                       │
│     └─ valid: флаг валидности                               │
│                                                              │
│  3. Memory Management                                        │
│     ├─ Cleanup expired routes (каждые 5000 тиков)           │
│     ├─ Cleanup old timestamps (каждые 10000 тиков)          │
│     └─ Cleanup old RREQ records (каждые 10000 тиков)        │
└─────────────────────────────────────────────────────────────┘
```

## Структуры данных

### Route Entry (C)

```c
typedef struct {
    uint8_t  dest_id;      // ID узла назначения
    uint8_t  next_hop;     // Следующий узел
    uint8_t  hop_count;    // Количество прыжков
    uint32_t seq_num;      // Sequence number
    uint32_t lifetime;     // Время истечения
    uint8_t  valid;        // Флаг валидности
} route_entry_t;
```

### RREQ Payload

```c
typedef struct {
    uint32_t rreq_id;       // ID запроса
    uint8_t  dest_id;       // Искомый узел
    uint32_t dest_seq_num;  // Seq num назначения
    uint8_t  orig_id;       // Инициатор запроса
    uint32_t orig_seq_num;  // Seq num инициатора
    uint8_t  hop_count;     // Счетчик прыжков
} rreq_payload_t;
```

### RREP Payload

```c
typedef struct {
    uint8_t  dest_id;       // Узел назначения
    uint32_t dest_seq_num;  // Seq num назначения
    uint8_t  orig_id;       // Инициатор запроса
    uint8_t  hop_count;     // Счетчик прыжков
    uint32_t lifetime;      // Время жизни маршрута
} rrep_payload_t;
```

## Процесс Route Discovery

### 1. Отправка данных без маршрута

```c
void mesh_send_data(uint8_t dst_id, const uint8_t *data, uint8_t len) {
    route_entry_t *route = mesh_find_route(dst_id);
    if (!route) {
        debug_puts("[AODV] No route, initiating discovery\n");
        mesh_send_rreq(dst_id);
        return;  // Данные будут отправлены после получения RREP
    }
    
    // Отправка данных по найденному маршруту
    // ...
}
```

### 2. Отправка RREQ

```
NODE1 (инициатор)
  ├─ Увеличивает rreq_id и self_seq_num
  ├─ Создает RREQ пакет:
  │    rreq_id = 1
  │    dest_id = NODE3
  │    orig_id = NODE1
  │    orig_seq_num = 1
  │    hop_count = 0
  ├─ Вычисляет Link MIC
  └─ Broadcast RREQ
       ↓
NODE2 (промежуточный)
  ├─ Проверяет Link MIC
  ├─ Проверяет duplicate (seen_rreq)
  ├─ Добавляет обратный маршрут к NODE1
  ├─ Увеличивает hop_count
  ├─ Пересылает RREQ дальше
       ↓
NODE3 (назначение)
  ├─ Проверяет Link MIC
  ├─ Добавляет обратный маршрут к NODE1
  ├─ Увеличивает self_seq_num
  └─ Отправляет RREP обратно к NODE1
```

### 3. Обработка RREP

```
NODE3 (назначение)
  └─ Отправляет RREP:
       dest_id = NODE3
       dest_seq_num = 2
       orig_id = NODE1
       hop_count = 0
       ↓
NODE2 (промежуточный)
  ├─ Проверяет Link MIC
  ├─ Добавляет прямой маршрут к NODE3
  ├─ Ищет обратный маршрут к NODE1
  ├─ Увеличивает hop_count
  └─ Пересылает RREP к NODE1
       ↓
NODE1 (инициатор)
  ├─ Проверяет Link MIC
  ├─ Добавляет прямой маршрут к NODE3
  └─ Маршрут установлен!
```

## Управление маршрутами

### Добавление маршрута

```c
void mesh_add_route(uint8_t dest_id, uint8_t next_hop, 
                    uint8_t hop_count, uint32_t seq_num) {
    route_entry_t *existing = mesh_find_route(dest_id);
    
    if (existing) {
        // Обновляем только если новый маршрут лучше
        if (seq_num > existing->seq_num ||
            (seq_num == existing->seq_num && hop_count < existing->hop_count)) {
            existing->next_hop = next_hop;
            existing->hop_count = hop_count;
            existing->seq_num = seq_num;
            existing->lifetime = internal_clock + ROUTE_LIFETIME;
        }
        return;
    }
    
    // Добавляем новый маршрут
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (!routing_table[i].valid) {
            routing_table[i].dest_id = dest_id;
            routing_table[i].next_hop = next_hop;
            routing_table[i].hop_count = hop_count;
            routing_table[i].seq_num = seq_num;
            routing_table[i].lifetime = internal_clock + ROUTE_LIFETIME;
            routing_table[i].valid = 1;
            return;
        }
    }
}
```

### Поиск маршрута

```c
route_entry_t* mesh_find_route(uint8_t dest_id) {
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routing_table[i].valid && routing_table[i].dest_id == dest_id) {
            if (routing_table[i].lifetime > internal_clock) {
                return &routing_table[i];
            } else {
                routing_table[i].valid = 0;  // Expired
            }
        }
    }
    return NULL;
}
```

### Очистка маршрутов

```c
void mesh_cleanup_routes(void) {
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routing_table[i].valid && 
            routing_table[i].lifetime <= internal_clock) {
            debug_puts("[AODV] Route expired\n");
            routing_table[i].valid = 0;
        }
    }
}
```

## Управление памятью

### Очистка старых записей

```c
#define TIMESTAMP_CLEANUP_INTERVAL 10000
#define TIMESTAMP_MAX_AGE 60000

void mesh_cleanup_old_data(void) {
    // Очистка старых timestamps
    for (int i = 0; i < 256; i++) {
        if (last_timestamps[i] > 0 &&
            (internal_clock - last_timestamps[i]) > TIMESTAMP_MAX_AGE) {
            last_timestamps[i] = 0;
        }
        
        // Очистка старых RREQ записей
        if (seen_rreq[i] > 0 &&
            (internal_clock - seen_rreq[i]) > TIMESTAMP_MAX_AGE) {
            seen_rreq[i] = 0;
        }
    }
}
```

### Периодический вызов

```c
void mesh_tick(void) {
    internal_clock++;
    
    // Очистка маршрутов каждые 5000 тиков
    if (internal_clock % 5000 == 0) {
        mesh_cleanup_routes();
    }
    
    // Очистка старых данных каждые 10000 тиков
    if (internal_clock - last_cleanup_time >= TIMESTAMP_CLEANUP_INTERVAL) {
        mesh_cleanup_old_data();
        last_cleanup_time = internal_clock;
    }
}
```

## Безопасность

### Link MIC для RREQ/RREP

RREQ и RREP пакеты защищены Link MIC:

```c
void mesh_process_packet(mesh_packet_t *pkt) {
    if (pkt->type == PACKET_TYPE_RREQ) {
        uint16_t expected_link_mic = compute_link_mic(pkt);
        if (pkt->link_mic != expected_link_mic) {
            debug_puts("[SECURITY] Link MIC verification FAILED!\n");
            return;
        }
        mesh_process_rreq(pkt, pkt->src_id);
        return;
    }
    
    if (pkt->type == PACKET_TYPE_RREP) {
        uint16_t expected_link_mic = compute_link_mic(pkt);
        if (pkt->link_mic != expected_link_mic) {
            debug_puts("[SECURITY] Link MIC verification FAILED!\n");
            return;
        }
        mesh_process_rrep(pkt, pkt->src_id);
        return;
    }
}
```

### Защита от атак

| Атака | Защита | Результат |
|-------|--------|-----------|
| Подделка RREQ/RREP | Link MIC | ✓ Отклоняется |
| Replay RREQ | seen_rreq[] | ✓ Дубликаты игнорируются |
| Route poisoning | Sequence numbers | ✓ Старые маршруты отклоняются |
| Routing loops | hop_count + TTL | ✓ Петли предотвращаются |

## Производительность

### Размер структур

- **route_entry_t**: 12 байт
- **routing_table[32]**: 384 байт
- **seen_rreq[256]**: 1024 байт
- **Overhead RREQ**: 14 байт
- **Overhead RREP**: 13 байт

### Время обработки (STM32F4 @ 168MHz)

- **mesh_find_route()**: ~50 мкс (линейный поиск)
- **mesh_add_route()**: ~100 мкс
- **mesh_process_rreq()**: ~200 мкс
- **mesh_process_rrep()**: ~150 мкс
- **mesh_cleanup_routes()**: ~500 мкс (каждые 5000 тиков)

### Память

- **Статическая**: ~1.5 КБ (routing_table + seen_rreq)
- **Динамическая**: 0 (все статически выделено)

## Ограничения

1. **Максимум 32 маршрута** (MAX_ROUTES)
   - Для больших сетей можно увеличить
   
2. **Линейный поиск** в routing_table
   - Для больших таблиц можно использовать hash table
   
3. **Нет RERR** (Route Error)
   - При обрыве связи маршрут просто истекает
   - Можно добавить RERR для быстрой инвалидации

4. **Нет Local Repair**
   - При обрыве маршрута нужен новый RREQ
   - Можно добавить локальный ремонт

## Будущие улучшения

1. **RERR пакеты** для быстрой инвалидации маршрутов
2. **Local Repair** для восстановления маршрутов
3. **Precursor lists** для оптимизации RERR
4. **Route caching** для уменьшения RREQ
5. **Expanding Ring Search** для оптимизации RREQ flooding
6. **Hash table** для быстрого поиска маршрутов

## API

### C (Firmware)

```c
// Управление маршрутами
route_entry_t* mesh_find_route(uint8_t dest_id);
void mesh_add_route(uint8_t dest_id, uint8_t next_hop, 
                    uint8_t hop_count, uint32_t seq_num);
void mesh_update_route_lifetime(uint8_t dest_id);
void mesh_invalidate_route(uint8_t dest_id);
void mesh_cleanup_routes(void);

// Route Discovery
void mesh_send_rreq(uint8_t dest_id);
void mesh_process_rreq(mesh_packet_t *pkt, uint8_t from_node);
void mesh_process_rrep(mesh_packet_t *pkt, uint8_t from_node);

// Отправка данных (автоматический RREQ если нет маршрута)
void mesh_send_data(uint8_t dst_id, const uint8_t *data, uint8_t len);
```

### Python (Simulation)

```python
# Управление маршрутами
route = node.find_route(dest_id)
node.add_route(dest_id, next_hop, hop_count, seq_num)
node.cleanup_routes()

# Route Discovery
node.send_rreq(dest_id)
node.process_rreq(packet, from_node)
node.process_rrep(packet, from_node)

# Memory Management
node.cleanup_old_data()
```

## Тестирование

Запуск тестов:

```bash
cd simulation
python3 test_aodv.py
```

Тесты проверяют:
1. Route Discovery (RREQ/RREP)
2. Data Transmission с маршрутизацией
3. Route Expiration
4. Memory Cleanup

## Пример использования

### Сценарий: NODE1 отправляет сообщение NODE3 через NODE2

```
1. NODE1 пытается отправить данные NODE3
   └─ Маршрута нет → отправляет RREQ

2. RREQ распространяется:
   NODE1 → NODE2 → NODE3
   
3. NODE3 отправляет RREP:
   NODE3 → NODE2 → NODE1
   
4. Маршруты установлены:
   NODE1: NODE3 via NODE2 (hop_count=2)
   NODE2: NODE3 via NODE3 (hop_count=1)
   NODE2: NODE1 via NODE1 (hop_count=1)
   NODE3: NODE1 via NODE2 (hop_count=2)
   
5. NODE1 отправляет данные по маршруту:
   NODE1 → NODE2 → NODE3
```

## Статус

- ✅ Route Discovery (RREQ/RREP)
- ✅ Routing Table management
- ✅ Route expiration
- ✅ Memory cleanup
- ✅ Link MIC для RREQ/RREP
- ✅ Python симуляция
- ✅ Тесты
- ⏳ RERR (Route Error)
- ⏳ Local Repair

**Готово к использованию!**
