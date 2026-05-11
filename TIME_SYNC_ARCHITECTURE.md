# Синхронизация времени в Mesh-сети

## Обзор

Система использует активную синхронизацию времени для:
- Корректной работы replay protection
- Уникальности nonce в CTR режиме шифрования
- Координации событий в сети

## Архитектура

### Time Master

- **Один узел** в сети назначается Time Master (обычно узел 1 или Gateway)
- Time Master периодически рассылает broadcast пакеты `PACKET_TYPE_TIME`
- Частота рассылки: каждые **1000 тиков** (~1 секунда при типичной скорости)

### Slave узлы

- Все остальные узлы синхронизируются с Time Master
- Получают TIME пакеты и корректируют свои часы
- Пересылают TIME пакеты дальше (для многоуровневых сетей)

## Алгоритм синхронизации

### Hard Sync (жесткая синхронизация)

Используется при большой рассинхронизации (>100 тиков):

```c
if (abs(time_diff) > 100) {
    internal_clock = pkt->timestamp;  // Резкая установка времени
}
```

**Когда применяется:**
- Первая синхронизация после старта
- После длительного отсутствия связи
- При критической рассинхронизации

### Soft Sync (мягкая синхронизация)

Используется при небольшой рассинхронизации (≤100 тиков):

```c
if (time_diff != 0) {
    clock_offset = time_diff / 4;
    internal_clock += clock_offset;  // Постепенная коррекция
}
```

**Преимущества:**
- Избегает резких скачков времени
- Плавная коррекция за несколько итераций
- Не нарушает монотонность timestamp

## Структура TIME пакета

```c
typedef struct {
    uint8_t  src_id;        // ID Time Master
    uint8_t  dst_id;        // 255 (broadcast)
    uint8_t  type;          // PACKET_TYPE_TIME
    uint8_t  ttl;           // 5 (для многоуровневых сетей)
    uint32_t timestamp;     // Текущее время мастера
    uint8_t  payload_len;   // 0
    uint8_t  e2e_encrypted; // 0
    uint8_t  payload[64];   // Не используется
    uint16_t mic;           // TODO
} mesh_packet_t;
```

## Процесс синхронизации

### 1. Time Master рассылает время

```c
void mesh_tick(void) {
    internal_clock++;
    time_sync_counter++;

    if (is_time_master && time_sync_counter >= 1000) {
        mesh_broadcast_time();
        time_sync_counter = 0;
    }
}

void mesh_broadcast_time(void) {
    mesh_packet_t pkt;
    pkt.src_id = self_node_id;
    pkt.dst_id = 255;  // Broadcast
    pkt.type = PACKET_TYPE_TIME;
    pkt.ttl = 5;
    pkt.timestamp = internal_clock;
    lora_send_packet(&pkt);
}
```

### 2. Slave узлы получают и обрабатывают

```c
if (pkt->type == PACKET_TYPE_TIME) {
    if (!is_time_master) {
        int32_t time_diff = pkt->timestamp - internal_clock;

        if (abs(time_diff) > 100) {
            internal_clock = pkt->timestamp;  // Hard sync
        } else if (time_diff != 0) {
            internal_clock += time_diff / 4;  // Soft sync
        }
    }

    // Пересылка для многоуровневых сетей
    if (pkt->dst_id == 255 && pkt->ttl > 0) {
        pkt->ttl--;
        lora_send_packet(pkt);
    }
}
```

## Особенности реализации

### Replay Protection для TIME пакетов

TIME пакеты **НЕ проверяются** через `last_timestamps[]`:

```c
// TIME обрабатывается ДО replay protection
if (pkt->type == PACKET_TYPE_TIME) {
    // Синхронизация
    return;
}

// Replay protection для остальных пакетов
if (pkt->timestamp <= last_timestamps[pkt->src_id]) return;
```

**Почему:**
- TIME пакеты могут приходить "из будущего" при рассинхронизации
- Нужно принять их для коррекции часов
- Duplicate suppression через `seen_packets` все равно работает

### Пересылка TIME пакетов

Промежуточные узлы пересылают TIME пакеты:

```
Node1 (Master) --TIME--> Node2 (Relay) --TIME--> Node3 (End)
   |                        |                        |
   └─ Генерирует           └─ Синхронизируется      └─ Синхронизируется
                              и пересылает
```

Это обеспечивает синхронизацию в многоуровневых сетях.

## Тестирование

### Тест синхронизации

```bash
cd simulation
python3 test_time_sync.py
```

**Сценарий:**
- Node1 (master): начальное время = 1000
- Node2 (relay): начальное время = 50 (сильно отстает)
- Node3 (end): начальное время = 980 (немного отстает)

**Ожидаемый результат:**
```
✓ SUCCESS: All nodes synchronized within 100 ticks
```

### Результаты теста

```
Initial clocks:
  NODE1 (master): 1000
  NODE2 (relay):  50
  NODE3 (end):    980

[NODE2] [TIME] Hard sync: clock adjusted by 959
[NODE3] [TIME] Soft sync: offset=9

Final clocks:
  NODE1 (master): 3707
  NODE2 (relay):  3672  (diff: 35 ticks)
  NODE3 (end):    3670  (diff: 37 ticks)
```

## Метрики синхронизации

### Точность

- **Типичная точность:** ±50 тиков
- **Максимальное отклонение:** 100 тиков (порог hard sync)
- **Время сходимости:** 2-3 цикла синхронизации (~2-3 секунды)

### Производительность

- **Частота синхронизации:** 1 раз в секунду
- **Размер TIME пакета:** ~10 байт (без payload)
- **Overhead:** минимальный (~10 байт/сек на узел)

## Настройка

### Изменение частоты синхронизации

```c
// Firmware (C)
if (is_time_master && time_sync_counter >= 1000) {  // Изменить 1000
    mesh_broadcast_time();
}

// Simulation (Python)
if self.is_time_master and self.time_sync_counter >= 1000:  # Изменить 1000
    self.broadcast_time()
```

**Рекомендации:**
- **1000 тиков** — хороший баланс для большинства случаев
- **500 тиков** — для критичных к точности приложений
- **2000 тиков** — для экономии энергии

### Изменение порога hard sync

```c
if (abs(time_diff) > 100) {  // Изменить 100
    internal_clock = pkt->timestamp;
}
```

**Рекомендации:**
- **100 тиков** — стандартное значение
- **50 тиков** — для более агрессивной коррекции
- **200 тиков** — для более плавной работы

### Назначение Time Master

```c
// Firmware
is_time_master = (node_id == 1);  // Узел 1 — мастер

// Python
is_time_master = (node_id == "NODE1" or node_id == "GATEWAY")
```

## Ограничения

1. **Один Time Master** — нет автоматического failover
2. **Нет компенсации задержки** — не учитывается время передачи пакета
3. **Линейный дрейф** — `internal_clock++` без привязки к реальному времени
4. **Нет NTP-подобной точности** — достаточно для mesh-сети, но не для точных измерений

## Будущие улучшения

1. **Автоматический выбор Time Master** — через алгоритм выборов
2. **Компенсация задержки** — учет RTT при синхронизации
3. **Привязка к RTC** — использование реального времени на мастере
4. **Метрики качества** — отслеживание jitter и drift
5. **Резервный Time Master** — автоматический failover

## API

### C (Firmware)

```c
void mesh_init(uint8_t node_id);           // Инициализация (автоматически определяет мастера)
void mesh_tick(void);                      // Вызывать в main loop
void mesh_broadcast_time(void);            // Ручная рассылка времени
uint32_t mesh_get_time(void);              // Получить текущее время
```

### Python (Simulation)

```python
node = MeshNode(node_id, port, neighbors, is_time_master=True)
node.tick()                                # Вызывать в main loop
node.broadcast_time()                      # Ручная рассылка времени
current_time = node.internal_clock        # Получить текущее время
```

## Пример использования

### Firmware (C)

```c
void main(void) {
    mesh_init(1);  // Узел 1 автоматически становится Time Master

    while(1) {
        mesh_tick();  // Автоматическая рассылка времени каждые 1000 тиков

        // Ваш код
        for(volatile int i=0; i<10; i++);
    }
}
```

### Simulation (Python)

```python
# Time Master
node1 = MeshNode("NODE1", 5001, [5002], is_time_master=True)

# Slave
node2 = MeshNode("NODE2", 5002, [5001], is_time_master=False)

while True:
    node1.tick()  # Автоматическая рассылка
    node2.tick()  # Автоматическая синхронизация
```
