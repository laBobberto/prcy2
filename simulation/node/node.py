
import socket
import json
import time
import os
import threading
import base64
import sys
from .crypto import Kuznyechik

from .crypto import Kuznyechik, X25519Auth, Ed25519Auth, serialization

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
        self.pairwise_packet_counts = {}
        self.internal_clock = 0
        self.is_time_master = is_time_master
        self.time_sync_counter = 0
        self.self_seq_num = 0
        self.rreq_id = 0
        self.last_cleanup_time = 0
        self.message_callbacks = []

        # DH State
        self.dh_ephemeral_priv = None
        self.dh_ephemeral_pub_bytes = None
        self.dh_in_progress = set()
        
        # Identity keys (Ed25519)
        # For simulation, derive from node_id
        dummy_seed = bytes([self._node_id_to_int(self.node_id)] * 32)
        from cryptography.hazmat.primitives.asymmetric import ed25519
        self.identity_priv = ed25519.Ed25519PrivateKey.from_private_bytes(dummy_seed)
        self.identity_pub = self.identity_priv.public_key()
        self.node_identity_pubs = {}

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('0.0.0.0', self.udp_port))

        log(f"Node {self.node_id} started on port {self.udp_port}")
        if is_time_master:
            log(f"[{self.node_id}] This node is TIME MASTER")

    def _node_id_to_int(self, node_id):
        if isinstance(node_id, str):
            if node_id.startswith("NODE"):
                return int(node_id[4:])
            elif node_id == "GATEWAY":
                return 1
            else:
                return ord(node_id[0])
        return node_id

    def set_pairwise_key(self, peer_id, key):
        self.pairwise_keys[peer_id] = Kuznyechik(key)
        self.pairwise_packet_counts[peer_id] = 0
        log(f"[{self.node_id}] Pairwise key set for {peer_id}")

    def init_dh(self, peer_id):
        if peer_id == self.node_id or peer_id in self.dh_in_progress:
            return

        log(f"[{self.node_id}] [CRYPTO] Initiating Authenticated DH with {peer_id}")
        self.dh_ephemeral_priv, pub = X25519Auth.generate_key_pair()
        self.dh_ephemeral_pub_bytes = pub.public_bytes(
            encoding=serialization.Encoding.Raw,
            format=serialization.PublicFormat.Raw
        )
        self.dh_in_progress.add(peer_id)

        # Signature of ephemeral pub key
        signature = Ed25519Auth.sign(self.identity_priv, self.dh_ephemeral_pub_bytes)

        pkt = {
            "src": self.node_id,
            "dst": peer_id,
            "type": "DH_REQ",
            "ttl": 20,
            "timestamp": self.internal_clock,
            "payload": base64.b64encode(self.dh_ephemeral_pub_bytes + signature).decode()
        }
        self.send_packet(pkt)

    def process_dh_req(self, packet):
        peer_id = packet['src']
        log(f"[{self.node_id}] [CRYPTO] Received DH_REQ from {peer_id}")

        payload = base64.b64decode(packet['payload'])
        peer_pub_bytes = payload[:32]
        peer_sig = payload[32:96]

        # In a real system, we'd verify the signature here if we know the peer's identity pubkey
        # For simulation, we'll assume we know it (derived from peer_id)
        peer_int_id = self._node_id_to_int(peer_id)
        peer_identity_seed = bytes([peer_int_id] * 32)
        from cryptography.hazmat.primitives.asymmetric import ed25519
        peer_identity_pub = ed25519.Ed25519PrivateKey.from_private_bytes(peer_identity_seed).public_key()
        
        try:
            peer_identity_pub.verify(peer_sig, peer_pub_bytes)
            log(f"[{self.node_id}] [SECURITY] DH_REQ signature verified")
        except:
            log(f"[{self.node_id}] [SECURITY] DH_REQ signature verification FAILED!")
            return

        my_priv, my_pub = X25519Auth.generate_key_pair()
        my_pub_bytes = my_pub.public_bytes(
            encoding=serialization.Encoding.Raw,
            format=serialization.PublicFormat.Raw
        )
        shared_secret = X25519Auth.get_shared_secret(my_priv, peer_pub_bytes)
        
        self.set_pairwise_key(peer_id, shared_secret)

        # Respond with DH_REP
        signature = Ed25519Auth.sign(self.identity_priv, my_pub_bytes)
        
        rep = {
            "src": self.node_id,
            "dst": peer_id,
            "type": "DH_REP",
            "ttl": 20,
            "timestamp": self.internal_clock,
            "payload": base64.b64encode(my_pub_bytes + signature).decode()
        }
        self.send_packet(rep)

    def process_dh_rep(self, packet):
        peer_id = packet['src']
        if peer_id not in self.dh_in_progress:
            log(f"[{self.node_id}] [CRYPTO] Unsolicited DH_REP from {peer_id}, ignoring")
            return

        log(f"[{self.node_id}] [CRYPTO] Received DH_REP from {peer_id}")
        
        payload = base64.b64decode(packet['payload'])
        peer_pub_bytes = payload[:32]
        peer_sig = payload[32:96]

        peer_int_id = self._node_id_to_int(peer_id)
        peer_identity_seed = bytes([peer_int_id] * 32)
        from cryptography.hazmat.primitives.asymmetric import ed25519
        peer_identity_pub = ed25519.Ed25519PrivateKey.from_private_bytes(peer_identity_seed).public_key()

        try:
            peer_identity_pub.verify(peer_sig, peer_pub_bytes)
            log(f"[{self.node_id}] [SECURITY] DH_REP signature verified")
        except:
            log(f"[{self.node_id}] [SECURITY] DH_REP signature verification FAILED!")
            return

        shared_secret = X25519Auth.get_shared_secret(self.dh_ephemeral_priv, peer_pub_bytes)
        self.set_pairwise_key(peer_id, shared_secret)
        self.dh_in_progress.remove(peer_id)
        self.dh_ephemeral_priv = None
        self.dh_ephemeral_pub_bytes = None

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
        src = packet['src']
        if isinstance(src, str):
            if src.startswith("NODE"):
                src_val = int(src[4:])
            elif src == "GATEWAY":
                src_val = 1
            else:
                src_val = ord(src[0])
        else:
            src_val = src
        mac_data.append(src_val)

        dst = packet['dst']
        if isinstance(dst, str):
            if dst.startswith("NODE"):
                dst_val = int(dst[4:])
            elif dst == "GATEWAY":
                dst_val = 1
            else:
                dst_val = ord(dst[0])
        else:
            dst_val = dst
        mac_data.append(dst_val)

        mac_data.append(0)  # type DATA (hardcoded for now as in C)
        mac_data.extend(int(packet['timestamp']).to_bytes(4, 'little'))
        payload_bytes = base64.b64decode(packet['payload'])
        mac_data.append(len(payload_bytes))
        mac_data.extend(payload_bytes)

        mac = key_crypto.mac(bytes(mac_data))
        # Возвращаем 32 бита (4 байта)
        return int.from_bytes(mac[0:4], 'big')

    def compute_link_mic(self, packet):
        mac_data = bytearray()
        src = packet['src']
        if isinstance(src, str):
            if src.startswith("NODE"):
                src_val = int(src[4:])
            elif src == "GATEWAY":
                src_val = 1
            else:
                src_val = ord(src[0])
        else:
            src_val = src
        mac_data.append(src_val)

        dst = packet['dst']
        if isinstance(dst, str):
            if dst.startswith("NODE"):
                dst_val = int(dst[4:])
            elif dst == "GATEWAY":
                dst_val = 1
            else:
                dst_val = ord(dst[0])
        else:
            dst_val = dst
        mac_data.append(dst_val)

        # Тип пакета (Data=0, RREQ=1, RREP=2, etc)
        type_map = {"DATA": 0, "RREQ": 1, "RREP": 2, "TIME": 3, "KEY_ROTATION": 4}
        mac_data.append(type_map.get(packet['type'], 0))

        # TTL НЕ включаем — он меняется при пересылке
        mac_data.extend(int(packet['timestamp']).to_bytes(4, 'little'))
        payload_bytes = base64.b64decode(packet['payload'])
        mac_data.append(len(payload_bytes))
        mac_data.append(1 if packet.get('e2e_encrypted') else 0)
        mac_data.extend(payload_bytes)
        
        e2e_mic = packet.get('e2e_mic', 0)
        mac_data.extend(e2e_mic.to_bytes(4, 'big')) # 4 bytes

        mac = self.session_crypto.mac(bytes(mac_data))
        return int.from_bytes(mac[0:4], 'big')

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

        if packet['type'] == 'DH_REQ':
            self.process_dh_req(packet)
            return

        if packet['type'] == 'DH_REP':
            self.process_dh_rep(packet)
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
                            for cb in self.message_callbacks:
                                cb(packet['src'], msg, True)
                        except UnicodeDecodeError:
                            log(f"[{self.node_id}] ✓ E2E MESSAGE FROM {packet['src']}: [binary data]")
                    else:
                        decrypted = self.session_crypto.ctr_crypt(
                            int(packet['timestamp']), payload_bytes
                        )
                        try:
                            msg = decrypted.decode('utf-8').rstrip(chr(0))
                            log(f"[{self.node_id}] LINK MESSAGE FROM {packet['src']}: {msg}")
                            for cb in self.message_callbacks:
                                cb(packet['src'], msg, False)
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

    def send_data_message(self, dest_id, message_str):
        """
        Unified method to send a secure message. 
        Returns: (status_code, detail_message)
        Status codes: 0: Success, 1: RREQ initiated, 2: DH initiated, 3: Error
        """
        if dest_id == "BROADCAST":
            e2e_encrypted = False
        else:
            # 1. Check Route
            route = self.find_route(dest_id)
            if not route:
                self.send_rreq(dest_id)
                return 1, f"Route to {dest_id} not found, RREQ initiated"

            # 2. Check/Initiate DH if needed for E2E
            e2e_encrypted = dest_id in self.pairwise_keys
            if not e2e_encrypted:
                self.init_dh(dest_id)
                return 2, f"E2E key for {dest_id} not found, DH initiated"

        # 3. Prepare Packet
        msg_bytes = message_str.encode()
        payload_len = ((len(msg_bytes) + 15) // 16) * 16
        padded = msg_bytes.ljust(payload_len, b'\0')

        pkt = {
            "src": self.node_id,
            "dst": dest_id,
            "type": "DATA",
            "ttl": 20,
            "timestamp": self.internal_clock,
            "payload": "",
            "e2e_encrypted": e2e_encrypted,
            "e2e_mic": 0,
            "link_mic": 0
        }

        # 4. Encrypt and compute MICs
        if e2e_encrypted:
            encrypted = self.pairwise_keys[dest_id].ctr_crypt(self.internal_clock, padded)
            pkt['payload'] = base64.b64encode(encrypted).decode()
            pkt['e2e_mic'] = self.compute_e2e_mic(pkt, self.pairwise_keys[dest_id])
            
            # Update packet count for rotation
            self.pairwise_packet_counts[dest_id] += 1
            if self.pairwise_packet_counts[dest_id] >= 1000:
                log(f"[{self.node_id}] Packet limit reached for {dest_id}, triggering rotation")
                self.init_dh(dest_id)
        else:
            encrypted = self.session_crypto.ctr_crypt(self.internal_clock, padded)
            pkt['payload'] = base64.b64encode(encrypted).decode()

        pkt['link_mic'] = self.compute_link_mic(pkt)

        # 5. Send
        self.send_packet(pkt)
        return 0, "Message sent successfully"

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
                    if not e2e_encrypted and dst != "BROADCAST":
                        node.init_dh(dst)

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
                        
                        node.pairwise_packet_counts[dst] += 1
                        if node.pairwise_packet_counts[dst] >= 1000:
                            print(f"Packet limit reached for {dst}, rotating key...")
                            node.init_dh(dst)
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
