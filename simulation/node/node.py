
import socket
import json
import time
import os
import threading
import base64
import sys
from .crypto import Kuznyechik

def log(msg):
    print(msg)
    sys.stdout.flush()

class MeshNode:
    def __init__(self, node_id, udp_port, neighbors, is_time_master=False):
        self.node_id = node_id
        self.udp_port = udp_port
        self.neighbors = neighbors
        self.routing_table = {}
        self.seen_packets = set()
        self.seen_rreq = {}
        self.session_crypto = Kuznyechik(b"MASTER_KEY_2026_STAY_SAFE_!!!!!")
        self.pairwise_keys = {}
        self.internal_clock = 0
        self.is_time_master = is_time_master
        self.time_sync_counter = 0
        self.self_seq_num = 0
        self.rreq_id = 0
        self.last_cleanup_time = 0

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('0.0.0.0', self.udp_port))

        log(f"Node {self.node_id} started on port {self.udp_port}")
        if is_time_master:
            log(f"[{self.node_id}] This node is TIME MASTER")

    def set_pairwise_key(self, peer_id, key):
        self.pairwise_keys[peer_id] = Kuznyechik(key)
        log(f"[{self.node_id}] Pairwise key set for {peer_id}")

    def discover_neighbors(self, all_nodes):
        """Автоматическое обнаружение прямых соседей и создание маршрутов к ним"""
        # Создаем маршруты к прямым соседям (1 hop)
        for node_id in all_nodes:
            if node_id != self.node_id:
                # Проверяем, является ли узел прямым соседом
                node_idx = all_nodes.index(node_id)
                neighbor_port = 7000 + node_idx  # base_port + index

                if neighbor_port in self.neighbors:
                    # Это прямой сосед - создаем маршрут
                    self.add_route(node_id, neighbor_port, 1, 0)
                    log(f"[{self.node_id}] [NEIGHBOR] Direct route to {node_id} (1 hop)")


    def rotate_session_key(self, new_key):
        """Rotate session key and broadcast to all nodes"""
        pkt = {
            "src": self.node_id,
            "dst": "BROADCAST",
            "type": "KEY_ROTATION",
            "ttl": 5,
            "timestamp": self.internal_clock,
            "payload": base64.b64encode(new_key).decode()
        }

        # Encrypt new key with master key
        master_crypto = Kuznyechik(b"MASTER_KEY_2026_STAY_SAFE_!!!!!")
        encrypted_key = master_crypto.ctr_crypt(self.internal_clock, new_key)
        pkt['payload'] = base64.b64encode(encrypted_key).decode()

        self.send_packet(pkt)

        # Update local session key
        self.session_crypto = Kuznyechik(new_key)
        log(f"[{self.node_id}] [SECURITY] Session key rotated")

    def tick(self):
        self.internal_clock += 1
        self.time_sync_counter += 1

        if self.is_time_master and self.time_sync_counter >= 1000:
            self.broadcast_time()
            self.time_sync_counter = 0

        if self.internal_clock % 5000 == 0:
            self.cleanup_routes()

        if self.internal_clock - self.last_cleanup_time >= 10000:
            self.cleanup_old_data()
            self.last_cleanup_time = self.internal_clock

    def find_route(self, dest_id):
        if dest_id in self.routing_table:
            route = self.routing_table[dest_id]
            if route['lifetime'] > self.internal_clock:
                return route
            else:
                del self.routing_table[dest_id]
        return None

    def add_route(self, dest_id, next_hop, hop_count, seq_num):
        if dest_id in self.routing_table:
            existing = self.routing_table[dest_id]
            if seq_num > existing['seq_num'] or \
               (seq_num == existing['seq_num'] and hop_count < existing['hop_count']):
                self.routing_table[dest_id] = {
                    'next_hop': next_hop,
                    'hop_count': hop_count,
                    'seq_num': seq_num,
                    'lifetime': self.internal_clock + 30000
                }
                log(f"[{self.node_id}] [AODV] Updated route to {dest_id} via {next_hop}")
        else:
            self.routing_table[dest_id] = {
                'next_hop': next_hop,
                'hop_count': hop_count,
                'seq_num': seq_num,
                'lifetime': self.internal_clock + 30000
            }
            log(f"[{self.node_id}] [AODV] Added route to {dest_id} via {next_hop}")

    def update_route_lifetime(self, dest_id):
        if dest_id in self.routing_table:
            self.routing_table[dest_id]['lifetime'] = self.internal_clock + 30000

    def invalidate_route(self, dest_id):
        if dest_id in self.routing_table:
            del self.routing_table[dest_id]
            log(f"[{self.node_id}] [AODV] Invalidated route to {dest_id}")

    def cleanup_routes(self):
        expired = [dest for dest, route in self.routing_table.items()
                   if route['lifetime'] <= self.internal_clock]
        for dest in expired:
            log(f"[{self.node_id}] [AODV] Route to {dest} expired")
            del self.routing_table[dest]

    def cleanup_old_data(self):
        old_packets = []
        for p in self.seen_packets:
            try:
                # packet_id format: "NODE_ID_timestamp"
                parts = p.split('_')
                if len(parts) >= 2:
                    timestamp = int(parts[-1])  # Последняя часть - timestamp
                    if self.internal_clock - timestamp > 60000:
                        old_packets.append(p)
            except (ValueError, IndexError):
                # Если не можем распарсить, пропускаем
                pass

        for p in old_packets:
            self.seen_packets.remove(p)

        old_rreq = [k for k, v in self.seen_rreq.items()
                    if self.internal_clock - v > 60000]
        for k in old_rreq:
            del self.seen_rreq[k]

        if old_packets or old_rreq:
            log(f"[{self.node_id}] [MEMORY] Cleaned up old data")

    def send_rreq(self, dest_id):
        self.rreq_id += 1
        self.self_seq_num += 1

        pkt = {
            "src": self.node_id,
            "dst": "BROADCAST",
            "type": "RREQ",
            "ttl": 20,
            "timestamp": self.internal_clock,
            "payload": {
                "rreq_id": self.rreq_id,
                "dest_id": dest_id,
                "dest_seq_num": 0,
                "orig_id": self.node_id,
                "orig_seq_num": self.self_seq_num,
                "hop_count": 0
            }
        }
        self.send_packet(pkt)
        log(f"[{self.node_id}] [AODV] Sent RREQ for dest={dest_id} rreq_id={self.rreq_id}")

    def process_rreq(self, packet, from_node):
        rreq = packet['payload']

        if rreq['orig_id'] == self.node_id:
            return

        rreq_key = f"{rreq['orig_id']}_{rreq['rreq_id']}"
        if rreq_key in self.seen_rreq:
            log(f"[{self.node_id}] [AODV] Duplicate RREQ ignored")
            return
        self.seen_rreq[rreq_key] = self.internal_clock

        self.add_route(rreq['orig_id'], from_node, rreq['hop_count'] + 1, rreq['orig_seq_num'])

        if rreq['dest_id'] == self.node_id:
            self.self_seq_num += 1

            rrep = {
                "src": self.node_id,
                "dst": rreq['orig_id'],
                "type": "RREP",
                "ttl": 20,
                "timestamp": self.internal_clock,
                "payload": {
                    "dest_id": self.node_id,
                    "dest_seq_num": self.self_seq_num,
                    "orig_id": rreq['orig_id'],
                    "hop_count": 0,
                    "lifetime": 30000
                }
            }
            self.send_packet(rrep)
            log(f"[{self.node_id}] [AODV] Sent RREP to orig={rreq['orig_id']}")
        else:
            rreq['hop_count'] += 1
            packet['ttl'] -= 1
            if packet['ttl'] > 0:
                packet['payload'] = rreq
                self.send_packet(packet)
                log(f"[{self.node_id}] [AODV] Forwarded RREQ")

    def process_rrep(self, packet, from_node):
        rrep = packet['payload']

        self.add_route(rrep['dest_id'], from_node, rrep['hop_count'] + 1, rrep['dest_seq_num'])

        if rrep['orig_id'] == self.node_id:
            log(f"[{self.node_id}] [AODV] Route established to dest={rrep['dest_id']}")
        else:
            route = self.find_route(rrep['orig_id'])
            if route:
                rrep['hop_count'] += 1
                packet['ttl'] -= 1
                if packet['ttl'] > 0:
                    packet['payload'] = rrep
                    self.send_packet(packet)
                    log(f"[{self.node_id}] [AODV] Forwarded RREP")

    def compute_e2e_mic(self, packet, key_crypto):
        mac_data = bytearray()
        mac_data.append(ord(packet['src'][0]) if isinstance(packet['src'], str) else packet['src'])
        mac_data.append(ord(packet['dst'][0]) if isinstance(packet['dst'], str) else packet['dst'])
        mac_data.append(0)  # type DATA
        mac_data.extend(int(packet['timestamp']).to_bytes(4, 'little'))
        payload_bytes = base64.b64decode(packet['payload'])
        mac_data.append(len(payload_bytes))
        mac_data.extend(payload_bytes)

        mac = key_crypto.mac(bytes(mac_data))
        return (mac[0] << 8) | mac[1]

    def compute_link_mic(self, packet):
        mac_data = bytearray()
        mac_data.append(ord(packet['src'][0]) if isinstance(packet['src'], str) else packet['src'])
        mac_data.append(ord(packet['dst'][0]) if isinstance(packet['dst'], str) else packet['dst'])
        mac_data.append(0)  # type DATA
        # TTL НЕ включаем — он меняется при пересылке
        mac_data.extend(int(packet['timestamp']).to_bytes(4, 'little'))
        payload_bytes = base64.b64decode(packet['payload'])
        mac_data.append(len(payload_bytes))
        mac_data.append(1 if packet.get('e2e_encrypted') else 0)
        mac_data.extend(payload_bytes)
        e2e_mic = packet.get('e2e_mic', 0)
        mac_data.extend(e2e_mic.to_bytes(2, 'big'))

        mac = self.session_crypto.mac(bytes(mac_data))
        return (mac[0] << 8) | mac[1]

    def broadcast_time(self):
        pkt = {
            "src": self.node_id,
            "dst": "BROADCAST",
            "type": "TIME",
            "ttl": 5,
            "timestamp": self.internal_clock,
            "payload": ""
        }
        self.send_packet(pkt)
        log(f"[{self.node_id}] [TIME] Broadcast time={self.internal_clock}")
        pkt = {
            "src": self.node_id,
            "dst": "BROADCAST",
            "type": "TIME",
            "ttl": 5,
            "timestamp": self.internal_clock,
            "payload": ""
        }
        self.send_packet(pkt)
        log(f"[{self.node_id}] [TIME] Broadcast time={self.internal_clock}")

    def send_packet(self, packet, target_port=None):
        data = json.dumps(packet).encode()
        if target_port:
            self.sock.sendto(data, ('localhost', target_port))
        else:
            # Simulate LoRa broadcast to neighbors
            for neighbor in self.neighbors:
                self.sock.sendto(data, ('localhost', neighbor))

    def handle_packet(self, data, addr):
        try:
            packet = json.loads(data.decode())
        except:
            return

        # Игнорируем свои же пакеты
        if packet['src'] == self.node_id:
            return

        if packet['ttl'] <= 0:
            log(f"[{self.node_id}] Packet dropped: TTL expired")
            return

        packet_id = f"{packet['src']}_{packet['timestamp']}"
        if packet_id in self.seen_packets:
            return
        self.seen_packets.add(packet_id)

        log(f"[{self.node_id}] Received {packet['type']} from {packet['src']} to {packet.get('dst', 'BROADCAST')}")

        if packet['type'] == 'KEY_ROTATION':
            if packet['src'] != self.node_id:
                log(f"[{self.node_id}] [SECURITY] Key Rotation request received!")
                master_crypto = Kuznyechik(b"MASTER_KEY_2026_STAY_SAFE_!!!!!")
                encrypted_key = base64.b64decode(packet['payload'])
                new_key = master_crypto.ctr_crypt(int(packet['timestamp']), encrypted_key)
                self.session_crypto = Kuznyechik(new_key)
                log(f"[{self.node_id}] [SECURITY] Session key updated and synced")

            if packet.get('dst') == "BROADCAST" and packet['ttl'] > 0:
                packet['ttl'] -= 1
                self.send_packet(packet)
            return

        if packet['type'] == 'TIME':
            if packet['src'] != self.node_id and not self.is_time_master:
                time_diff = int(packet['timestamp']) - self.internal_clock

                if abs(time_diff) > 100:
                    self.internal_clock = int(packet['timestamp'])
                    log(f"[{self.node_id}] [TIME] Hard sync: clock adjusted by {abs(time_diff)}")
                elif time_diff != 0:
                    clock_offset = time_diff // 4
                    self.internal_clock += clock_offset
                    log(f"[{self.node_id}] [TIME] Soft sync: offset={clock_offset}")

            if packet.get('dst') == "BROADCAST" and packet['ttl'] > 0:
                packet['ttl'] -= 1
                self.send_packet(packet)
            return

        if packet['type'] == 'RREQ':
            self.process_rreq(packet, packet['src'])
            return

        if packet['type'] == 'RREP':
            self.process_rrep(packet, packet['src'])
            return

        if packet['dst'] == self.node_id:
            if packet['type'] == 'DATA':
                try:
                    expected_link_mic = self.compute_link_mic(packet)
                    actual_link_mic = packet.get('link_mic', 0)
                    if actual_link_mic != expected_link_mic:
                        log(f"[{self.node_id}] [SECURITY] Link MIC verification FAILED!")
                        return

                    payload_bytes = base64.b64decode(packet['payload'])
                    e2e_encrypted = packet.get('e2e_encrypted', False)

                    if e2e_encrypted and packet['src'] in self.pairwise_keys:
                        expected_e2e_mic = self.compute_e2e_mic(packet, self.pairwise_keys[packet['src']])
                        actual_e2e_mic = packet.get('e2e_mic', 0)
                        if actual_e2e_mic != expected_e2e_mic:
                            log(f"[{self.node_id}] [SECURITY] E2E MIC verification FAILED!")
                            return

                        decrypted = self.pairwise_keys[packet['src']].ctr_crypt(
                            int(packet['timestamp']), payload_bytes
                        )
                        try:
                            msg = decrypted.decode('utf-8').rstrip(chr(0))
                            log(f"[{self.node_id}] ✓ E2E MESSAGE FROM {packet['src']}: {msg}")
                        except UnicodeDecodeError:
                            log(f"[{self.node_id}] ✓ E2E MESSAGE FROM {packet['src']}: [binary data]")
                    else:
                        decrypted = self.session_crypto.ctr_crypt(
                            int(packet['timestamp']), payload_bytes
                        )
                        try:
                            msg = decrypted.decode('utf-8').rstrip(chr(0))
                            log(f"[{self.node_id}] LINK MESSAGE FROM {packet['src']}: {msg}")
                        except UnicodeDecodeError:
                            log(f"[{self.node_id}] LINK MESSAGE FROM {packet['src']}: [binary data]")
                except Exception as e:
                    log(f"[{self.node_id}] ERROR processing DATA packet: {e}")

            elif packet['type'] == 'RREQ':
                reply = {
                    "src": self.node_id,
                    "dst": packet['src'],
                    "type": "RREP",
                    "ttl": 20,
                    "timestamp": time.time(),
                    "payload": ""
                }
                self.send_packet(reply)
        else:
            # Пересылка пакета
            if packet['type'] == 'DATA':
                # Проверяем Link MIC перед пересылкой
                expected_link_mic = self.compute_link_mic(packet)
                if packet.get('link_mic', 0) != expected_link_mic:
                    log(f"[{self.node_id}] [SECURITY] Link MIC verification FAILED! Not forwarding.")
                    return

            # Уменьшаем TTL
            packet['ttl'] -= 1

            # Пересчитываем Link MIC для следующего хопа (если это DATA пакет)
            if packet['type'] == 'DATA':
                packet['link_mic'] = self.compute_link_mic(packet)

            log(f"[{self.node_id}] Forwarding packet to {packet['dst']}, TTL={packet['ttl']}")
            self.send_packet(packet)

    def run(self):
        self.sock.settimeout(0.001)
        while True:
            try:
                data, addr = self.sock.recvfrom(4096)
                self.handle_packet(data, addr)
            except socket.timeout:
                pass
            self.tick()

if __name__ == "__main__":
    node_id = os.getenv("NODE_ID", "A")
    port = int(os.getenv("PORT", 5000))
    neighbors = [int(p) for p in os.getenv("NEIGHBORS", "").split(",") if p]
    is_time_master = (node_id == "NODE1" or node_id == "GATEWAY")

    node = MeshNode(node_id, port, neighbors, is_time_master)

    if node_id == "GATEWAY":
        def gateway_input():
            while True:
                msg = input("Enter message to send (format: dst message): ")
                try:
                    dst, content = msg.split(" ", 1)

                    route = node.find_route(dst)
                    if not route:
                        print(f"No route to {dst}, initiating route discovery...")
                        node.send_rreq(dst)
                        continue

                    payload_len = ((len(content) + 15) // 16) * 16
                    padded = content.encode().ljust(payload_len, b'\0')

                    e2e_encrypted = dst in node.pairwise_keys

                    pkt = {
                        "src": "GATEWAY",
                        "dst": dst,
                        "type": "DATA",
                        "ttl": 20,
                        "timestamp": node.internal_clock,
                        "payload": "",
                        "e2e_encrypted": e2e_encrypted,
                        "e2e_mic": 0,
                        "link_mic": 0
                    }

                    if e2e_encrypted:
                        pkt['payload'] = base64.b64encode(padded).decode()
                        pkt['e2e_mic'] = node.compute_e2e_mic(pkt, node.pairwise_keys[dst])
                        encrypted = node.pairwise_keys[dst].ctr_crypt(node.internal_clock, padded)
                        pkt['payload'] = base64.b64encode(encrypted).decode()
                    else:
                        encrypted = node.session_crypto.ctr_crypt(node.internal_clock, padded)
                        pkt['payload'] = base64.b64encode(encrypted).decode()

                    pkt['link_mic'] = node.compute_link_mic(pkt)

                    node.send_packet(pkt)
                    print(f"Sent: E2E_MIC={pkt['e2e_mic']}, LINK_MIC={pkt['link_mic']}")
                except Exception as e:
                    print(f"Error: {e}")
                    import traceback
                    traceback.print_exc()

        threading.Thread(target=gateway_input, daemon=True).start()

    node.run()
