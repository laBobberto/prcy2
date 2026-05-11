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

# Import NetworkTopology from the original configurator if possible, 
# or just redefine it for simplicity.
# We'll redefine a simplified version or reuse the one from mesh_configurator_qt.
from mesh_configurator_qt import NetworkTopology, NetworkGraphWidget, ConsoleDialog, MessageSignal

class RenodeNodeProxy(QObject):
    """Proxy for an STM32 node running inside Renode"""
    message_received = pyqtSignal(str, str, str, bool) # receiver_id, sender_id, message, e2e
    output_received = pyqtSignal(str)

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
        except Exception as e:
            print(f"Failed to connect to {self.node_id} on port {self.port}: {e}")
            return False

    def _listen(self):
        buffer = ""
        while self.running:
            try:
                data = self.sock.recv(1024).decode('utf-8', errors='ignore')
                if not data:
                    break
                
                buffer += data
                self.full_log += data
                self.output_received.emit(data)

                # Parse lines for messages
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    line = line.strip()
                    
                    # Pattern: *** E2E MSG FROM NODE 1: [hello] ***
                    e2e_match = re.search(r"\*\*\* E2E MSG FROM NODE (\d+): \[(.*?)\] \*\*\*", line)
                    if e2e_match:
                        sender_id = f"NODE{e2e_match.group(1)}"
                        msg = e2e_match.group(2)
                        self.message_received.emit(self.node_id, sender_id, msg, True)
                        continue

                    link_match = re.search(r"\*\*\* LINK MSG FROM NODE (\d+): \[(.*?)\] \*\*\*", line)
                    if link_match:
                        sender_id = f"NODE{link_match.group(1)}"
                        msg = link_match.group(2)
                        self.message_received.emit(self.node_id, sender_id, msg, False)

            except socket.timeout:
                continue
            except Exception as e:
                print(f"Error in listener for {self.node_id}: {e}")
                break
        self.running = False

    def send_command(self, cmd):
        if self.sock and self.running:
            try:
                self.sock.send((cmd + "\n").encode())
            except Exception as e:
                print(f"Failed to send command to {self.node_id}: {e}")

    def disconnect(self):
        self.running = False
        if self.sock:
            self.sock.close()

class MeshConfiguratorRenode(QMainWindow):
    def __init__(self):
        super().__init__()
        self.nodes = {}
        self.topology = None
        self.renode_process = None
        self.msg_signal = MessageSignal()
        self.msg_signal.received.connect(self.show_received_message)
        
        self.init_ui()
        self.apply_styles()

    def init_ui(self):
        self.setWindowTitle("Mesh Network Configurator - Renode Edition (STM32)")
        self.setGeometry(100, 100, 1400, 800)

        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QHBoxLayout()
        central_widget.setLayout(main_layout)

        # Left Panel
        left_panel = QWidget()
        left_layout = QVBoxLayout()
        left_panel.setLayout(left_layout)
        left_panel.setFixedWidth(350)

        # Config Group
        config_group = QGroupBox("Renode STM32 Network")
        config_layout = QVBoxLayout()
        
        nodes_input_layout = QHBoxLayout()
        nodes_input_layout.addWidget(QLabel("Nodes (2-5):"))
        self.num_nodes_spin = QSpinBox()
        self.num_nodes_spin.setRange(2, 5)
        self.num_nodes_spin.setValue(3)
        nodes_input_layout.addWidget(self.num_nodes_spin)
        config_layout.addLayout(nodes_input_layout)

        self.start_btn = QPushButton("🚀 Launch Renode Network")
        self.start_btn.clicked.connect(self.start_renode)
        config_layout.addWidget(self.start_btn)

        self.stop_btn = QPushButton("⏹ Stop Renode")
        self.stop_btn.clicked.connect(self.stop_renode)
        self.stop_btn.setEnabled(False)
        config_layout.addWidget(self.stop_btn)
        
        config_group.setLayout(config_layout)
        left_layout.addWidget(config_group)

        # Message Group
        msg_group = QGroupBox("Send Message (LoRa)")
        msg_layout = QVBoxLayout()
        
        self.sender_combo = QComboBox()
        self.receiver_combo = QComboBox()
        self.msg_input = QLineEdit()
        self.msg_input.setPlaceholderText("Message content...")
        
        msg_layout.addWidget(QLabel("From:"))
        msg_layout.addWidget(self.sender_combo)
        msg_layout.addWidget(QLabel("To:"))
        msg_layout.addWidget(self.receiver_combo)
        msg_layout.addWidget(QLabel("Message:"))
        msg_layout.addWidget(self.msg_input)
        
        self.send_btn = QPushButton("📤 Send via Mesh")
        self.send_btn.clicked.connect(self.send_message)
        self.send_btn.setEnabled(False)
        msg_layout.addWidget(self.send_btn)
        
        msg_group.setLayout(msg_layout)
        left_layout.addWidget(msg_group)

        self.info_text = QTextEdit()
        self.info_text.setReadOnly(True)
        left_layout.addWidget(QLabel("Network Status:"))
        left_layout.addWidget(self.info_text)

        main_layout.addWidget(left_panel)

        # Right Panel - Graph
        self.graph_widget = NetworkGraphWidget()
        self.graph_widget.node_clicked.connect(self.on_node_clicked)
        main_layout.addWidget(self.graph_widget, 1)

    def apply_styles(self):
        self.setStyleSheet("""
            QMainWindow, QWidget { background-color: #1e1e1e; color: white; }
            QGroupBox { border: 1px solid #3d3d3d; border-radius: 5px; margin-top: 10px; padding: 10px; font-weight: bold; }
            QPushButton { background-color: #333; border: 1px solid #555; padding: 8px; border-radius: 4px; }
            QPushButton:hover { background-color: #444; }
            QPushButton:disabled { color: #666; background-color: #222; }
            QLineEdit, QSpinBox, QComboBox { background-color: #2d2d2d; border: 1px solid #3d3d3d; padding: 4px; color: white; }
            QTextEdit { background-color: #000; color: #0f0; font-family: 'Courier New'; border: 1px solid #3d3d3d; }
        """)

    def start_renode(self):
        num_nodes = self.num_nodes_spin.value()
        
        # 1. Generate .resc file
        resc_content = self.generate_resc(num_nodes)
        self.temp_resc = tempfile.NamedTemporaryFile(suffix=".resc", delete=False)
        self.temp_resc.write(resc_content.encode())
        self.temp_resc.close()
        
        # 2. Launch Renode
        self.info_text.append("Starting Renode...")
        self.renode_process = QProcess()
        self.renode_process.start("renode", ["--plain", "--hide-log", self.temp_resc.name])
        
        # 3. Wait and connect proxies
        QTimer.singleShot(3000, lambda: self.connect_to_nodes(num_nodes))

        self.start_btn.setEnabled(False)
        self.stop_btn.setEnabled(True)
        
        # Create topology for visualization
        self.topology = NetworkTopology(num_nodes)
        # For Renode, we assume a chain or a star for now based on the resc hub
        self.topology.generate_random_topology('medium')
        self.graph_widget.set_topology(self.topology)
        
        self.sender_combo.clear()
        self.receiver_combo.clear()
        self.sender_combo.addItems(self.topology.nodes)
        self.receiver_combo.addItems(self.topology.nodes)

    def generate_resc(self, num_nodes):
        # Base path to firmware and platform
        root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        firmware = os.path.join(root_dir, "firmware", "mesh_firmware.elf")
        
        resc = 'emulation CreateUARTHub "radio_hub"\n'
        for i in range(1, num_nodes + 1):
            port = 12340 + i
            resc += f'\n# Node {i}\nmach create "node{i}"\n'
            resc += 'machine LoadPlatformDescription @platforms/cpus/stm32f4.repl\n'
            resc += 'connector Connect sysbus.usart3 radio_hub\n'
            resc += f'emulation CreateServerSocketTerminal {port} "uart{i}"\n'
            resc += f'connector Connect sysbus.usart2 uart{i}\n'
            resc += f'sysbus LoadELF @{firmware}\n'
            resc += f'mach set "node{i}"\n'
            resc += f'cpu SetRegisterUnsafe 11 {i}\n'
        
        resc += "\nstart\n"
        return resc

    def connect_to_nodes(self, num_nodes):
        connected_count = 0
        for i in range(1, num_nodes + 1):
            node_id = f"NODE{i}"
            port = 12340 + i
            proxy = RenodeNodeProxy(node_id, port)
            if proxy.connect():
                proxy.message_received.connect(lambda r, s, m, e: self.msg_signal.received.emit(r, s, m, e))
                self.nodes[node_id] = proxy
                connected_count += 1
        
        self.info_text.append(f"Connected to {connected_count}/{num_nodes} nodes.")
        if connected_count > 0:
            self.send_btn.setEnabled(True)

    def stop_renode(self):
        for proxy in self.nodes.values():
            proxy.disconnect()
        self.nodes.clear()
        
        if self.renode_process:
            self.renode_process.terminate()
            self.renode_process = None
            
        if hasattr(self, 'temp_resc'):
            os.unlink(self.temp_resc.name)
            
        self.info_text.append("Renode stopped.")
        self.start_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)
        self.send_btn.setEnabled(False)

    def send_message(self):
        sender_id = self.sender_combo.currentText()
        receiver_id = self.receiver_combo.currentText()
        message = self.msg_input.text()
        
        if not message:
            return
            
        if sender_id not in self.nodes:
            return
            
        # Format for firmware: s <dst_id_digit> <msg>
        dst_digit = receiver_id.replace("NODE", "")
        cmd = f"s {dst_digit} {message}"
        self.nodes[sender_id].send_command(cmd)
        
        QMessageBox.information(self, "Command Sent", f"Sent to {sender_id}: {cmd}")
        self.msg_input.clear()

    def show_received_message(self, receiver_id, sender_id, message, e2e):
        enc_type = "E2E (Secure)" if e2e else "Link-layer only"
        QMessageBox.information(self, f"Message Received @ {receiver_id}", 
                              f"From: {sender_id}\nType: {enc_type}\n\nContent: {message}")

    def on_node_clicked(self, node_id):
        if node_id in self.nodes:
            # Show a simple log dialog
            log_dialog = QDialog(self)
            log_dialog.setWindowTitle(f"Log: {node_id}")
            layout = QVBoxLayout()
            text = QTextEdit()
            text.setReadOnly(True)
            text.setPlainText(self.nodes[node_id].full_log)
            layout.addWidget(text)
            log_dialog.setLayout(layout)
            log_dialog.resize(600, 400)
            log_dialog.exec_()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MeshConfiguratorRenode()
    window.show()
    sys.exit(app.exec_())
