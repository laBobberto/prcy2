#!/usr/bin/env python3
"""
Графический конфигуратор Mesh-сети на PyQt5
- Современный интерфейс с темной темой
- Визуализация графа сети
- Анимация передачи сообщений
- Консоли узлов в отдельных окнах
"""

import sys
import os
import random
import threading
import time
import base64
import math

# Добавляем путь к simulation
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from simulation.node.node import MeshNode

try:
    from PyQt5.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout,
                                 QHBoxLayout, QPushButton, QLabel, QSpinBox,
                                 QComboBox, QLineEdit, QTextEdit, QSplitter,
                                 QGroupBox, QMessageBox, QDialog, QTabWidget)
    from PyQt5.QtCore import Qt, QTimer, QPointF, pyqtSignal, QObject
    from PyQt5.QtGui import QPainter, QColor, QPen, QBrush, QFont, QPalette
except ImportError:
    print("Ошибка: PyQt5 не установлен")
    print("Установите: pip install PyQt5")
    sys.exit(1)

class NetworkTopology:
    """Генератор топологии сети"""

    def __init__(self, num_nodes):
        self.num_nodes = num_nodes
        self.nodes = []
        self.connections = {}
        self.positions = {}

    def generate_random_topology(self, strategy=None):
        """Генерация случайной топологии"""
        self.nodes = [f"NODE{i+1}" for i in range(self.num_nodes)]
        self.connections = {node: [] for node in self.nodes}

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

        self._calculate_positions()
        return self.connections

    def _add_connection(self, node1, node2):
        if node2 not in self.connections[node1]:
            self.connections[node1].append(node2)
        if node1 not in self.connections[node2]:
            self.connections[node2].append(node1)

    def _generate_chain(self):
        for i in range(len(self.nodes) - 1):
            self._add_connection(self.nodes[i], self.nodes[i + 1])

    def _generate_star(self):
        center = self.nodes[0]
        for node in self.nodes[1:]:
            self._add_connection(center, node)

    def _generate_ring(self):
        for i in range(len(self.nodes)):
            next_node = self.nodes[(i + 1) % len(self.nodes)]
            self._add_connection(self.nodes[i], next_node)

    def _generate_tree(self):
        connected = [self.nodes[0]]
        unconnected = self.nodes[1:]
        while unconnected:
            parent = random.choice(connected)
            child = unconnected.pop(random.randint(0, len(unconnected) - 1))
            self._add_connection(parent, child)
            connected.append(child)

    def _generate_random_with_density(self, min_neighbors, max_neighbors):
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

    def _calculate_positions(self):
        """Вычислить позиции узлов для визуализации"""
        n = len(self.nodes)
        radius = 200
        center_x, center_y = 300, 300

        for i, node in enumerate(self.nodes):
            angle = 2 * math.pi * i / n - math.pi / 2
            x = center_x + radius * math.cos(angle)
            y = center_y + radius * math.sin(angle)
            self.positions[node] = (x, y)

    def get_topology_type(self):
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

class NetworkGraphWidget(QWidget):
    """Виджет для визуализации графа сети"""

    node_clicked = pyqtSignal(str)

    def __init__(self):
        super().__init__()
        self.topology = None
        self.selected_node = None
        self.setMinimumSize(600, 600)

    def set_topology(self, topology):
        self.topology = topology
        self.update()

    def paintEvent(self, event):
        if not self.topology:
            return

        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        # Рисуем связи
        pen = QPen(QColor(100, 100, 100), 2)
        painter.setPen(pen)

        for node, neighbors in self.topology.connections.items():
            x1, y1 = self.topology.positions[node]
            for neighbor in neighbors:
                if node < neighbor:  # Избегаем дублирования
                    x2, y2 = self.topology.positions[neighbor]
                    painter.drawLine(int(x1), int(y1), int(x2), int(y2))

        # Рисуем узлы
        for node in self.topology.nodes:
            x, y = self.topology.positions[node]

            # Цвет узла
            if node == self.selected_node:
                color = QColor(255, 165, 0)  # Оранжевый для выбранного
            elif node == "NODE1":
                color = QColor(0, 200, 0)  # Зеленый для Time Master
            else:
                color = QColor(70, 130, 180)  # Синий для обычных

            # Рисуем круг
            painter.setBrush(QBrush(color))
            painter.setPen(QPen(QColor(255, 255, 255), 2))
            painter.drawEllipse(int(x - 25), int(y - 25), 50, 50)

            # Рисуем текст
            painter.setPen(QPen(QColor(255, 255, 255)))
            painter.setFont(QFont("Arial", 10, QFont.Bold))
            text_rect = painter.boundingRect(int(x - 25), int(y - 10), 50, 20, Qt.AlignCenter, node)
            painter.drawText(text_rect, Qt.AlignCenter, node)

    def mousePressEvent(self, event):
        if not self.topology:
            return

        x, y = event.x(), event.y()

        # Проверяем клик по узлу
        for node in self.topology.nodes:
            nx, ny = self.topology.positions[node]
            distance = math.sqrt((x - nx)**2 + (y - ny)**2)
            if distance <= 25:
                self.selected_node = node
                self.node_clicked.emit(node)
                self.update()
                break

class ConsoleDialog(QDialog):
    """Диалог консоли узла"""

    def __init__(self, parent, node_id, node):
        super().__init__(parent)
        self.node_id = node_id
        self.node = node

        self.setWindowTitle(f"Консоль {node_id}")
        self.setGeometry(100, 100, 700, 500)

        layout = QVBoxLayout()

        # Текстовая область
        self.console_text = QTextEdit()
        self.console_text.setReadOnly(True)
        self.console_text.setFont(QFont("Courier", 10))
        layout.addWidget(self.console_text)

        # Кнопка обновления
        refresh_btn = QPushButton("Обновить")
        refresh_btn.clicked.connect(self.refresh)
        layout.addWidget(refresh_btn)

        self.setLayout(layout)
        self.refresh()

    def refresh(self):
        if not self.node:
            self.console_text.setText(f"Узел {self.node_id} не запущен")
            return

        info = f"{'='*60}\n"
        info += f"КОНСОЛЬ {self.node_id}\n"
        info += f"{'='*60}\n\n"
        info += f"Порт: {self.node.udp_port}\n"
        info += f"Соседи: {self.node.neighbors}\n"
        info += f"Time Master: {self.node.is_time_master}\n"
        info += f"Внутренние часы: {self.node.internal_clock}\n"
        info += f"Маршруты: {len(self.node.routing_table)}\n"
        info += f"Seen packets: {len(self.node.seen_packets)}\n"
        info += f"Seen RREQ: {len(self.node.seen_rreq)}\n\n"

        info += "Маршрутная таблица:\n"
        if self.node.routing_table:
            for dest, route in sorted(self.node.routing_table.items()):
                lifetime_left = route['lifetime'] - self.node.internal_clock
                info += f"  {dest}:\n"
                info += f"    Next hop: {route['next_hop']}\n"
                info += f"    Hop count: {route['hop_count']}\n"
                info += f"    Seq num: {route['seq_num']}\n"
                info += f"    Lifetime: {lifetime_left} тиков\n"
        else:
            info += "  (пусто)\n"

        info += f"\nE2E ключи: {len(self.node.pairwise_keys)} узлов\n"

        self.console_text.setText(info)

class MessageSignal(QObject):
    """Сигнал для передачи сообщений между потоками"""
    received = pyqtSignal(str, str, str, bool) # receiver_id, sender_id, message, e2e

class MeshConfiguratorQt(QMainWindow):
    """Главное окно конфигуратора"""

    def __init__(self):
        super().__init__()

        self.nodes = {}
        self.node_threads = {}
        self.topology = None
        self.base_port = 7000
        self.running = False
        self.msg_signal = MessageSignal()
        self.msg_signal.received.connect(self.show_received_message)

        self.init_ui()
        self.apply_dark_theme()

    def init_ui(self):
        self.setWindowTitle("Mesh Network Configurator - PyQt5")
        self.setGeometry(100, 100, 1400, 800)

        # Центральный виджет
        central_widget = QWidget()
        self.setCentralWidget(central_widget)

        main_layout = QHBoxLayout()
        central_widget.setLayout(main_layout)

        # Левая панель - управление
        left_panel = QWidget()
        left_layout = QVBoxLayout()
        left_panel.setLayout(left_layout)
        left_panel.setMaximumWidth(400)

        # Группа конфигурации
        config_group = QGroupBox("Конфигурация сети")
        config_layout = QVBoxLayout()

        # Количество узлов
        nodes_layout = QHBoxLayout()
        nodes_layout.addWidget(QLabel("Количество узлов:"))
        self.num_nodes_spin = QSpinBox()
        self.num_nodes_spin.setRange(2, 10)
        self.num_nodes_spin.setValue(5)
        nodes_layout.addWidget(self.num_nodes_spin)
        config_layout.addLayout(nodes_layout)

        # Кнопки управления
        self.gen_btn = QPushButton("🎲 Сгенерировать топологию")
        self.gen_btn.clicked.connect(self.generate_topology)
        config_layout.addWidget(self.gen_btn)

        self.start_btn = QPushButton("▶ Запустить сеть")
        self.start_btn.clicked.connect(self.start_network)
        self.start_btn.setEnabled(False)
        config_layout.addWidget(self.start_btn)

        self.stop_btn = QPushButton("⏹ Остановить сеть")
        self.stop_btn.clicked.connect(self.stop_network)
        self.stop_btn.setEnabled(False)
        config_layout.addWidget(self.stop_btn)

        config_group.setLayout(config_layout)
        left_layout.addWidget(config_group)

        # Группа отправки сообщений
        message_group = QGroupBox("Отправка сообщений")
        message_layout = QVBoxLayout()

        message_layout.addWidget(QLabel("Отправитель:"))
        self.sender_combo = QComboBox()
        message_layout.addWidget(self.sender_combo)

        message_layout.addWidget(QLabel("Получатель:"))
        self.receiver_combo = QComboBox()
        message_layout.addWidget(self.receiver_combo)

        message_layout.addWidget(QLabel("Сообщение:"))
        self.message_input = QLineEdit()
        self.message_input.setPlaceholderText("Введите сообщение...")
        message_layout.addWidget(self.message_input)

        self.send_btn = QPushButton("📤 Отправить")
        self.send_btn.clicked.connect(self.send_message)
        message_layout.addWidget(self.send_btn)

        message_group.setLayout(message_layout)
        left_layout.addWidget(message_group)

        # Информация о топологии
        info_group = QGroupBox("Информация")
        info_layout = QVBoxLayout()

        self.info_text = QTextEdit()
        self.info_text.setReadOnly(True)
        self.info_text.setMaximumHeight(200)
        self.info_text.setFont(QFont("Courier", 9))
        info_layout.addWidget(self.info_text)

        info_group.setLayout(info_layout)
        left_layout.addWidget(info_group)

        left_layout.addStretch()

        main_layout.addWidget(left_panel)

        # Правая панель - визуализация
        right_panel = QWidget()
        right_layout = QVBoxLayout()
        right_panel.setLayout(right_layout)

        # Граф сети
        self.graph_widget = NetworkGraphWidget()
        self.graph_widget.node_clicked.connect(self.on_node_clicked)
        right_layout.addWidget(self.graph_widget)

        main_layout.addWidget(right_panel)

    def apply_dark_theme(self):
        """Применить темную тему"""
        self.setStyleSheet("""
            QMainWindow {
                background-color: #2b2b2b;
            }
            QWidget {
                background-color: #2b2b2b;
                color: #ffffff;
            }
            QGroupBox {
                border: 2px solid #555555;
                border-radius: 5px;
                margin-top: 10px;
                font-weight: bold;
                padding: 10px;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 5px;
            }
            QPushButton {
                background-color: #4a4a4a;
                border: 1px solid #666666;
                border-radius: 5px;
                padding: 8px;
                font-size: 12px;
            }
            QPushButton:hover {
                background-color: #5a5a5a;
            }
            QPushButton:pressed {
                background-color: #3a3a3a;
            }
            QPushButton:disabled {
                background-color: #333333;
                color: #666666;
            }
            QLineEdit, QSpinBox, QComboBox {
                background-color: #3a3a3a;
                border: 1px solid #555555;
                border-radius: 3px;
                padding: 5px;
            }
            QTextEdit {
                background-color: #1e1e1e;
                border: 1px solid #555555;
                border-radius: 3px;
            }
            QLabel {
                color: #ffffff;
            }
        """)

    def generate_topology(self):
        num_nodes = self.num_nodes_spin.value()

        self.topology = NetworkTopology(num_nodes)
        self.topology.generate_random_topology()

        # Обновляем граф
        self.graph_widget.set_topology(self.topology)

        # Обновляем комбобоксы
        self.sender_combo.clear()
        self.receiver_combo.clear()
        self.sender_combo.addItems(self.topology.nodes)
        self.receiver_combo.addItems(self.topology.nodes)

        # Обновляем информацию
        info = f"Тип: {self.topology.get_topology_type()}\n"
        info += f"Узлов: {len(self.topology.nodes)}\n"
        total_connections = sum(len(neighbors) for neighbors in self.topology.connections.values()) // 2
        avg_neighbors = sum(len(neighbors) for neighbors in self.topology.connections.values()) / len(self.topology.nodes)
        info += f"Связей: {total_connections}\n"
        info += f"Среднее соседей: {avg_neighbors:.1f}\n\n"

        info += "Связи:\n"
        for node, neighbors in sorted(self.topology.connections.items()):
            if neighbors:
                info += f"{node}: {', '.join(sorted(neighbors))}\n"

        self.info_text.setText(info)

        self.start_btn.setEnabled(True)
        QMessageBox.information(self, "Топология", f"Сгенерирована топология для {num_nodes} узлов")

    def start_network(self):
        if not self.topology:
            QMessageBox.warning(self, "Ошибка", "Сначала сгенерируйте топологию")
            return

        for i, node_id in enumerate(self.topology.nodes):
            port = self.base_port + i

            neighbor_ports = []
            for neighbor in self.topology.connections[node_id]:
                neighbor_idx = self.topology.nodes.index(neighbor)
                neighbor_ports.append(self.base_port + neighbor_idx)

            is_time_master = (i == 0)
            node = MeshNode(node_id, port, neighbor_ports, is_time_master)

            # Добавляем callback для уведомления о сообщении
            def make_callback(nid):
                return lambda sender, msg, e2e: self.msg_signal.received.emit(nid, sender, msg, e2e)
            
            node.message_callbacks.append(make_callback(node_id))

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

        # Даем узлам время на инициализацию
        time.sleep(0.5)

        # Автоматическое обнаружение соседей
        for node_id, node in self.nodes.items():
            node.discover_neighbors(self.topology.nodes)

        self.running = True
        self.start_btn.setEnabled(False)
        self.stop_btn.setEnabled(True)
        self.gen_btn.setEnabled(False)

        QMessageBox.information(self, "Сеть",
                              f"Запущено {len(self.nodes)} узлов\n"
                              f"Маршруты к прямым соседям установлены")

    def stop_network(self):
        self.nodes.clear()
        self.node_threads.clear()
        self.running = False

        self.start_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)
        self.gen_btn.setEnabled(True)

        QMessageBox.information(self, "Сеть", "Сеть остановлена")

    def send_message(self):
        sender = self.sender_combo.currentText()
        receiver = self.receiver_combo.currentText()
        message = self.message_input.text()

        if not sender or not receiver or not message:
            QMessageBox.warning(self, "Ошибка", "Заполните все поля")
            return

        if not self.running:
            QMessageBox.warning(self, "Ошибка", "Сеть не запущена")
            return

        node = self.nodes[sender]

        # Проверяем маршрут
        route = node.find_route(receiver)
        if not route:
            node.send_rreq(receiver)
            QMessageBox.information(self, "Маршрутизация",
                                  f"Маршрут к {receiver} не найден.\n"
                                  f"Инициирован поиск маршрута (RREQ).\n"
                                  f"Попробуйте отправить через 2-3 секунды.")
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

        QMessageBox.information(self, "Отправлено",
                              f"Сообщение отправлено\n"
                              f"От: {sender}\n"
                              f"Кому: {receiver}\n"
                              f"Содержимое: {message}\n"
                              f"E2E: {e2e_encrypted}\n"
                              f"E2E MIC: {pkt['e2e_mic']}\n"
                              f"Link MIC: {pkt['link_mic']}")

        self.message_input.clear()

    def show_received_message(self, receiver_id, sender_id, message, e2e):
        """Показать всплывающее окно при получении сообщения"""
        encryption_type = "E2E (Зашифровано)" if e2e else "Link (Общее шифрование)"
        QMessageBox.information(self, f"Получено сообщение: {receiver_id}",
                              f"Узел {receiver_id} получил сообщение\n"
                              f"От: {sender_id}\n"
                              f"Тип: {encryption_type}\n\n"
                              f"Содержимое: \"{message}\"")

    def on_node_clicked(self, node_id):
        if node_id not in self.nodes:
            QMessageBox.warning(self, "Ошибка", f"Узел {node_id} не запущен")
            return

        dialog = ConsoleDialog(self, node_id, self.nodes[node_id])
        dialog.exec_()

def main():
    app = QApplication(sys.argv)
    window = MeshConfiguratorQt()
    window.show()
    sys.exit(app.exec_())

if __name__ == "__main__":
    main()
