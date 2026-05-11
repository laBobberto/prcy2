#!/usr/bin/env python3

import sys
sys.path.insert(0, '.')

from node.node import MeshNode
import time

# Создаем 3 узла
print("=== Setting up 3-node mesh with E2E encryption ===\n")

# Node 1 (порт 5001)
node1 = MeshNode("NODE1", 5001, [5002])
node1.set_pairwise_key("NODE3", b"NODE1_NODE3_SECRET_KEY_2026!!!!!")

# Node 2 (порт 5002, промежуточный)
node2 = MeshNode("NODE2", 5002, [5001, 5003])
# Node2 НЕ имеет парных ключей - только пересылает

# Node 3 (порт 5003)
node3 = MeshNode("NODE3", 5003, [5002])
node3.set_pairwise_key("NODE1", b"NODE1_NODE3_SECRET_KEY_2026!!!!!")

print("✓ Node1 (5001) <-> Node2 (5002) <-> Node3 (5003)")
print("✓ Node1 and Node3 have pairwise keys")
print("✓ Node2 is relay-only (can't decrypt E2E messages)\n")

# Запускаем узлы в отдельных потоках
import threading

def run_node(node):
    node.run()

threading.Thread(target=run_node, args=(node1,), daemon=True).start()
threading.Thread(target=run_node, args=(node2,), daemon=True).start()
threading.Thread(target=run_node, args=(node3,), daemon=True).start()

time.sleep(1)

print("=== Test 1: E2E encrypted message (Node1 -> Node3) ===")
msg1 = b"Secret message for Node3 only"
padded1 = msg1.ljust(32, b'\0')
timestamp1 = int(time.time())

encrypted1 = node1.pairwise_keys["NODE3"].ctr_crypt(timestamp1, padded1)
import base64
pkt1 = {
    "src": "NODE1",
    "dst": "NODE3",
    "type": "DATA",
    "ttl": 20,
    "timestamp": timestamp1,
    "payload": base64.b64encode(encrypted1).decode(),
    "e2e_encrypted": True
}
node1.send_packet(pkt1)
print("✓ Node1 sent E2E encrypted message")

time.sleep(1)

print("\n=== Test 2: Link-layer message (Node1 -> Node3) ===")
msg2 = b"Public message, Node2 can read"
padded2 = msg2.ljust(32, b'\0')
timestamp2 = int(time.time())

encrypted2 = node1.session_crypto.ctr_crypt(timestamp2, padded2)
pkt2 = {
    "src": "NODE1",
    "dst": "NODE3",
    "type": "DATA",
    "ttl": 20,
    "timestamp": timestamp2,
    "payload": base64.b64encode(encrypted2).decode(),
    "e2e_encrypted": False
}
node1.send_packet(pkt2)
print("✓ Node1 sent link-layer encrypted message")

time.sleep(2)

print("\n=== Test Complete ===")
print("Check logs above:")
print("- Node3 should receive both messages")
print("- Node2 can only read the link-layer message")
