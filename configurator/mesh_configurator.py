#!/usr/bin/env python3
"""
Интерактивный конфигуратор Mesh-сети
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

# Добавляем путь к simulation
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from simulation.node.node import MeshNode

try:
    import tkinter as tk
    from tkinter import ttk, scrolledtext, messagebox
except ImportError:
    print("Ошибка: tkinter не установлен")
    print("Установите: sudo dnf install python3-tkinter")
    sys.exit(1)

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
            parent = random.choice(connected)
            child = unconnected.pop(random.randint(0, len(unconnected) - 1))
            self._add_connection(parent, child)
            connected.append(child)

    def _generate_random_with_density(self, min_neighbors, max_neighbors):
        """Генерация случайной топологии с заданной плотностью"""
        self._generate_tree()

        for node in self.nodes:
            current_neighbors = len(self.connections[node])
            target_neighbors = random.randint(min_neighbors, max_neighbors)
            needed = max(0, target_neighbors - current_neighbors)

            attempts = 0
            while needed > 0 and attempts < 20:
                attempts += 1
                candidates = [n for n in self.nodes
                            if n != node
                            and n not in self.connections[node]
                            and len(self.connections[n]) < max_neighbors]

                if not candidates:
                    break

                neighbor = random.choice(candidates)
                self._add_connection(node, neighbor)
                needed -= 1

    def get_topology_string(self):
        """Получить строковое представление топологии"""
        lines = []

        # Определяем тип топологии
        topology_type = self._detect_topology_type()
        lines.append(f"Тип: {topology_type}\n")

        for node, neighbors in sorted(self.connections.items()):
            if neighbors:
                lines.append(f"{node}: {', '.join(sorted(neighbors))}")

        # Статистика
        total_connections = sum(len(neighbors) for neighbors in self.connections.values()) // 2
        avg_neighbors = sum(len(neighbors) for neighbors in self.connections.values()) / len(self.nodes)
        lines.append(f"\nСтатистика:")
        lines.append(f"Всего связей: {total_connections}")
        lines.append(f"Среднее соседей: {avg_neighbors:.1f}")

        return "\n".join(lines)

    def _detect_topology_type(self):
        """Определить тип топологии"""
        if not self.connections:
            return "Пустая"

        neighbor_counts = [len(neighbors) for neighbors in self.connections.values()]
        avg_neighbors = sum(neighbor_counts) / len(neighbor_counts)
        max_neighbors = max(neighbor_counts)

        if max_neighbors == len(self.nodes) - 1 and neighbor_counts.count(1) == len(self.nodes) - 1:
            return "Звезда (Star)"
        if neighbor_counts.count(1) == 2 and neighbor_counts.count(2) == len(self.nodes) - 2:
            return "Цепочка (Chain)"
        if all(count == 2 for count in neighbor_counts):
            return "Кольцо (Ring)"

        total_edges = sum(neighbor_counts) // 2
        if total_edges == len(self.nodes) - 1:
            return "Дерево (Tree)"

        if avg_neighbors < 2:
            return "Разреженная (Sparse)"
        elif avg_neighbors < 3:
            return "Средняя (Medium)"
        else:
            return "Плотная (Dense)"

class MeshConfigurator(tk.Tk):
    """Главное окно конфигуратора"""

    def __init__(self):
        super().__init__()

        self.title("Mesh Network Configurator")
        self.geometry("1200x800")

        self.nodes = {}  # {node_id: MeshNode}
        self.node_threads = {}
        self.topology = None
        self.base_port = 7000

        self.create_widgets()

    def create_widgets(self):
        """Создание виджетов"""

        # Верхняя панель - конфигурация
        config_frame = ttk.LabelFrame(self, text="Конфигурация сети", padding=10)
        config_frame.pack(fill=tk.X, padx=10, pady=5)

        # Количество узлов
        ttk.Label(config_frame, text="Количество узлов:").grid(row=0, column=0, padx=5)
        self.num_nodes_var = tk.IntVar(value=3)
        ttk.Spinbox(config_frame, from_=2, to=10, textvariable=self.num_nodes_var, width=10).grid(row=0, column=1, padx=5)

        # Кнопка генерации
        ttk.Button(config_frame, text="Сгенерировать топологию",
                  command=self.generate_topology).grid(row=0, column=2, padx=5)

        # Кнопка запуска
        self.start_button = ttk.Button(config_frame, text="Запустить сеть",
                                       command=self.start_network, state=tk.DISABLED)
        self.start_button.grid(row=0, column=3, padx=5)

        # Кнопка остановки
        self.stop_button = ttk.Button(config_frame, text="Остановить сеть",
                                      command=self.stop_network, state=tk.DISABLED)
        self.stop_button.grid(row=0, column=4, padx=5)

        # Средняя панель - топология
        topology_frame = ttk.LabelFrame(self, text="Топология сети", padding=10)
        topology_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        # Текст топологии
        self.topology_text = scrolledtext.ScrolledText(topology_frame, height=10, width=80)
        self.topology_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        # Визуализация (ASCII art)
        self.visual_text = scrolledtext.ScrolledText(topology_frame, height=10, width=40)
        self.visual_text.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True)

        # Нижняя панель - управление узлами
        control_frame = ttk.LabelFrame(self, text="Управление узлами", padding=10)
        control_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        # Выбор узла отправителя
        left_frame = ttk.Frame(control_frame)
        left_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5)

        ttk.Label(left_frame, text="Отправитель:").pack()
        self.sender_var = tk.StringVar()
        self.sender_combo = ttk.Combobox(left_frame, textvariable=self.sender_var, state='readonly')
        self.sender_combo.pack(fill=tk.X, pady=5)

        ttk.Label(left_frame, text="Получатель:").pack()
        self.receiver_var = tk.StringVar()
        self.receiver_combo = ttk.Combobox(left_frame, textvariable=self.receiver_var, state='readonly')
        self.receiver_combo.pack(fill=tk.X, pady=5)

        ttk.Label(left_frame, text="Сообщение:").pack()
        self.message_entry = ttk.Entry(left_frame)
        self.message_entry.pack(fill=tk.X, pady=5)

        ttk.Button(left_frame, text="Отправить", command=self.send_message).pack(pady=5)

        # Кнопки консолей
        right_frame = ttk.Frame(control_frame)
        right_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=5)

        ttk.Label(right_frame, text="Открыть консоль узла:").pack()
        self.console_buttons_frame = ttk.Frame(right_frame)
        self.console_buttons_frame.pack(fill=tk.BOTH, expand=True)

    def generate_topology(self):
        """Генерация топологии"""
        num_nodes = self.num_nodes_var.get()

        self.topology = NetworkTopology(num_nodes)
        connections = self.topology.generate_random_topology()

        # Показываем топологию
        self.topology_text.delete(1.0, tk.END)
        self.topology_text.insert(1.0, self.topology.get_topology_string())

        # Визуализация
        self.visualize_topology()

        # Обновляем комбобоксы
        node_list = self.topology.nodes
        self.sender_combo['values'] = node_list
        self.receiver_combo['values'] = node_list

        if node_list:
            self.sender_combo.current(0)
            if len(node_list) > 1:
                self.receiver_combo.current(1)

        # Активируем кнопку запуска
        self.start_button.config(state=tk.NORMAL)

        messagebox.showinfo("Топология", f"Сгенерирована топология для {num_nodes} узлов")

    def visualize_topology(self):
        """Простая ASCII визуализация"""
        self.visual_text.delete(1.0, tk.END)

        lines = []
        lines.append("Связи между узлами:")
        lines.append("")

        for node, neighbors in sorted(self.topology.connections.items()):
            if neighbors:
                for neighbor in sorted(neighbors):
                    if node < neighbor:  # Избегаем дублирования
                        lines.append(f"  {node} <---> {neighbor}")

        self.visual_text.insert(1.0, "\n".join(lines))

    def start_network(self):
        """Запуск сети"""
        if not self.topology:
            messagebox.showerror("Ошибка", "Сначала сгенерируйте топологию")
            return

        # Создаем узлы
        for i, node_id in enumerate(self.topology.nodes):
            port = self.base_port + i

            # Получаем порты соседей
            neighbor_ports = []
            for neighbor in self.topology.connections[node_id]:
                neighbor_idx = self.topology.nodes.index(neighbor)
                neighbor_ports.append(self.base_port + neighbor_idx)

            # Создаем узел
            is_time_master = (i == 0)
            node = MeshNode(node_id, port, neighbor_ports, is_time_master)

            self.nodes[node_id] = node

            thread = threading.Thread(target=node.run, daemon=True)
            thread.start()
            self.node_threads[node_id] = thread

        # Даем узлам время на инициализацию
        time.sleep(0.5)

        # Автоматическое обнаружение соседей
        for node_id, node in self.nodes.items():
            node.discover_neighbors(self.topology.nodes)

        # Создаем кнопки консолей
        for widget in self.console_buttons_frame.winfo_children():
            widget.destroy()

        for node_id in self.topology.nodes:
            btn = ttk.Button(self.console_buttons_frame, text=node_id,
                           command=lambda nid=node_id: self.open_console(nid))
            btn.pack(side=tk.LEFT, padx=2, pady=2)

        self.start_button.config(state=tk.DISABLED)
        self.stop_button.config(state=tk.NORMAL)

        messagebox.showinfo("Сеть", f"Запущено {len(self.nodes)} узлов")

    def stop_network(self):
        """Остановка сети"""
        self.nodes.clear()
        self.node_threads.clear()

        self.start_button.config(state=tk.NORMAL)
        self.stop_button.config(state=tk.DISABLED)

        messagebox.showinfo("Сеть", "Сеть остановлена")

    def send_message(self):
        """Отправка сообщения"""
        sender = self.sender_var.get()
        receiver = self.receiver_var.get()
        message = self.message_entry.get()

        if not sender or not receiver or not message:
            messagebox.showerror("Ошибка", "Заполните все поля")
            return

        if sender not in self.nodes:
            messagebox.showerror("Ошибка", "Сеть не запущена")
            return

        # Отправляем сообщение
        node = self.nodes[sender]

        # Используем новый унифицированный метод отправки
        status, detail = node.send_data_message(receiver, message)

        if status == 1: # RREQ
            messagebox.showinfo("Маршрутизация",
                              f"Маршрут к {receiver} не найден. Инициирован поиск маршрута (RREQ).\n"
                              f"Попробуйте отправить сообщение через 2-3 секунды.")
        elif status == 2: # DH
            messagebox.showinfo("Безопасность",
                              f"Защищенный ключ для {receiver} не установлен. Инициирован обмен ключами DH (X25519).\n"
                              f"Попробуйте отправить сообщение через 1-2 секунды.")
        elif status == 0:
            print(f"[{sender}] Отправлено сообщение для {receiver}")
        else:
            messagebox.showerror("Ошибка", f"Не удалось отправить сообщение: {detail}")

        self.message_entry.delete(0, tk.END)

    def open_console(self, node_id):
        """Открыть консоль узла"""
        ConsoleWindow(self, node_id, self.nodes.get(node_id))

class ConsoleWindow(tk.Toplevel):
    """Окно консоли узла"""

    def __init__(self, parent, node_id, node):
        super().__init__(parent)

        self.node_id = node_id
        self.node = node

        self.title(f"Консоль {node_id}")
        self.geometry("800x600")

        # Текстовая область для вывода
        self.console_text = scrolledtext.ScrolledText(self, height=30, width=100)
        self.console_text.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        # Информация о узле
        if node:
            info = f"=== Консоль {node_id} ===\n"
            info += f"Порт: {node.udp_port}\n"
            info += f"Соседи: {node.neighbors}\n"
            info += f"Time Master: {node.is_time_master}\n"
            info += f"Внутренние часы: {node.internal_clock}\n"
            info += f"Маршруты: {len(node.routing_table)}\n"
            info += f"\nМаршрутная таблица:\n"

            for dest, route in node.routing_table.items():
                info += f"  {dest}: via {route['next_hop']}, hops={route['hop_count']}, seq={route['seq_num']}\n"

            info += f"\nE2E ключи установлены для: {list(node.pairwise_keys.keys())}\n"
            info += "\n" + "="*50 + "\n"

            self.console_text.insert(1.0, info)
        else:
            self.console_text.insert(1.0, f"Узел {node_id} не запущен\n")

        # Кнопка обновления
        ttk.Button(self, text="Обновить", command=self.refresh).pack(pady=5)

    def refresh(self):
        """Обновление информации"""
        self.console_text.delete(1.0, tk.END)

        if self.node:
            info = f"=== Консоль {self.node_id} (обновлено {time.strftime('%H:%M:%S')}) ===\n"
            info += f"Порт: {self.node.udp_port}\n"
            info += f"Соседи: {self.node.neighbors}\n"
            info += f"Time Master: {self.node.is_time_master}\n"
            info += f"Внутренние часы: {self.node.internal_clock}\n"
            info += f"Маршруты: {len(self.node.routing_table)}\n"
            info += f"Seen packets: {len(self.node.seen_packets)}\n"
            info += f"Seen RREQ: {len(self.node.seen_rreq)}\n"
            info += f"\nМаршрутная таблица:\n"

            for dest, route in sorted(self.node.routing_table.items()):
                lifetime_left = route['lifetime'] - self.node.internal_clock
                info += f"  {dest}: via {route['next_hop']}, hops={route['hop_count']}, "
                info += f"seq={route['seq_num']}, lifetime={lifetime_left}\n"

            info += f"\nE2E ключи: {len(self.node.pairwise_keys)}\n"
            info += "\n" + "="*50 + "\n"

            self.console_text.insert(1.0, info)
        else:
            self.console_text.insert(1.0, f"Узел {self.node_id} не запущен\n")

def main():
    app = MeshConfigurator()
    app.mainloop()

if __name__ == "__main__":
    main()
