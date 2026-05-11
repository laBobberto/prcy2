#!/usr/bin/env python3

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

from node.node import MeshNode
from node.crypto import Kuznyechik
import time
import threading
import base64

print("=== MIC (Message Integrity Code) Test ===\n")

# Node1 - отправитель
node1 = MeshNode("NODE1", 7001, [7002], is_time_master=False)
node1.internal_clock = 1000

# Node2 - промежуточный (relay)
node2 = MeshNode("NODE2", 7002, [7001, 7003], is_time_master=False)
node2.internal_clock = 1000

# Node3 - получатель
node3 = MeshNode("NODE3", 7003, [7002], is_time_master=False)
node3.internal_clock = 1000

# Устанавливаем парные ключи для E2E
key_1_3 = b"NODE1_NODE3_SECRET_KEY_2026!!!!!"
node1.set_pairwise_key("NODE3", key_1_3)
node3.set_pairwise_key("NODE1", key_1_3)

print("Setup:")
print("  NODE1 <--LoRa--> NODE2 <--LoRa--> NODE3")
print("  NODE1 and NODE3 have pairwise keys (E2E)")
print("  NODE2 is relay-only\n")

def run_node(node, duration=2):
    node.sock.settimeout(0.001)
    start = time.time()
    while time.time() - start < duration:
        try:
            data, addr = node.sock.recvfrom(4096)
            node.handle_packet(data, addr)
        except:
            pass
        node.tick()

# Запускаем узлы
t1 = threading.Thread(target=run_node, args=(node1,), daemon=True)
t2 = threading.Thread(target=run_node, args=(node2,), daemon=True)
t3 = threading.Thread(target=run_node, args=(node3,), daemon=True)

t1.start()
t2.start()
t3.start()

time.sleep(0.5)

print("Test 1: Valid E2E message with correct MIC")
print("-" * 50)

msg = b"Secret message"
padded = msg.ljust(16, b'\0')

# Сначала шифруем
encrypted = node1.pairwise_keys["NODE3"].ctr_crypt(node1.internal_clock, padded)

pkt = {
    "src": "NODE1",
    "dst": "NODE3",
    "type": "DATA",
    "ttl": 20,
    "timestamp": node1.internal_clock,
    "payload": base64.b64encode(encrypted).decode(),
    "e2e_encrypted": True,
    "e2e_mic": 0,
    "link_mic": 0
}

# Вычисляем E2E MIC от зашифрованного payload
pkt['e2e_mic'] = node1.compute_e2e_mic(pkt, node1.pairwise_keys["NODE3"])

# Вычисляем Link MIC
pkt['link_mic'] = node1.compute_link_mic(pkt)

print(f"Sending: E2E_MIC={pkt['e2e_mic']}, LINK_MIC={pkt['link_mic']}")
print(f"Debug: src={pkt['src']}, dst={pkt['dst']}, timestamp={pkt['timestamp']}")
node1.send_packet(pkt)

time.sleep(1)

print("\nTest 2: Tampered message (modified payload)")
print("-" * 50)

msg2 = b"Original message"
padded2 = msg2.ljust(16, b'\0')

# Шифруем
encrypted2 = node1.pairwise_keys["NODE3"].ctr_crypt(node1.internal_clock + 10, padded2)

pkt2 = {
    "src": "NODE1",
    "dst": "NODE3",
    "type": "DATA",
    "ttl": 20,
    "timestamp": node1.internal_clock + 10,
    "payload": base64.b64encode(encrypted2).decode(),
    "e2e_encrypted": True,
    "e2e_mic": 0,
    "link_mic": 0
}

pkt2['e2e_mic'] = node1.compute_e2e_mic(pkt2, node1.pairwise_keys["NODE3"])
pkt2['link_mic'] = node1.compute_link_mic(pkt2)

# ПОДДЕЛКА: меняем payload после вычисления MIC
tampered = base64.b64decode(pkt2['payload'])
tampered = bytearray(tampered)
tampered[0] ^= 0xFF  # Изменяем первый байт
pkt2['payload'] = base64.b64encode(bytes(tampered)).decode()

print(f"Sending tampered packet (payload modified after MIC)")
node1.send_packet(pkt2)

time.sleep(1)

print("\n=== Test Complete ===")
print("\nExpected results:")
print("  Test 1: ✓ NODE3 should receive and decrypt message")
print("  Test 2: ✗ NODE3 should reject (Link MIC mismatch)")
