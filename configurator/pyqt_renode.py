#!/usr/bin/env python3
"""
Графический конфигуратор Mesh-сети на PyQt5 (Renode Edition)
- Полностью копирует интерфейс и логику графа из оригинального конфигуратора
- Использует Renode STM32 в качестве бэкенда
"""

import sys
import os
import random
import threading
import time
import socket
import re
import tempfile
import math

try:
    from PyQt5.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout,
                                 QHBoxLayout, QPushButton, QLabel, QSpinBox,
                                 QComboBox, QLineEdit, QTextEdit, QSplitter,
                                 QGroupBox, QMessageBox, QDialog, QTabWidget)
    from PyQt5.QtCore import Qt, QTimer, QPointF, pyqtSignal, QObject, QProcess
    from PyQt5.QtGui import QPainter, QColor, QPen, QBrush, QFont, QPalette
except ImportError:
    print("Ошибка: PyQt5 не установлен")
    sys.exit(1)

# Копируем классы визуализации из оригинального конфигуратора
class NetworkTopology:
    """Генератор топологии сети (Идентичен оригиналу)"""

    def __init__(self, num_nodes):
        self.num_nodes = num_nodes
        self.nodes = []
        self.connections = {}
        self.positions = {}

    def generate_random_topology(self, strategy=None):
        self.nodes = [f"NODE{i+1}" for i in range(self.num_nodes)]
        self.connections = {node: [] for node in self.nodes}

        if strategy is None:
            strategies = ['sparse', 'medium', 'dense', 'chain', 'star', 'ring', 'tree']
            strategy = random.choice(strategies)

        if strategy == 'chain': self._generate_chain()
        elif strategy == 'star': self._generate_star()
        elif strategy == 'ring': self._generate_ring()
        elif strategy == 'tree': self._generate_tree()
        elif strategy == 'sparse': self._generate_random_with_density(1, 2)
        elif strategy == 'medium': self._generate_random_with_density(2, 3)
        elif strategy == 'dense': self._generate_random_with_density(3, 4)

        self._calculate_positions()
        return self.connections

    def _add_connection(self, node1, node2):
        if node2 not in self.connections[node1]: self.connections[node1].append(node2)
        if node1 not in self.connections[node2]: self.connections[node2].append(node1)

    def _generate_chain(self):
        for i in range(len(self.nodes) - 1): self._add_connection(self.nodes[i], self.nodes[i + 1])

    def _generate_star(self):
        center = self.nodes[0]
        for node in self.nodes[1:]: self._add_connection(center, node)

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
                candidates = [n for n in self.nodes if n != node and n not in self.connections[node]]
                if not candidates: break
                neighbor = random.choice(candidates)
                self._add_connection(node, neighbor)
                needed -= 1

    def _calculate_positions(self):
        n = len(self.nodes)
        radius = 200
        center_x, center_y = 300, 300
        for i, node in enumerate(self.nodes):
            angle = 2 * math.pi * i / n - math.pi / 2
            x = center_x + radius * math.cos(angle)
            y = center_y + radius * math.sin(angle)
            self.positions[node] = (x, y)

    def get_topology_type(self):
        if not self.connections: return "Пустая"
        neighbor_counts = [len(neighbors) for neighbors in self.connections.values()]
        avg_neighbors = sum(neighbor_counts) / len(neighbor_counts)
        max_neighbors = max(neighbor_counts)
        if max_neighbors == len(self.nodes) - 1 and neighbor_counts.count(1) == len(self.nodes) - 1: return "Звезда (Star)"
        if neighbor_counts.count(1) == 2 and neighbor_counts.count(2) == len(self.nodes) - 2: return "Цепочка (Chain)"
        if all(count == 2 for count in neighbor_counts): return "Кольцо (Ring)"
        total_edges = sum(neighbor_counts) // 2
        if total_edges == len(self.nodes) - 1: return "Дерево (Tree)"
        return "Разреженная" if avg_neighbors < 2 else "Средняя" if avg_neighbors < 3 else "Плотная"

class NetworkGraphWidget(QWidget):
    """Виджет для визуализации графа сети (Идентичен оригиналу)"""
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
        if not self.topology: return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        pen = QPen(QColor(100, 100, 100), 2)
        painter.setPen(pen)
        for node, neighbors in self.topology.connections.items():
            x1, y1 = self.topology.positions[node]
            for neighbor in neighbors:
                if node < neighbor:
                    x2, y2 = self.topology.positions[neighbor]
                    painter.drawLine(int(x1), int(y1), int(x2), int(y2))
        for node in self.topology.nodes:
            x, y = self.topology.positions[node]
            color = QColor(255, 165, 0) if node == self.selected_node else QColor(0, 200, 0) if node == "NODE1" else QColor(70, 130, 180)
            painter.setBrush(QBrush(color))
            painter.setPen(QPen(QColor(255, 255, 255), 2))
            painter.drawEllipse(int(x - 25), int(y - 25), 50, 50)
            painter.setPen(QPen(QColor(255, 255, 255)))
            painter.setFont(QFont("Arial", 10, QFont.Bold))
            text_rect = painter.boundingRect(int(x - 25), int(y - 10), 50, 20, Qt.AlignCenter, node)
            painter.drawText(text_rect, Qt.AlignCenter, node)

    def mousePressEvent(self, event):
        if not self.topology: return
        x, y = event.x(), event.y()
        for node in self.topology.nodes:
            nx, ny = self.topology.positions[node]
            if math.sqrt((x - nx)**2 + (y - ny)**2) <= 25:
                self.selected_node = node
                self.node_clicked.emit(node)
                self.update()
                break

# Бэкенд для Renode
class RenodeNodeProxy(QObject):
    message_received = pyqtSignal(str, str, str, bool)
    log_updated = pyqtSignal(str)

    def __init__(self, node_id, port):
        super().__init__()
        self.node_id = node_id
        self.port = port
        self.sock = None
        self.running = False
        self.full_log = ""

    def connect(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(0.5)
            self.sock.connect(('localhost', self.port))
            self.running = True
            threading.Thread(target=self._listen, daemon=True).start()
            return True
        except: return False

    def _listen(self):
        buffer = ""
        while self.running:
            try:
                data = self.sock.recv(4096).decode('utf-8', errors='ignore')
                if not data: break
                self.full_log += data
                self.log_updated.emit(data)
                buffer += data
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    match = re.search(r"\*\*\* (E2E|LINK) MSG FROM NODE (\d+): \[(.*?)\]", line, re.IGNORECASE)
                    if match:
                        self.message_received.emit(self.node_id, f"NODE{match.group(2)}", match.group(3), match.group(1).upper() == "E2E")
            except: continue
        self.running = False

    def send_command(self, cmd):
        if self.sock and self.running:
            try: self.sock.send((cmd + "\n").encode())
            except: pass

    def disconnect(self):
        self.running = False
        if self.sock: self.sock.close()

class ConsoleRenodeDialog(QDialog):
    def __init__(self, parent, node_id, proxy):
        super().__init__(parent)
        self.proxy = proxy
        self.setWindowTitle(f"Консоль {node_id} (Renode STM32)")
        self.resize(800, 500)
        layout = QVBoxLayout(self)
        self.text_edit = QTextEdit()
        self.text_edit.setReadOnly(True)
        self.text_edit.setStyleSheet("background-color: black; color: #00ff00; font-family: 'Courier New';")
        self.text_edit.setPlainText(proxy.full_log)
        layout.addWidget(self.text_edit)
        self.proxy.log_updated.connect(self.append_text)

    def append_text(self, text):
        self.text_edit.insertPlainText(text)
        self.text_edit.verticalScrollBar().setValue(self.text_edit.verticalScrollBar().maximum())

    def closeEvent(self, event):
        try: self.proxy.log_updated.disconnect(self.append_text)
        except: pass
        super().closeEvent(event)

class MessageSignal(QObject):
    received = pyqtSignal(str, str, str, bool)

class MeshConfiguratorRenode(QMainWindow):
    """Главное окно конфигуратора (Дубликат оригинала, но с Renode)"""

    def __init__(self):
        super().__init__()
        self.nodes = {}
        self.topology = None
        self.renode_process = None
        self.temp_resc = None
        self.msg_signal = MessageSignal()
        self.msg_signal.received.connect(self.show_received_message)
        self.init_ui()
        self.apply_dark_theme()

    def init_ui(self):
        self.setWindowTitle("Mesh Network Configurator - Renode Edition")
        self.setGeometry(100, 100, 1400, 800)
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QHBoxLayout(central_widget)

        # Левая панель (Копия оригинала)
        left_panel = QWidget()
        left_layout = QVBoxLayout(left_panel)
        left_panel.setMaximumWidth(400)

        config_group = QGroupBox("Конфигурация сети")
        config_layout = QVBoxLayout(config_group)
        nodes_layout = QHBoxLayout()
        nodes_layout.addWidget(QLabel("Количество узлов:"))
        self.num_nodes_spin = QSpinBox()
        self.num_nodes_spin.setRange(2, 10)
        self.num_nodes_spin.setValue(5)
        nodes_layout.addWidget(self.num_nodes_spin)
        config_layout.addLayout(nodes_layout)

        self.gen_btn = QPushButton("🎲 Сгенерировать топологию")
        self.gen_btn.clicked.connect(self.generate_topology)
        config_layout.addWidget(self.gen_btn)

        self.start_btn = QPushButton("▶ Запустить сеть (Renode)")
        self.start_btn.clicked.connect(self.start_network)
        self.start_btn.setEnabled(False)
        config_layout.addWidget(self.start_btn)

        self.stop_btn = QPushButton("⏹ Остановить сеть")
        self.stop_btn.clicked.connect(self.stop_network)
        self.stop_btn.setEnabled(False)
        config_layout.addWidget(self.stop_btn)
        left_layout.addWidget(config_group)

        message_group = QGroupBox("Отправка сообщений")
        message_layout = QVBoxLayout(message_group)
        self.sender_combo = QComboBox()
        self.receiver_combo = QComboBox()
        self.message_input = QLineEdit()
        self.message_input.setPlaceholderText("Введите сообщение...")
        message_layout.addWidget(QLabel("Отправитель:"))
        message_layout.addWidget(self.sender_combo)
        message_layout.addWidget(QLabel("Получатель:"))
        message_layout.addWidget(self.receiver_combo)
        message_layout.addWidget(QLabel("Сообщение:"))
        message_layout.addWidget(self.message_input)
        self.send_btn = QPushButton("📤 Отправить")
        self.send_btn.clicked.connect(self.send_message)
        message_layout.addWidget(self.send_btn)
        left_layout.addWidget(message_group)

        info_group = QGroupBox("Информация")
        info_layout = QVBoxLayout(info_group)
        self.info_text = QTextEdit()
        self.info_text.setReadOnly(True)
        self.info_text.setMaximumHeight(200)
        self.info_text.setFont(QFont("Courier", 9))
        info_layout.addWidget(self.info_text)
        left_layout.addWidget(info_group)
        left_layout.addStretch()
        main_layout.addWidget(left_panel)

        # Правая панель (Копия оригинала)
        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)
        self.graph_widget = NetworkGraphWidget()
        self.graph_widget.node_clicked.connect(self.on_node_clicked)
        right_layout.addWidget(self.graph_widget)
        main_layout.addWidget(right_panel)

    def apply_dark_theme(self):
        self.setStyleSheet("""
            QMainWindow, QWidget { background-color: #2b2b2b; color: white; }
            QGroupBox { border: 2px solid #555; border-radius: 5px; margin-top: 10px; padding: 10px; font-weight: bold; }
            QPushButton { background-color: #4a4a4a; border: 1px solid #666; padding: 8px; border-radius: 5px; }
            QPushButton:hover { background-color: #5a5a5a; }
            QPushButton:disabled { color: #666; background-color: #333; }
            QLineEdit, QSpinBox, QComboBox { background-color: #3a3a3a; border: 1px solid #555; padding: 5px; }
            QTextEdit { background-color: #1e1e1e; border: 1px solid #555; }
        """)

    def generate_topology(self):
        num_nodes = self.num_nodes_spin.value()
        self.topology = NetworkTopology(num_nodes)
        self.topology.generate_random_topology()
        self.graph_widget.set_topology(self.topology)
        self.sender_combo.clear(); self.receiver_combo.clear()
        self.sender_combo.addItems(self.topology.nodes); self.receiver_combo.addItems(self.topology.nodes)
        info = f"Тип: {self.topology.get_topology_type()}\nУзлов: {len(self.topology.nodes)}\n"
        self.info_text.setText(info)
        self.start_btn.setEnabled(True)

    def start_network(self):
        num_nodes = len(self.topology.nodes)
        root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        firmware = os.path.join(root_dir, "firmware", "mesh_firmware.elf")
        
        resc = 'emulation CreateUARTHub "radio_hub"\n'
        for i in range(1, num_nodes + 1):
            port = 12340 + i
            resc += f'\nmach create "node{i}"\nmachine LoadPlatformDescription @platforms/cpus/stm32f4.repl\n'
            resc += f'connector Connect sysbus.usart3 radio_hub\nemulation CreateServerSocketTerminal {port} "uart{i}"\n'
            resc += f'connector Connect sysbus.usart2 uart{i}\nsysbus LoadELF @{firmware}\n'
            resc += f'mach set "node{i}"\ncpu SetRegisterUnsafe 11 {i}\n'
        resc += "\nstart\n"

        self.temp_resc = tempfile.NamedTemporaryFile(suffix=".resc", delete=False)
        self.temp_resc.write(resc.encode()); self.temp_resc.close()

        self.renode_process = QProcess()
        self.renode_process.start("renode", ["--plain", "--hide-log", self.temp_resc.name])
        
        self.info_text.append("Запуск Renode...")
        QTimer.singleShot(3000, self.connect_proxies)
        self.start_btn.setEnabled(False); self.stop_btn.setEnabled(True)

    def connect_proxies(self):
        connected = 0
        for i in range(1, len(self.topology.nodes) + 1):
            node_id = f"NODE{i}"
            proxy = RenodeNodeProxy(node_id, 12340 + i)
            if proxy.connect():
                proxy.message_received.connect(lambda r, s, m, e: self.msg_signal.received.emit(r, s, m, e))
                self.nodes[node_id] = proxy
                connected += 1
        self.info_text.append(f"Подключено {connected} узлов Renode.")
        if connected > 0: self.send_btn.setEnabled(True)

    def stop_network(self):
        for p in self.nodes.values(): p.disconnect()
        self.nodes.clear()
        if self.renode_process: self.renode_process.terminate()
        if self.temp_resc: os.unlink(self.temp_resc.name)
        self.start_btn.setEnabled(True); self.stop_btn.setEnabled(False); self.send_btn.setEnabled(False)
        self.info_text.append("Сеть остановлена.")

    def send_message(self):
        sender, receiver, message = self.sender_combo.currentText(), self.receiver_combo.currentText(), self.message_input.text()
        if not message or sender not in self.nodes: return
        dst_id = receiver.replace("NODE", "")
        self.nodes[sender].send_command(f"s {dst_id} {message}")
        QMessageBox.information(self, "Отправлено", f"Сообщение отправлено из {sender} в {receiver}\nКонтент: {message}")
        self.message_input.clear()

    def show_received_message(self, receiver_id, sender_id, message, e2e):
        enc = "E2E (Зашифровано)" if e2e else "Link (Общее)"
        QMessageBox.information(self, f"Получено сообщение: {receiver_id}", 
                              f"Узел {receiver_id} получил сообщение от {sender_id}\nТип: {enc}\nКонтент: {message}")

    def on_node_clicked(self, node_id):
        if node_id in self.nodes:
            ConsoleRenodeDialog(self, node_id, self.nodes[node_id]).show()
        else: QMessageBox.warning(self, "Ошибка", "Узел не запущен")

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MeshConfiguratorRenode()
    window.show()
    sys.exit(app.exec_())
