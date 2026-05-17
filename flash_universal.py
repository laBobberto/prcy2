#!/usr/bin/env python3
"""
Universal flash tool for PRCY Mesh Network.

Flashes one firmware to multiple STM32 boards, configuring
each with a unique node ID and shared network secret.

Usage:
    python3 flash_universal.py                    # Interactive mode
    python3 flash_universal.py --port /dev/ttyACM0 --node-id 1  # Direct mode
"""

import argparse
import glob
import subprocess
import sys
import time

try:
    import serial
    import serial.tools.list_ports
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False


def list_stm32_ports():
    """List available STM32 USB CDC ports."""
    ports = []
    for p in serial.tools.list_ports.comports():
        if 'ttyACM' in p.device or 'ttyUSB' in p.device:
            ports.append({
                'device': p.device,
                'description': p.description,
                'serial': p.serial_number or 'N/A',
                'hwid': p.hwid,
            })
    if not ports:
        # Fallback: glob
        for dev in sorted(glob.glob('/dev/ttyACM*')):
            ports.append({
                'device': dev,
                'description': 'STM32 USB CDC',
                'serial': 'N/A',
                'hwid': '',
            })
    return ports


def flash_firmware(port, env='node_universal'):
    """Flash firmware using PlatformIO."""
    print(f"\n[*] Flashing {env} to {port}...")
    cmd = ['pio', 'run', '-e', env, '-t', 'upload', '--upload-port', port]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        print(f"[!] Flash FAILED:\n{result.stderr[-500:]}")
        return False
    print(f"[+] Flash successful!")
    return True


def wait_for_prompt(port, timeout=30):
    """Wait for the config wizard prompt on serial."""
    print(f"[*] Waiting for config prompt on {port}...")
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        start = time.time()
        buf = b''
        while (time.time() - start) < timeout:
            data = ser.read(256)
            if data:
                buf += data
                text = buf.decode('utf-8', errors='replace')
                if 'First Boot Setup' in text or 'Enter network secret' in text:
                    print(f"[+] Config prompt detected!")
                    return ser
                # Also check if already configured
                if 'PRCY Mesh Network' in text:
                    print(f"[+] Node already configured (no wizard needed)")
                    ser.close()
                    return None
        print(f"[!] Timeout waiting for prompt")
        ser.close()
        return None
    except Exception as e:
        print(f"[!] Serial error: {e}")
        return None


def configure_node(ser, node_id, network_secret_hex):
    """Send configuration to the node via serial."""
    time.sleep(0.5)

    # Wait for "Enter network secret" prompt
    print(f"[*] Sending network secret...")
    ser.write((network_secret_hex + '\r\n').encode())
    time.sleep(1)

    # Wait for "Enter node ID" prompt
    print(f"[*] Sending node ID: {node_id}")
    ser.write((str(node_id) + '\r\n').encode())
    time.sleep(1)

    # Wait for "Save to flash? (y/n)"
    print(f"[*] Confirming save...")
    ser.write(b'y\r\n')
    time.sleep(2)

    # Read response
    response = ser.read(4096).decode('utf-8', errors='replace')
    print(response)

    if 'Saved' in response or 'Rebooting' in response:
        print(f"[+] Configuration saved! Node {node_id} will reboot.")
        return True
    else:
        print(f"[!] Configuration may not have been saved.")
        return False


def interactive_mode():
    """Interactive multi-board flash and configure."""
    print("=" * 50)
    print("  PRCY Mesh Network - Universal Flash Tool")
    print("=" * 50)

    if not HAS_SERIAL:
        print("\n[!] pyserial not installed. Install with:")
        print("    pip install pyserial")
        print("\nFalling back to manual mode.")
        manual_mode()
        return

    # List ports
    ports = list_stm32_ports()
    if not ports:
        print("\n[!] No STM32 boards found. Connect via USB and try again.")
        return

    print(f"\nFound {len(ports)} board(s):")
    for i, p in enumerate(ports):
        print(f"  [{i+1}] {p['device']} - {p['description']} (S/N: {p['serial']})")

    # Network secret
    print("\n--- Network Configuration ---")
    print("Enter network secret (64 hex chars) or press Enter for default:")
    secret = input("> ").strip()
    if not secret:
        secret = "0102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F20"
        print(f"  Using default: {secret[:16]}...")

    if len(secret) != 64:
        print(f"[!] Secret must be 64 hex chars (32 bytes). Got {len(secret)}.")
        return

    # Configure each board
    next_node_id = 1
    for i, p in enumerate(ports):
        print(f"\n--- Board {i+1}: {p['device']} ---")
        resp = input(f"Flash and configure this board? (y/n/skip to set ID): ").strip().lower()

        if resp == 'n':
            continue
        elif resp == 'skip' or resp == 's':
            try:
                next_node_id = int(input("  Enter starting node ID for next board: "))
            except ValueError:
                pass
            continue

        # Flash
        if not flash_firmware(p['device']):
            print(f"[!] Skipping {p['device']} due to flash error")
            continue

        # Wait for config prompt
        time.sleep(3)  # Wait for USB re-enumeration after flash
        ser = wait_for_prompt(p['device'])
        if ser is None:
            print(f"[!] Could not connect to {p['device']} for configuration")
            print(f"    Configure manually: open serial, press 'c', follow prompts")
            continue

        # Configure
        configure_node(ser, next_node_id, secret)
        ser.close()
        next_node_id += 1

    print(f"\n[+] Done! {next_node_id - 1} board(s) configured.")
    print("    Use 'h' command in serial to see available commands.")


def manual_mode():
    """Manual mode without pyserial."""
    print("\nManual mode: flash each board individually.")
    print()
    print("Steps:")
    print("  1. Connect ONE board at a time")
    print("  2. Run: pio run -e node_universal -t upload")
    print("  3. Open serial monitor (115200 baud)")
    print("  4. Follow the 'First Boot Setup' prompts")
    print("  5. Repeat for each board with different node IDs")
    print()
    print("Or use direct commands:")
    print("  pio run -e node_universal -t upload --upload-port /dev/ttyACM0")


def main():
    parser = argparse.ArgumentParser(description='PRCY Mesh Network Universal Flash Tool')
    parser.add_argument('--port', help='Serial port (e.g., /dev/ttyACM0)')
    parser.add_argument('--node-id', type=int, help='Node ID (1-254)')
    parser.add_argument('--secret', help='Network secret (64 hex chars)')
    parser.add_argument('--list', action='store_true', help='List available ports')
    parser.add_argument('--flash-only', action='store_true', help='Flash without configuring')
    args = parser.parse_args()

    if args.list:
        ports = list_stm32_ports()
        if not ports:
            print("No STM32 boards found.")
        for p in ports:
            print(f"  {p['device']} - {p['description']} (S/N: {p['serial']})")
        return

    if args.port:
        # Direct mode
        secret = args.secret or "0102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F20"
        node_id = args.node_id or 1

        if not flash_firmware(args.port):
            sys.exit(1)

        if args.flash_only:
            print("[+] Flash-only mode. Configure via serial 'c' command.")
            return

        time.sleep(3)
        ser = wait_for_prompt(args.port)
        if ser:
            configure_node(ser, node_id, secret)
            ser.close()
        else:
            print("[!] Configure manually via serial monitor.")
    else:
        interactive_mode()


if __name__ == '__main__':
    main()
