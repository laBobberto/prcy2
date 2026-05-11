#!/usr/bin/env python3

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

from node.node import MeshNode
import time
import threading

print("=== Time Synchronization Test ===\n")

# Node1 - Time Master
node1 = MeshNode("NODE1", 6001, [6002], is_time_master=True)
node1.internal_clock = 1000

# Node2 - Relay (not time master)
node2 = MeshNode("NODE2", 6002, [6001, 6003], is_time_master=False)
node2.internal_clock = 50  # Сильно отстает

# Node3 - End node (not time master)
node3 = MeshNode("NODE3", 6003, [6002], is_time_master=False)
node3.internal_clock = 980  # Немного отстает

print(f"Initial clocks:")
print(f"  NODE1 (master): {node1.internal_clock}")
print(f"  NODE2 (relay):  {node2.internal_clock}")
print(f"  NODE3 (end):    {node3.internal_clock}\n")

def run_node(node):
    node.sock.settimeout(0.001)
    for _ in range(10000):
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

print("Running simulation for 3 seconds...\n")
time.sleep(3)

print(f"\nFinal clocks:")
print(f"  NODE1 (master): {node1.internal_clock}")
print(f"  NODE2 (relay):  {node2.internal_clock}")
print(f"  NODE3 (end):    {node3.internal_clock}\n")

# Проверка синхронизации
diff_2_1 = abs(node2.internal_clock - node1.internal_clock)
diff_3_1 = abs(node3.internal_clock - node1.internal_clock)

print(f"Clock differences from master:")
print(f"  NODE2: {diff_2_1} ticks")
print(f"  NODE3: {diff_3_1} ticks\n")

if diff_2_1 < 100 and diff_3_1 < 100:
    print("✓ SUCCESS: All nodes synchronized within 100 ticks")
else:
    print("✗ FAILURE: Nodes not synchronized")

print("\n=== Test Complete ===")
