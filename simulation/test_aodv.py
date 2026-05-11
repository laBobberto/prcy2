#!/usr/bin/env python3
"""
Test AODV routing protocol implementation
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from simulation.node.node import MeshNode
from simulation.node.crypto import Kuznyechik
import time
import base64

def test_route_discovery():
    """Test RREQ/RREP route discovery"""
    print("\n=== Test 1: Route Discovery ===")

    # Create 3 nodes in a line: NODE1 -- NODE2 -- NODE3
    node1 = MeshNode("NODE1", 5001, [5002], is_time_master=True)
    node2 = MeshNode("NODE2", 5002, [5001, 5003])
    node3 = MeshNode("NODE3", 5003, [5002])

    # Set pairwise keys
    key13 = b"NODE1_TO_NODE3_SECRET_2026!!!!!!"
    node1.set_pairwise_key("NODE3", key13)
    node3.set_pairwise_key("NODE1", key13)

    print("Topology: NODE1 -- NODE2 -- NODE3")

    # Start nodes in background
    import threading
    threads = []
    for node in [node1, node2, node3]:
        t = threading.Thread(target=node.run, daemon=True)
        t.start()
        threads.append(t)

    time.sleep(0.5)  # Let nodes start

    print("NODE1 initiating route discovery to NODE3...")

    # NODE1 sends RREQ
    node1.send_rreq("NODE3")

    # Wait for RREQ/RREP propagation
    time.sleep(1)

    # Check if route was established
    route = node1.find_route("NODE3")
    if route:
        print(f"✓ Route established: NODE1 -> NODE3 via {route['next_hop']}")
        print(f"  Hop count: {route['hop_count']}")
        print(f"  Seq num: {route['seq_num']}")
    else:
        print("✗ Route discovery failed")

    return route is not None

def test_data_with_routing():
    """Test data transmission using routing table"""
    print("\n=== Test 2: Data Transmission with Routing ===")

    # Create 3 nodes
    node1 = MeshNode("NODE1", 5011, [5012], is_time_master=True)
    node2 = MeshNode("NODE2", 5012, [5011, 5013])
    node3 = MeshNode("NODE3", 5013, [5012])

    # Set pairwise keys
    key13 = b"NODE1_TO_NODE3_SECRET_2026!!!!!!"
    node1.set_pairwise_key("NODE3", key13)
    node3.set_pairwise_key("NODE1", key13)

    # Manually add routes (simulating successful RREQ/RREP)
    node1.add_route("NODE3", "NODE2", 2, 1)
    node2.add_route("NODE3", "NODE3", 1, 1)
    node2.add_route("NODE1", "NODE1", 1, 1)
    node3.add_route("NODE1", "NODE2", 2, 1)

    print("Routes established manually")
    print("NODE1 sending E2E encrypted message to NODE3...")

    # Prepare message
    msg = b"Secret via routing"
    payload_len = ((len(msg) + 15) // 16) * 16
    padded = msg.ljust(payload_len, b'\0')

    pkt = {
        "src": "NODE1",
        "dst": "NODE3",
        "type": "DATA",
        "ttl": 20,
        "timestamp": node1.internal_clock,
        "payload": base64.b64encode(padded).decode(),
        "e2e_encrypted": True,
        "e2e_mic": 0,
        "link_mic": 0
    }

    # Compute MICs
    pkt['e2e_mic'] = node1.compute_e2e_mic(pkt, node1.pairwise_keys["NODE3"])
    encrypted = node1.pairwise_keys["NODE3"].ctr_crypt(node1.internal_clock, padded)
    pkt['payload'] = base64.b64encode(encrypted).decode()
    pkt['link_mic'] = node1.compute_link_mic(pkt)

    print(f"Packet prepared: E2E_MIC={pkt['e2e_mic']}, LINK_MIC={pkt['link_mic']}")

    # Send packet
    node1.send_packet(pkt)

    print("✓ Packet sent using routing table")
    return True

def test_route_expiration():
    """Test route lifetime and cleanup"""
    print("\n=== Test 3: Route Expiration ===")

    node = MeshNode("NODE1", 5021, [5022], is_time_master=True)

    # Add route with short lifetime
    node.add_route("NODE2", "NODE2", 1, 1)
    node.routing_table["NODE2"]["lifetime"] = node.internal_clock + 100

    print(f"Route added with lifetime={node.routing_table['NODE2']['lifetime']}")

    # Advance clock
    for _ in range(150):
        node.internal_clock += 1

    print(f"Clock advanced to {node.internal_clock}")

    # Cleanup
    node.cleanup_routes()

    # Check if route expired
    route = node.find_route("NODE2")
    if route is None:
        print("✓ Route expired and cleaned up")
        return True
    else:
        print("✗ Route still exists")
        return False

def test_memory_cleanup():
    """Test cleanup of old timestamps and RREQ records"""
    print("\n=== Test 4: Memory Cleanup ===")

    node = MeshNode("NODE1", 5031, [5032], is_time_master=True)

    # Add old seen packets
    node.seen_packets.add("NODE2_1000")
    node.seen_packets.add("NODE3_2000")

    # Add old RREQ records
    node.seen_rreq["NODE2_1"] = 1000
    node.seen_rreq["NODE3_2"] = 2000

    print(f"Added old records at clock={node.internal_clock}")
    print(f"Seen packets: {len(node.seen_packets)}")
    print(f"Seen RREQ: {len(node.seen_rreq)}")

    # Advance clock beyond cleanup threshold
    node.internal_clock = 70000

    # Cleanup
    node.cleanup_old_data()

    print(f"After cleanup at clock={node.internal_clock}:")
    print(f"Seen packets: {len(node.seen_packets)}")
    print(f"Seen RREQ: {len(node.seen_rreq)}")

    if len(node.seen_packets) == 0 and len(node.seen_rreq) == 0:
        print("✓ Old data cleaned up")
        return True
    else:
        print("✗ Cleanup failed")
        return False

def main():
    print("=" * 60)
    print("AODV Routing Protocol Tests")
    print("=" * 60)

    results = []

    try:
        results.append(("Route Discovery", test_route_discovery()))
    except Exception as e:
        print(f"✗ Test failed with error: {e}")
        results.append(("Route Discovery", False))

    try:
        results.append(("Data with Routing", test_data_with_routing()))
    except Exception as e:
        print(f"✗ Test failed with error: {e}")
        results.append(("Data with Routing", False))

    try:
        results.append(("Route Expiration", test_route_expiration()))
    except Exception as e:
        print(f"✗ Test failed with error: {e}")
        results.append(("Route Expiration", False))

    try:
        results.append(("Memory Cleanup", test_memory_cleanup()))
    except Exception as e:
        print(f"✗ Test failed with error: {e}")
        results.append(("Memory Cleanup", False))

    print("\n" + "=" * 60)
    print("Test Results:")
    print("=" * 60)

    for name, passed in results:
        status = "✓ PASS" if passed else "✗ FAIL"
        print(f"{status}: {name}")

    total = len(results)
    passed = sum(1 for _, p in results if p)
    print(f"\nTotal: {passed}/{total} tests passed")

    return passed == total

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
