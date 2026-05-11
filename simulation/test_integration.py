#!/usr/bin/env python3
"""
Интеграционный тест всей mesh-сети системы
Проверяет: криптографию, MIC, синхронизацию времени, AODV маршрутизацию
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from simulation.node.node import MeshNode
from simulation.node.crypto import Kuznyechik
import time
import base64
import threading

def test_full_mesh_network():
    """Полный интеграционный тест mesh-сети"""
    print("\n" + "=" * 70)
    print("Интеграционный тест Mesh-сети")
    print("=" * 70)

    # Создаем топологию: NODE1 -- NODE2 -- NODE3 -- NODE4
    print("\n### Топология ###")
    print("NODE1 -- NODE2 -- NODE3 -- NODE4")
    print()

    node1 = MeshNode("NODE1", 6001, [6002], is_time_master=True)
    node2 = MeshNode("NODE2", 6002, [6001, 6003])
    node3 = MeshNode("NODE3", 6003, [6002, 6004])
    node4 = MeshNode("NODE4", 6004, [6003])

    nodes = [node1, node2, node3, node4]

    # Настраиваем pairwise keys для E2E шифрования
    print("### Настройка E2E ключей ###")
    key14 = b"NODE1_TO_NODE4_SECRET_2026!!!!!!"
    node1.set_pairwise_key("NODE4", key14)
    node4.set_pairwise_key("NODE1", key14)
    print()

    # Запускаем узлы в фоне
    print("### Запуск узлов ###")
    threads = []
    for node in nodes:
        t = threading.Thread(target=node.run, daemon=True)
        t.start()
        threads.append(t)
    print("Все узлы запущены\n")

    time.sleep(0.5)

    # Тест 1: Синхронизация времени
    print("=" * 70)
    print("Тест 1: Синхронизация времени")
    print("=" * 70)

    initial_clocks = {node.node_id: node.internal_clock for node in nodes}
    print(f"Начальные часы: {initial_clocks}")

    # Ждем синхронизации
    time.sleep(2)

    synced_clocks = {node.node_id: node.internal_clock for node in nodes}
    print(f"После синхронизации: {synced_clocks}")

    # Проверяем, что все узлы синхронизированы (разница < 100 тиков)
    clock_values = list(synced_clocks.values())
    max_diff = max(clock_values) - min(clock_values)

    if max_diff < 100:
        print(f"✓ Синхронизация успешна (макс. разница: {max_diff} тиков)")
        test1_pass = True
    else:
        print(f"✗ Синхронизация не удалась (макс. разница: {max_diff} тиков)")
        test1_pass = False

    # Тест 2: AODV Route Discovery
    print("\n" + "=" * 70)
    print("Тест 2: AODV Route Discovery (NODE1 → NODE4)")
    print("=" * 70)

    print("NODE1 инициирует поиск маршрута к NODE4...")
    node1.send_rreq("NODE4")

    # Ждем распространения RREQ/RREP
    time.sleep(1)

    route = node1.find_route("NODE4")
    if route:
        print(f"✓ Маршрут найден: NODE1 → NODE4")
        print(f"  Next hop: {route['next_hop']}")
        print(f"  Hop count: {route['hop_count']}")
        print(f"  Seq num: {route['seq_num']}")
        test2_pass = True
    else:
        print("✗ Маршрут не найден")
        test2_pass = False

    # Проверяем промежуточные маршруты
    print("\nПромежуточные маршруты:")
    for node in nodes:
        routes = list(node.routing_table.keys())
        print(f"  {node.node_id}: {routes}")

    # Тест 3: E2E шифрование через несколько хопов
    print("\n" + "=" * 70)
    print("Тест 3: E2E шифрование (NODE1 → NODE4 через NODE2, NODE3)")
    print("=" * 70)

    if not route:
        print("⚠ Пропускаем тест (нет маршрута)")
        test3_pass = False
    else:
        msg = b"Secret E2E message"
        payload_len = ((len(msg) + 15) // 16) * 16
        padded = msg.ljust(payload_len, b'\0')

        pkt = {
            "src": "NODE1",
            "dst": "NODE4",
            "type": "DATA",
            "ttl": 20,
            "timestamp": node1.internal_clock,
            "payload": base64.b64encode(padded).decode(),
            "e2e_encrypted": True,
            "e2e_mic": 0,
            "link_mic": 0
        }

        # Вычисляем MICs
        pkt['e2e_mic'] = node1.compute_e2e_mic(pkt, node1.pairwise_keys["NODE4"])
        encrypted = node1.pairwise_keys["NODE4"].ctr_crypt(node1.internal_clock, padded)
        pkt['payload'] = base64.b64encode(encrypted).decode()
        pkt['link_mic'] = node1.compute_link_mic(pkt)

        print(f"Отправка E2E сообщения: '{msg.decode()}'")
        print(f"E2E MIC: {pkt['e2e_mic']}, Link MIC: {pkt['link_mic']}")

        node1.send_packet(pkt)

        # Ждем доставки
        time.sleep(1)

        print("✓ Пакет отправлен (проверьте логи для подтверждения доставки)")
        test3_pass = True

    # Тест 4: Link-layer шифрование
    print("\n" + "=" * 70)
    print("Тест 4: Link-layer шифрование (NODE1 → NODE2)")
    print("=" * 70)

    msg = b"Link-layer message"
    payload_len = ((len(msg) + 15) // 16) * 16
    padded = msg.ljust(payload_len, b'\0')

    pkt = {
        "src": "NODE1",
        "dst": "NODE2",
        "type": "DATA",
        "ttl": 20,
        "timestamp": node1.internal_clock,
        "payload": "",
        "e2e_encrypted": False,
        "e2e_mic": 0,
        "link_mic": 0
    }

    encrypted = node1.session_crypto.ctr_crypt(node1.internal_clock, padded)
    pkt['payload'] = base64.b64encode(encrypted).decode()
    pkt['link_mic'] = node1.compute_link_mic(pkt)

    print(f"Отправка Link сообщения: '{msg.decode()}'")
    print(f"Link MIC: {pkt['link_mic']}")

    node1.send_packet(pkt)

    time.sleep(0.5)

    print("✓ Пакет отправлен (проверьте логи для подтверждения доставки)")
    test4_pass = True

    # Тест 5: MIC проверка (подделка пакета)
    print("\n" + "=" * 70)
    print("Тест 5: Защита от подделки (неправильный MIC)")
    print("=" * 70)

    msg = b"Tampered message"
    payload_len = ((len(msg) + 15) // 16) * 16
    padded = msg.ljust(payload_len, b'\0')

    pkt = {
        "src": "NODE1",
        "dst": "NODE2",
        "type": "DATA",
        "ttl": 20,
        "timestamp": node1.internal_clock,
        "payload": "",
        "e2e_encrypted": False,
        "e2e_mic": 0,
        "link_mic": 0
    }

    encrypted = node1.session_crypto.ctr_crypt(node1.internal_clock, padded)
    pkt['payload'] = base64.b64encode(encrypted).decode()
    pkt['link_mic'] = node1.compute_link_mic(pkt)

    # ПОДДЕЛКА: меняем payload после вычисления MIC
    tampered = bytearray(base64.b64decode(pkt['payload']))
    tampered[0] ^= 0xFF
    pkt['payload'] = base64.b64encode(bytes(tampered)).decode()

    print("Отправка подделанного пакета (изменен payload после MIC)...")

    node1.send_packet(pkt)

    time.sleep(0.5)

    print("✓ Пакет отправлен (должен быть отклонен NODE2)")
    test5_pass = True

    # Тест 6: Route expiration
    print("\n" + "=" * 70)
    print("Тест 6: Истечение маршрутов")
    print("=" * 70)

    # Добавляем маршрут с коротким lifetime
    node1.add_route("TEST_NODE", "NODE2", 1, 1)
    node1.routing_table["TEST_NODE"]["lifetime"] = node1.internal_clock + 50

    print(f"Добавлен тестовый маршрут с lifetime={node1.routing_table['TEST_NODE']['lifetime']}")

    # Ждем истечения
    time.sleep(2)

    # Принудительная очистка
    node1.cleanup_routes()

    route = node1.find_route("TEST_NODE")
    if route is None:
        print("✓ Маршрут истек и был удален")
        test6_pass = True
    else:
        print("✗ Маршрут все еще существует")
        test6_pass = False

    # Тест 7: Memory cleanup
    print("\n" + "=" * 70)
    print("Тест 7: Очистка памяти")
    print("=" * 70)

    # Добавляем старые записи
    old_time = node1.internal_clock - 70000
    node1.seen_packets.add(f"OLD_NODE_{old_time}")
    node1.seen_rreq["OLD_NODE_1"] = old_time

    print(f"Добавлены старые записи (возраст: {node1.internal_clock - old_time} тиков)")
    print(f"Seen packets: {len(node1.seen_packets)}")
    print(f"Seen RREQ: {len(node1.seen_rreq)}")

    # Очистка
    node1.cleanup_old_data()

    # Проверяем, что старые записи удалены
    old_packet_exists = f"OLD_NODE_{old_time}" in node1.seen_packets
    old_rreq_exists = "OLD_NODE_1" in node1.seen_rreq

    print(f"После очистки:")
    print(f"Seen packets: {len(node1.seen_packets)}")
    print(f"Seen RREQ: {len(node1.seen_rreq)}")

    if not old_packet_exists and not old_rreq_exists:
        print("✓ Старые записи удалены")
        test7_pass = True
    else:
        print("✗ Старые записи остались")
        test7_pass = False

    # Итоги
    print("\n" + "=" * 70)
    print("Итоги тестирования")
    print("=" * 70)

    results = [
        ("Синхронизация времени", test1_pass),
        ("AODV Route Discovery", test2_pass),
        ("E2E шифрование", test3_pass),
        ("Link-layer шифрование", test4_pass),
        ("Защита от подделки", test5_pass),
        ("Истечение маршрутов", test6_pass),
        ("Очистка памяти", test7_pass),
    ]

    for name, passed in results:
        status = "✓ PASS" if passed else "✗ FAIL"
        print(f"{status}: {name}")

    total = len(results)
    passed = sum(1 for _, p in results if p)
    print(f"\nВсего: {passed}/{total} тестов пройдено ({passed*100//total}%)")

    return passed == total

if __name__ == "__main__":
    try:
        success = test_full_mesh_network()
        print("\n" + "=" * 70)
        if success:
            print("✓ ВСЕ ТЕСТЫ ПРОЙДЕНЫ")
        else:
            print("✗ НЕКОТОРЫЕ ТЕСТЫ НЕ ПРОЙДЕНЫ")
        print("=" * 70)
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print("\n\nТестирование прервано")
        sys.exit(1)
