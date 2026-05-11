#!/usr/bin/env python3
"""
Терминальный конфигуратор Mesh-сети (без GUI зависимостей)
- Создание произвольного количества узлов
- Случайная генерация топологии
- Визуализация связей
- Отправка сообщений между узлами
- Консоль для каждого узла
"""

import sys
import os
import random
import threading
import time
import base64

# Добавляем путь к simulation
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from simulation.node.node import MeshNode

class NetworkTopology:
    """Генератор топологии сети"""

    def __init__(self, num_nodes):
        self.num_nodes = num_nodes
        self.nodes = []
        self.connections = {}

    def generate_random_topology(self, strategy=None):
        """Генерация случайной топологии с выбором стратегии

        Стратегии:
        - None (auto): случайный выбор стратегии
        - 'sparse': разреженная сеть (1-2 соседа на узел)
        - 'medium': средняя плотность (2-3 соседа)
        - 'dense': плотная сеть (3-4 соседа)
        - 'chain': цепочка узлов
        - 'star': звезда (один центральный узел)
        - 'ring': кольцо
        - 'tree': дерево
        """
        self.nodes = [f"NODE{i+1}" for i in range(self.num_nodes)]
        self.connections = {node: [] for node in self.nodes}

        # Автоматический выбор стратегии
        if strategy is None:
            strategies = ['sparse', 'medium', 'dense', 'chain', 'star', 'ring', 'tree']
            strategy = random.choice(strategies)

        if strategy == 'chain':
            self._generate_chain()
        elif strategy == 'star':
            self._generate_star()
        elif strategy == 'ring':
            self._generate_ring()
        elif strategy == 'tree':
            self._generate_tree()
        elif strategy == 'sparse':
            self._generate_random_with_density(1, 2)
        elif strategy == 'medium':
            self._generate_random_with_density(2, 3)
        elif strategy == 'dense':
            self._generate_random_with_density(3, 4)
        else:
            # По умолчанию - средняя плотность
            self._generate_random_with_density(2, 3)

        return self.connections

    def _add_connection(self, node1, node2):
        """Добавить двунаправленное соединение"""
        if node2 not in self.connections[node1]:
            self.connections[node1].append(node2)
        if node1 not in self.connections[node2]:
            self.connections[node2].append(node1)

    def _generate_chain(self):
        """Линейная цепочка: NODE1--NODE2--NODE3--..."""
        for i in range(len(self.nodes) - 1):
            self._add_connection(self.nodes[i], self.nodes[i + 1])

    def _generate_star(self):
        """Звезда: центральный узел соединен со всеми"""
        center = self.nodes[0]
        for node in self.nodes[1:]:
            self._add_connection(center, node)

    def _generate_ring(self):
        """Кольцо: каждый узел соединен с двумя соседями"""
        for i in range(len(self.nodes)):
            next_node = self.nodes[(i + 1) % len(self.nodes)]
            self._add_connection(self.nodes[i], next_node)

    def _generate_tree(self):
        """Дерево: случайное дерево без циклов"""
        connected = [self.nodes[0]]
        unconnected = self.nodes[1:]

        while unconnected:
            # Выбираем случайный узел из подключенных
            parent = random.choice(connected)
            # Выбираем случайный узел из неподключенных
            child = unconnected.pop(random.randint(0, len(unconnected) - 1))

            self._add_connection(parent, child)
            connected.append(child)

    def _generate_random_with_density(self, min_neighbors, max_neighbors):
        """Генерация случайной топологии с заданной плотностью"""
        # Сначала создаем связное дерево
        self._generate_tree()

        # Добавляем дополнительные связи для достижения нужной плотности
        for node in self.nodes:
            current_neighbors = len(self.connections[node])

            # Определяем сколько еще соседей нужно добавить
            target_neighbors = random.randint(min_neighbors, max_neighbors)
            needed = max(0, target_neighbors - current_neighbors)

            attempts = 0
            while needed > 0 and attempts < 20:
                attempts += 1

                # Выбираем кандидатов для соединения
                candidates = [n for n in self.nodes
                            if n != node
                            and n not in self.connections[node]
                            and len(self.connections[n]) < max_neighbors]

                if not candidates:
                    break

                neighbor = random.choice(candidates)
                self._add_connection(node, neighbor)
                needed -= 1

    def print_topology(self):
        """Вывод топологии"""
        print("\n" + "="*70)
        print("ТОПОЛОГИЯ СЕТИ")
        print("="*70)

        # Определяем тип топологии
        topology_type = self._detect_topology_type()
        print(f"Тип: {topology_type}")
        print()

        for node, neighbors in sorted(self.connections.items()):
            if neighbors:
                print(f"{node}: {', '.join(sorted(neighbors))}")

        print("\nВизуализация связей:")
        for node, neighbors in sorted(self.connections.items()):
            if neighbors:
                for neighbor in sorted(neighbors):
                    if node < neighbor:
                        print(f"  {node} <---> {neighbor}")

        # Статистика
        total_connections = sum(len(neighbors) for neighbors in self.connections.values()) // 2
        avg_neighbors = sum(len(neighbors) for neighbors in self.connections.values()) / len(self.nodes)
        print(f"\nСтатистика:")
        print(f"  Всего связей: {total_connections}")
        print(f"  Среднее соседей на узел: {avg_neighbors:.1f}")
        print("="*70)

    def _detect_topology_type(self):
        """Определить тип топологии"""
        if not self.connections:
            return "Пустая"

        neighbor_counts = [len(neighbors) for neighbors in self.connections.values()]
        avg_neighbors = sum(neighbor_counts) / len(neighbor_counts)
        max_neighbors = max(neighbor_counts)
        min_neighbors = min(neighbor_counts)

        # Проверка на звезду
        if max_neighbors == len(self.nodes) - 1 and neighbor_counts.count(1) == len(self.nodes) - 1:
            return "Звезда (Star)"

        # Проверка на цепочку
        if neighbor_counts.count(1) == 2 and neighbor_counts.count(2) == len(self.nodes) - 2:
            return "Цепочка (Chain)"

        # Проверка на кольцо
        if all(count == 2 for count in neighbor_counts):
            return "Кольцо (Ring)"

        # Проверка на дерево (нет циклов)
        total_edges = sum(neighbor_counts) // 2
        if total_edges == len(self.nodes) - 1:
            return "Дерево (Tree)"

        # По плотности
        if avg_neighbors < 2:
            return "Разреженная сеть (Sparse)"
        elif avg_neighbors < 3:
            return "Средняя плотность (Medium)"
        else:
            return "Плотная сеть (Dense)"

class MeshNetwork:
    """Управление mesh-сетью"""

    def __init__(self):
        self.nodes = {}
        self.node_threads = {}
        self.topology = None
        self.base_port = 7000
        self.running = False

    def generate_topology(self, num_nodes):
        """Генерация топологии"""
        self.topology = NetworkTopology(num_nodes)
        self.topology.generate_random_topology()
        self.topology.print_topology()

    def start_network(self):
        """Запуск сети"""
        if not self.topology:
            print("✗ Сначала сгенерируйте топологию")
            return

        print("\n" + "="*70)
        print("ЗАПУСК СЕТИ")
        print("="*70)

        for i, node_id in enumerate(self.topology.nodes):
            port = self.base_port + i

            neighbor_ports = []
            for neighbor in self.topology.connections[node_id]:
                neighbor_idx = self.topology.nodes.index(neighbor)
                neighbor_ports.append(self.base_port + neighbor_idx)

            is_time_master = (i == 0)
            node = MeshNode(node_id, port, neighbor_ports, is_time_master)

            # Устанавливаем E2E ключи (симметричные)
            for other_node_id in self.topology.nodes:
                if other_node_id != node_id:
                    # Используем одинаковый ключ для обеих сторон (меньший ID первым)
                    if node_id < other_node_id:
                        key = f"{node_id}_TO_{other_node_id}_KEY_2026".ljust(32, '!')[:32].encode()
                    else:
                        key = f"{other_node_id}_TO_{node_id}_KEY_2026".ljust(32, '!')[:32].encode()
                    node.set_pairwise_key(other_node_id, key)

            self.nodes[node_id] = node

            thread = threading.Thread(target=node.run, daemon=True)
            thread.start()
            self.node_threads[node_id] = thread

            print(f"✓ {node_id} запущен на порту {port}")

        # Даем узлам время на инициализацию
        time.sleep(0.5)

        # Автоматическое обнаружение соседей
        for node_id, node in self.nodes.items():
            node.discover_neighbors(self.topology.nodes)

        self.running = True
        print(f"\n✓ Запущено {len(self.nodes)} узлов")
        print("✓ Маршруты к прямым соседям установлены")
        print("="*70)

    def stop_network(self):
        """Остановка сети"""
        self.nodes.clear()
        self.node_threads.clear()
        self.running = False
        print("\n✓ Сеть остановлена")

    def send_message(self, sender, receiver, message):
        """Отправка сообщения"""
        if not self.running:
            print("✗ Сеть не запущена")
            return

        if sender not in self.nodes or receiver not in self.nodes:
            print("✗ Неверный отправитель или получатель")
            return

        node = self.nodes[sender]

        # Проверяем маршрут
        route = node.find_route(receiver)
        if not route:
            print(f"\n⚠ Маршрут к {receiver} не найден")
            print(f"Инициирован поиск маршрута (RREQ)...")
            node.send_rreq(receiver)
            print("Попробуйте отправить сообщение через 2-3 секунды")
            return

        # Отправляем данные
        msg_bytes = message.encode()
        payload_len = ((len(msg_bytes) + 15) // 16) * 16
        padded = msg_bytes.ljust(payload_len, b'\0')

        e2e_encrypted = receiver in node.pairwise_keys

        pkt = {
            "src": sender,
            "dst": receiver,
            "type": "DATA",
            "ttl": 20,
            "timestamp": node.internal_clock,
            "payload": "",
            "e2e_encrypted": e2e_encrypted,
            "e2e_mic": 0,
            "link_mic": 0
        }

        if e2e_encrypted:
            # Сначала шифруем
            encrypted = node.pairwise_keys[receiver].ctr_crypt(node.internal_clock, padded)
            pkt['payload'] = base64.b64encode(encrypted).decode()
            # Потом вычисляем E2E MIC с зашифрованным payload
            pkt['e2e_mic'] = node.compute_e2e_mic(pkt, node.pairwise_keys[receiver])
        else:
            encrypted = node.session_crypto.ctr_crypt(node.internal_clock, padded)
            pkt['payload'] = base64.b64encode(encrypted).decode()

        pkt['link_mic'] = node.compute_link_mic(pkt)

        node.send_packet(pkt)

        print(f"\n✓ Сообщение отправлено")
        print(f"  От: {sender}")
        print(f"  Кому: {receiver}")
        print(f"  E2E шифрование: {e2e_encrypted}")
        print(f"  E2E MIC: {pkt['e2e_mic']}")
        print(f"  Link MIC: {pkt['link_mic']}")

    def show_console(self, node_id):
        """Показать консоль узла"""
        if node_id not in self.nodes:
            print(f"✗ Узел {node_id} не найден")
            return

        node = self.nodes[node_id]

        print("\n" + "="*70)
        print(f"КОНСОЛЬ {node_id}")
        print("="*70)
        print(f"Порт: {node.udp_port}")
        print(f"Соседи: {node.neighbors}")
        print(f"Time Master: {node.is_time_master}")
        print(f"Внутренние часы: {node.internal_clock}")
        print(f"Маршруты: {len(node.routing_table)}")
        print(f"Seen packets: {len(node.seen_packets)}")
        print(f"Seen RREQ: {len(node.seen_rreq)}")

        print("\nМаршрутная таблица:")
        if node.routing_table:
            for dest, route in sorted(node.routing_table.items()):
                lifetime_left = route['lifetime'] - node.internal_clock
                print(f"  {dest}:")
                print(f"    Next hop: {route['next_hop']}")
                print(f"    Hop count: {route['hop_count']}")
                print(f"    Seq num: {route['seq_num']}")
                print(f"    Lifetime: {lifetime_left} тиков")
        else:
            print("  (пусто)")

        print(f"\nE2E ключи установлены для: {len(node.pairwise_keys)} узлов")
        print("="*70)

def main_menu():
    """Главное меню"""
    network = MeshNetwork()

    while True:
        print("\n" + "="*70)
        print("MESH NETWORK CONFIGURATOR")
        print("="*70)
        print("1. Сгенерировать топологию")
        print("2. Запустить сеть")
        print("3. Остановить сеть")
        print("4. Отправить сообщение")
        print("5. Открыть консоль узла")
        print("6. Показать топологию")
        print("0. Выход")
        print("="*70)

        choice = input("\nВыберите действие: ").strip()

        if choice == '1':
            try:
                num = int(input("Количество узлов (2-10): "))
                if 2 <= num <= 10:
                    network.generate_topology(num)
                else:
                    print("✗ Количество узлов должно быть от 2 до 10")
            except ValueError:
                print("✗ Неверный ввод")

        elif choice == '2':
            network.start_network()
            time.sleep(1)  # Даем время на инициализацию

        elif choice == '3':
            network.stop_network()

        elif choice == '4':
            if not network.running:
                print("✗ Сеть не запущена")
                continue

            print("\nДоступные узлы:", ", ".join(network.topology.nodes))
            sender = input("Отправитель: ").strip()
            receiver = input("Получатель: ").strip()
            message = input("Сообщение: ").strip()

            if sender and receiver and message:
                network.send_message(sender, receiver, message)
            else:
                print("✗ Заполните все поля")

        elif choice == '5':
            if not network.running:
                print("✗ Сеть не запущена")
                continue

            print("\nДоступные узлы:", ", ".join(network.topology.nodes))
            node_id = input("Узел: ").strip()
            if node_id:
                network.show_console(node_id)

        elif choice == '6':
            if network.topology:
                network.topology.print_topology()
            else:
                print("✗ Топология не сгенерирована")

        elif choice == '0':
            if network.running:
                network.stop_network()
            print("\nДо свидания!")
            break

        else:
            print("✗ Неверный выбор")

if __name__ == "__main__":
    try:
        main_menu()
    except KeyboardInterrupt:
        print("\n\nПрограмма прервана")
        sys.exit(0)
