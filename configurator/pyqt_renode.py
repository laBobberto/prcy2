#!/usr/bin/env python3
import sys
import os
import socket
import threading
import time
import subprocess
import tempfile
import re

try:
    from PyQt5.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout,
                                 QHBoxLayout, QPushButton, QLabel, QSpinBox,
                                 QComboBox, QLineEdit, QTextEdit, QGroupBox, 
                                 QMessageBox, QDialog)
    from PyQt5.QtCore import Qt, QTimer, pyqtSignal, QObject, QProcess
    from PyQt5.QtGui import QPainter, QColor, QPen, QBrush, QFont
except ImportError:
    print("Error: PyQt5 is not installed. Run: pip install PyQt5")
    sys.exit(1)

from mesh_configurator_qt import NetworkTopology, NetworkGraphWidget, MessageSignal

class RenodeNodeProxy(QObject):
    """Proxy for an STM32 node running inside Renode"""
    message_received = pyqtSignal(str, str, str, bool) # receiver_id, sender_id, message, e2e
    log_updated = pyqtSignal(str)

    def __init__(self, node_id, port):
        super().__init__()
        self.node_id = node_id
        self.port = port
        self.sock = None
        self.running = False
        self.thread = None
        self.full_log = ""

    def connect(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(1.0)
            self.sock.connect(('localhost', self.port))
            self.running = True
            self.thread = threading.Thread(target=self._listen, daemon=True)
            self.thread.start()
            return True
        except Exception:
            return False

    def _listen(self):
        buffer = ""
        while self.running:
            try:
                data = self.sock.recv(4096).decode('utf-8', errors='ignore')
                if not data:
                    break
                
                self.full_log += data
                self.log_updated.emit(data)

                buffer += data
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    line = line.strip()
                    
                    # Pattern matching: *** E2E/LINK MSG FROM NODE X: [msg]
                    match = re.search(r"\*\*\* (E2E|LINK) MSG FROM NODE (\d+): \[(.*?)\]", line, re.IGNORECASE)
                    if match:
                        msg_type = match.group(1).upper()
                        sender_id = f"NODE{match.group(2)}"
                        msg = match.group(3)
                        is_e2e = (msg_type == "E2E")
                        self.message_received.emit(self.node_id, sender_id, msg, is_e2e)

            except socket.timeout:
                continue
            except Exception:
                break
        self.running = False

    def send_command(self, cmd):
        if self.sock and self.running:
            try:
                self.sock.send((cmd + "\n").encode())
            except Exception:
                pass

    def disconnect(self):
        self.running = False
        if self.sock:
            try:
                self.sock.close()
            except:
                pass

class ConsoleRenodeDialog(QDialog):
    def __init__(self, parent, node_id, proxy):
        super().__init__(parent)
        self.node_id = node_id
        self.proxy = proxy
        self.setWindowTitle(f"Консоль {node_id} (Renode)")
        self.resize(800, 500)
        layout = QVBoxLayout()
        self.text_edit = QTextEdit()
        self.text_edit.setReadOnly(True)
        self.text_edit.setStyleSheet("background-color: black; color: #00ff00; font-family: 'Courier New';")
        self.text_edit.setPlainText(proxy.full_log)
        layout.addWidget(self.text_edit)
        self.setLayout(layout)
        self.proxy.log_updated.connect(self.append_text)

    def append_text(self, text):
        self.text_edit.insertPlainText(text)
        sb = self.text_edit.verticalScrollBar()
        sb.setValue(sb.maximum())

    def closeEvent(self, event):
        try:
            self.proxy.log_updated.disconnect(self.append_text)
        except:
            pass
        super().closeEvent(event)

class MeshConfiguratorRenode(QMainWindow):
    def __init__(self):
        super().__init__()
        self.nodes = {}
        self.topology = None
        self.renode_process = None
        self.msg_signal = MessageSignal()
        self.msg_signal.received.connect(self.show_received_message)
        self.temp_resc_path = None
        
        self.init_ui()
        self.apply_styles()

    def init_ui(self):
        self.setWindowTitle("Mesh Network Configurator - Renode Edition")
        self.setGeometry(100, 100, 1200, 800)

        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QHBoxLayout()
        central_widget.setLayout(main_layout)

        left_panel = QWidget()
        left_layout = QVBoxLayout()
        left_panel.setLayout(left_layout)
        left_panel.setFixedWidth(350)

        config_group = QGroupBox("Renode Network Control")
        config_layout = QVBoxLayout()
        
        nodes_input_layout = QHBoxLayout()
        nodes_input_layout.addWidget(QLabel("Nodes (2-5):"))
        self.num_nodes_spin = QSpinBox()
        self.num_nodes_spin.setRange(2, 5)
        self.num_nodes_spin.setValue(3)
        nodes_input_layout.addWidget(self.num_nodes_spin)
        config_layout.addLayout(nodes_input_layout)

        self.start_btn = QPushButton("🚀 Start Renode")
        self.start_btn.clicked.connect(self.start_renode)
        config_layout.addWidget(self.start_btn)

        self.stop_btn = QPushButton("⏹ Stop Renode")
        self.stop_btn.clicked.connect(self.stop_renode)
        self.stop_btn.setEnabled(False)
        config_layout.addWidget(self.stop_btn)
        
        config_group.setLayout(config_layout)
        left_layout.addWidget(config_group)

        msg_group = QGroupBox("Mesh Messaging")
        msg_layout = QVBoxLayout()
        
        self.sender_combo = QComboBox()
        self.receiver_combo = QComboBox()
        self.msg_input = QLineEdit()
        self.msg_input.setPlaceholderText("Hello Mesh!")
        
        msg_layout.addWidget(QLabel("Sender:"))
        msg_layout.addWidget(self.sender_combo)
        msg_layout.addWidget(QLabel("Destination:"))
        msg_layout.addWidget(self.receiver_combo)
        msg_layout.addWidget(QLabel("Message:"))
        msg_layout.addWidget(self.msg_input)
        
        self.send_btn = QPushButton("📤 Send Data")
        self.send_btn.clicked.connect(self.send_message)
        self.send_btn.setEnabled(False)
        msg_layout.addWidget(self.send_btn)
        
        msg_group.setLayout(msg_layout)
        left_layout.addWidget(msg_group)

        self.info_text = QTextEdit()
        self.info_text.setReadOnly(True)
        left_layout.addWidget(QLabel("Status & Events:"))
        left_layout.addWidget(self.info_text)

        main_layout.addWidget(left_panel)

        self.graph_widget = NetworkGraphWidget()
        self.graph_widget.node_clicked.connect(self.on_node_clicked)
        main_layout.addWidget(self.graph_widget, 1)

    def apply_styles(self):
        self.setStyleSheet("""
            QMainWindow, QWidget { background-color: #2b2b2b; color: white; }
            QGroupBox { border: 1px solid #555; border-radius: 5px; margin-top: 10px; padding: 10px; font-weight: bold; }
            QPushButton { background-color: #444; border: 1px solid #666; padding: 8px; border-radius: 4px; }
            QPushButton:hover { background-color: #555; }
            QPushButton:disabled { color: #888; background-color: #333; }
            QLineEdit, QSpinBox, QComboBox { background-color: #333; border: 1px solid #555; padding: 4px; color: white; }
            QTextEdit { background-color: #111; color: #0f0; font-family: 'Courier New'; }
        """)

    def start_renode(self):
        num_nodes = self.num_nodes_spin.value()
        self.stop_renode() # Ensure clean start
        
        resc_content = self.generate_resc(num_nodes)
        with tempfile.NamedTemporaryFile(suffix=".resc", delete=False) as f:
            f.write(resc_content.encode())
            self.temp_resc_path = f.name
        
        self.info_text.append(f"Starting Renode with {num_nodes} nodes...")
        self.renode_process = QProcess()
        self.renode_process.start("renode", ["--plain", "--hide-log", self.temp_resc_path])
        
        self.conn_attempts = 0
        self.target_nodes = num_nodes
        QTimer.singleShot(2000, self.attempt_connection)

        self.start_btn.setEnabled(False)
        self.stop_btn.setEnabled(True)
        
        self.topology = NetworkTopology(num_nodes)
        self.topology.generate_random_topology('medium')
        self.graph_widget.set_topology(self.topology)
        
        self.sender_combo.clear()
        self.receiver_combo.clear()
        self.sender_combo.addItems(self.topology.nodes)
        self.receiver_combo.addItems(self.topology.nodes)

    def attempt_connection(self):
        self.conn_attempts += 1
        connected_count = 0
        for i in range(1, self.target_nodes + 1):
            node_id = f"NODE{i}"
            if node_id in self.nodes and self.nodes[node_id].running:
                connected_count += 1
                continue
                
            port = 12340 + i
            proxy = RenodeNodeProxy(node_id, port)
            if proxy.connect():
                proxy.message_received.connect(lambda r, s, m, e: self.msg_signal.received.emit(r, s, m, e))
                self.nodes[node_id] = proxy
                connected_count += 1
        
        if connected_count < self.target_nodes and self.conn_attempts < 10:
            QTimer.singleShot(1000, self.attempt_connection)
        else:
            self.info_text.append(f"Connected to {connected_count}/{self.target_nodes} nodes.")
            if connected_count > 0:
                self.send_btn.setEnabled(True)

    def generate_resc(self, num_nodes):
        root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        firmware = os.path.join(root_dir, "firmware", "mesh_firmware.elf")
        
        resc = 'emulation CreateUARTHub "radio_hub"\n'
        for i in range(1, num_nodes + 1):
            port = 12340 + i
            resc += f'\nmach create "node{i}"\n'
            resc += 'machine LoadPlatformDescription @platforms/cpus/stm32f4.repl\n'
            resc += 'connector Connect sysbus.usart3 radio_hub\n'
            resc += f'emulation CreateServerSocketTerminal {port} "uart{i}"\n'
            resc += f'connector Connect sysbus.usart2 uart{i}\n'
            resc += f'sysbus LoadELF @{firmware}\n'
            resc += f'mach set "node{i}"\n'
            resc += f'cpu SetRegisterUnsafe 11 {i}\n'
        
        resc += "\nstart\n"
        return resc

    def stop_renode(self):
        for proxy in self.nodes.values():
            proxy.disconnect()
        self.nodes.clear()
        
        if self.renode_process:
            self.renode_process.terminate()
            if not self.renode_process.waitForFinished(2000):
                self.renode_process.kill()
            self.renode_process = None
            
        if self.temp_resc_path and os.path.exists(self.temp_resc_path):
            try: os.unlink(self.temp_resc_path)
            except: pass
            self.temp_resc_path = None
            
        self.info_text.append("Renode stopped.")
        self.start_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)
        self.send_btn.setEnabled(False)

    def send_message(self):
        sender_id = self.sender_combo.currentText()
        receiver_id = self.receiver_combo.currentText()
        message = self.msg_input.text()
        if not message or sender_id not in self.nodes: return
            
        dst_digit = receiver_id.replace("NODE", "")
        self.nodes[sender_id].send_command(f"s {dst_digit} {message}")
        self.info_text.append(f"Command sent: {sender_id} -> {receiver_id}: {message}")
        self.msg_input.clear()

    def show_received_message(self, receiver_id, sender_id, message, e2e):
        enc = "E2E" if e2e else "Link"
        self.info_text.append(f"MSG received: {sender_id} -> {receiver_id} ({enc}): {message}")
        QMessageBox.information(self, f"New Message @ {receiver_id}", 
                              f"From: {sender_id}\nType: {enc}\nContent: {message}")

    def on_node_clicked(self, node_id):
        if node_id in self.nodes:
            dialog = ConsoleRenodeDialog(self, node_id, self.nodes[node_id])
            dialog.show()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MeshConfiguratorRenode()
    window.show()
    sys.exit(app.exec_())
