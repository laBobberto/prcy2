#!/usr/bin/env python3
"""
Test script for PRCY Mesh Network.
Connects to both STM32 boards via USB CDC and tests communication.

Usage:
    python3 test_mesh.py
    python3 test_mesh.py /dev/ttyACM0 /dev/ttyACM1
"""

import sys
import time
import serial
import threading

# Default ports (auto-detect or specify as arguments)
DEFAULT_PORT1 = "/dev/ttyACM0"
DEFAULT_PORT2 = "/dev/ttyACM1"
BAUD = 115200
TIMEOUT = 2


class MeshNode:
    def __init__(self, port, name):
        self.port = port
        self.name = name
        self.ser = None
        self.lines = []
        self.running = False
        self.thread = None

    def connect(self):
        try:
            self.ser = serial.Serial(self.port, BAUD, timeout=TIMEOUT)
            self.running = True
            self.thread = threading.Thread(target=self._reader, daemon=True)
            self.thread.start()
            time.sleep(0.5)
            return True
        except Exception as e:
            print(f"  [{self.name}] Failed to open {self.port}: {e}")
            return False

    def _reader(self):
        while self.running and self.ser and self.ser.is_open:
            try:
                line = self.ser.readline()
                if line:
                    text = line.decode("utf-8", errors="replace").strip()
                    if text:
                        self.lines.append((time.time(), text))
            except:
                break

    def send(self, cmd):
        if self.ser and self.ser.is_open:
            self.ser.write((cmd + "\r\n").encode())
            self.ser.flush()

    def get_new_lines(self):
        lines = self.lines[:]
        self.lines.clear()
        return lines

    def wait_for(self, pattern, timeout=5):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for ts, line in self.lines:
                if pattern in line:
                    return line
            time.sleep(0.1)
        return None

    def close(self):
        self.running = False
        if self.ser and self.ser.is_open:
            self.ser.close()


def print_banner(text):
    print(f"\n{'='*50}")
    print(f"  {text}")
    print(f"{'='*50}")


def test_basic_connectivity(node1, node2):
    print_banner("TEST 1: Basic Connectivity")

    # Clear old messages
    node1.get_new_lines()
    node2.get_new_lines()

    # Send help command to node1
    print("  [>] Sending 'h' to Node 1...")
    node1.send("h")
    time.sleep(1)

    lines = node1.get_new_lines()
    found_help = any("COMMANDS" in line for _, line in lines)
    print(f"  [{'OK' if found_help else 'FAIL'}] Node 1 responded to 'h' command")
    for _, line in lines:
        if "COMMANDS" in line or "===" in line:
            print(f"       {line}")

    # Send help command to node2
    print("  [>] Sending 'h' to Node 2...")
    node2.send("h")
    time.sleep(1)

    lines = node2.get_new_lines()
    found_help = any("COMMANDS" in line for _, line in lines)
    print(f"  [{'OK' if found_help else 'FAIL'}] Node 2 responded to 'h' command")

    return found_help


def test_stats(node1, node2):
    print_banner("TEST 2: Statistics")

    node1.get_new_lines()
    node2.get_new_lines()

    print("  [>] Requesting stats from Node 1...")
    node1.send("i")
    time.sleep(1)

    lines = node1.get_new_lines()
    for _, line in lines:
        if any(kw in line for kw in ["TX packets", "RX packets", "RSSI", "Routes", "Battery"]):
            print(f"       {line}")

    print("  [>] Requesting stats from Node 2...")
    node2.send("i")
    time.sleep(1)

    lines = node2.get_new_lines()
    for _, line in lines:
        if any(kw in line for kw in ["TX packets", "RX packets", "RSSI", "Routes", "Battery"]):
            print(f"       {line}")


def test_ping(node1, node2):
    print_banner("TEST 3: PING/PONG")

    node1.get_new_lines()
    node2.get_new_lines()

    print("  [>] Sending PING from Node 1 to Node 2...")
    node1.send("p 2")
    time.sleep(3)

    # Check node1 for PONG reply
    lines = node1.get_new_lines()
    pong = any("PONG" in line for _, line in lines)
    print(f"  [{'OK' if pong else 'FAIL'}] Node 1 received PONG")
    for _, line in lines:
        if "PONG" in line or "PING" in line:
            print(f"       {line}")

    # Check node2 for PING received
    lines2 = node2.get_new_lines()
    ping_rx = any("PING" in line for _, line in lines2)
    print(f"  [{'OK' if ping_rx else 'FAIL'}] Node 2 received PING")
    for _, line in lines2:
        if "PING" in line:
            print(f"       {line}")

    return pong


def test_data_send(node1, node2):
    print_banner("TEST 4: Data Send (s 2 hello)")

    node1.get_new_lines()
    node2.get_new_lines()

    print("  [>] Sending 's 2 hello' from Node 1...")
    node1.send("s 2 hello")
    time.sleep(3)

    # Check node1
    lines1 = node1.get_new_lines()
    sent = any("Sending to node" in line or "CMD" in line for _, line in lines1)
    print(f"  [{'OK' if sent else 'FAIL'}] Node 1 processed send command")
    for _, line in lines1:
        if "CMD" in line or "Sending" in line or "hello" in line:
            print(f"       {line}")

    # Check node2 for received data
    lines2 = node2.get_new_lines()
    received = any("hello" in line for _, line in lines2)
    print(f"  [{'OK' if received else 'FAIL'}] Node 2 received data")
    for _, line in lines2:
        if "hello" in line or "Received data" in line:
            print(f"       {line}")

    # Check node1 for echo
    time.sleep(2)
    lines1 = node1.get_new_lines()
    echo = any("PONG" in line or "echo" in line.lower() or "hello" in line for _, line in lines1)
    print(f"  [{'OK' if echo else '..'}] Node 1 received echo (if loopback)")

    return sent


def test_version(node1, node2):
    print_banner("TEST 5: Firmware Version")

    node1.get_new_lines()
    node2.get_new_lines()

    node1.send("v")
    time.sleep(1)
    lines = node1.get_new_lines()
    for _, line in lines:
        if "Version" in line or "Build" in line or "Node ID" in line:
            print(f"       {line}")

    node2.send("v")
    time.sleep(1)
    lines = node2.get_new_lines()
    for _, line in lines:
        if "Version" in line or "Build" in line or "Node ID" in line:
            print(f"       {line}")


def main():
    port1 = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PORT1
    port2 = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_PORT2

    print_banner("PRCY Mesh Network Test Suite")
    print(f"  Node 1: {port1}")
    print(f"  Node 2: {port2}")

    node1 = MeshNode(port1, "Node1")
    node2 = MeshNode(port2, "Node2")

    if not node1.connect():
        sys.exit(1)
    if not node2.connect():
        node1.close()
        sys.exit(1)

    print("  Connected to both nodes.")

    # Wait for boot
    print("  Waiting for nodes to boot...")
    time.sleep(2)
    node1.get_new_lines()
    node2.get_new_lines()

    results = {}

    try:
        results["connectivity"] = test_basic_connectivity(node1, node2)
        test_version(node1, node2)
        test_stats(node1, node2)
        results["ping"] = test_ping(node1, node2)
        results["data"] = test_data_send(node1, node2)
    except KeyboardInterrupt:
        print("\n  Interrupted.")
    finally:
        node1.close()
        node2.close()

    # Summary
    print_banner("RESULTS")
    for test, passed in results.items():
        status = "PASS" if passed else "FAIL"
        print(f"  [{status}] {test}")

    total = len(results)
    passed = sum(1 for v in results.values() if v)
    print(f"\n  {passed}/{total} tests passed")


if __name__ == "__main__":
    main()
