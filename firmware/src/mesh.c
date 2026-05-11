
#include "mesh.h"
#include "kuznyechik.h"
#include "debug.h"
#include <string.h>

static uint8_t self_node_id;
static kuznyechik_ctx_t session_crypto;
static kuznyechik_ctx_t pairwise_keys[MAX_NODES];
static uint8_t pairwise_keys_set[MAX_NODES];
static uint8_t master_key[32] = "MASTER_KEY_2026_STAY_SAFE_!!!!!";
static uint32_t last_timestamps[256];
static uint32_t internal_clock = 0;
static uint8_t is_time_master = 0;
static uint32_t time_sync_counter = 0;
static int32_t clock_offset = 0;

static route_entry_t routing_table[MAX_ROUTES];
static uint32_t self_seq_num = 0;
static uint32_t rreq_id = 0;
static uint32_t seen_rreq[256];
static uint32_t last_cleanup_time = 0;

#define TIMESTAMP_CLEANUP_INTERVAL 10000
#define TIMESTAMP_MAX_AGE 60000

void mesh_init(uint8_t node_id) {
    self_node_id = node_id;
    kuznyechik_init(&session_crypto, master_key);
    memset(last_timestamps, 0, sizeof(last_timestamps));
    memset(pairwise_keys_set, 0, sizeof(pairwise_keys_set));
    memset(routing_table, 0, sizeof(routing_table));
    memset(seen_rreq, 0, sizeof(seen_rreq));
    is_time_master = (node_id == 1);
    self_seq_num = 0;
    rreq_id = 0;
    debug_puts("Mesh System: Kuznyechik E2E + Link-layer encryption active\n");
    if (is_time_master) {
        debug_puts("[TIME] This node is TIME MASTER\n");
    }
    debug_puts("[AODV] Routing table initialized\n");
}

void mesh_set_pairwise_key(uint8_t peer_id, const uint8_t *key) {
    if (peer_id >= MAX_NODES) return;
    kuznyechik_init(&pairwise_keys[peer_id], key);
    pairwise_keys_set[peer_id] = 1;
    debug_puts("[CRYPTO] Pairwise key set for node ");
    debug_puti(peer_id);
    debug_puts("\n");
}

void mesh_tick(void) {
    internal_clock++;
    time_sync_counter++;

    if (is_time_master && time_sync_counter >= 1000) {
        mesh_broadcast_time();
        time_sync_counter = 0;
    }

    if (internal_clock % 5000 == 0) {
        mesh_cleanup_routes();
    }

    if (internal_clock - last_cleanup_time >= TIMESTAMP_CLEANUP_INTERVAL) {
        mesh_cleanup_old_data();
        last_cleanup_time = internal_clock;
    }
}

uint32_t mesh_get_time(void) {
    return internal_clock;
}

static uint16_t compute_e2e_mic(mesh_packet_t *pkt, kuznyechik_ctx_t *key) {
    uint8_t mac_data[128];
    int offset = 0;

    mac_data[offset++] = pkt->src_id;
    mac_data[offset++] = pkt->dst_id;
    mac_data[offset++] = pkt->type;
    memcpy(mac_data + offset, &pkt->timestamp, 4);
    offset += 4;
    mac_data[offset++] = pkt->payload_len;
    memcpy(mac_data + offset, pkt->payload, pkt->payload_len);
    offset += pkt->payload_len;

    uint8_t mac[16];
    kuznyechik_mac(key, mac_data, offset, mac);

    return (mac[0] << 8) | mac[1];
}

static uint16_t compute_link_mic(mesh_packet_t *pkt) {
    uint8_t mac_data[128];
    int offset = 0;

    mac_data[offset++] = pkt->src_id;
    mac_data[offset++] = pkt->dst_id;
    mac_data[offset++] = pkt->type;
    // TTL НЕ включаем — он меняется при пересылке
    memcpy(mac_data + offset, &pkt->timestamp, 4);
    offset += 4;
    mac_data[offset++] = pkt->payload_len;
    mac_data[offset++] = pkt->e2e_encrypted;
    memcpy(mac_data + offset, pkt->payload, pkt->payload_len);
    offset += pkt->payload_len;
    memcpy(mac_data + offset, &pkt->e2e_mic, 2);
    offset += 2;

    uint8_t mac[16];
    kuznyechik_mac(&session_crypto, mac_data, offset, mac);

    return (mac[0] << 8) | mac[1];
}

void mesh_crypt_ctr(kuznyechik_ctx_t *ctx, uint32_t nonce, uint8_t *data, uint8_t len) {
    uint8_t counter_block[16];
    uint8_t keystream[16];
    for (int i = 0; i < len; i += 16) {
        memset(counter_block, 0, 16);
        memcpy(counter_block, &nonce, 4);
        uint32_t block_idx = i / 16;
        memcpy(counter_block + 4, &block_idx, 4);
        kuznyechik_encrypt_block(ctx, counter_block, keystream);
        for (int j = 0; j < 16 && (i + j) < len; j++) {
            data[i + j] ^= keystream[j];
        }
    }
}

void mesh_process_packet(mesh_packet_t *pkt) {
    if (pkt->ttl == 0) return;

    if (pkt->type == PACKET_TYPE_KEY_ROTATION) {
        debug_puts("[SECURITY] Key Rotation request received!\n");
        kuznyechik_ctx_t master_ctx;
        kuznyechik_init(&master_ctx, master_key);

        uint8_t new_key[32];
        memcpy(new_key, pkt->payload, 32);
        mesh_crypt_ctr(&master_ctx, pkt->timestamp, new_key, 32);

        kuznyechik_init(&session_crypto, new_key);
        debug_puts("[SECURITY] Session key updated and synced\n");
        return;
    }

    if (pkt->type == PACKET_TYPE_TIME) {
        if (pkt->src_id != self_node_id && !is_time_master) {
            int32_t time_diff = (int32_t)pkt->timestamp - (int32_t)internal_clock;

            if (time_diff > 100 || time_diff < -100) {
                internal_clock = pkt->timestamp;
                debug_puts("[TIME] Hard sync: clock adjusted by ");
                debug_puti(time_diff > 0 ? time_diff : -time_diff);
                debug_puts("\n");
            } else if (time_diff != 0) {
                clock_offset = time_diff / 4;
                internal_clock += clock_offset;
                debug_puts("[TIME] Soft sync: offset=");
                debug_puti(clock_offset);
                debug_puts("\n");
            }
        }

        if (pkt->dst_id == 255 && pkt->ttl > 0) {
            pkt->ttl--;
            void lora_send_packet(mesh_packet_t *p);
            lora_send_packet(pkt);
        }
        return;
    }

    if (pkt->type == PACKET_TYPE_RREQ) {
        uint16_t expected_link_mic = compute_link_mic(pkt);
        if (pkt->link_mic != expected_link_mic) {
            debug_puts("[SECURITY] Link MIC verification FAILED! Packet dropped.\n");
            return;
        }
        mesh_process_rreq(pkt, pkt->src_id);
        return;
    }

    if (pkt->type == PACKET_TYPE_RREP) {
        uint16_t expected_link_mic = compute_link_mic(pkt);
        if (pkt->link_mic != expected_link_mic) {
            debug_puts("[SECURITY] Link MIC verification FAILED! Packet dropped.\n");
            return;
        }
        mesh_process_rrep(pkt, pkt->src_id);
        return;
    }

    uint16_t expected_link_mic = compute_link_mic(pkt);
    if (pkt->link_mic != expected_link_mic) {
        debug_puts("[SECURITY] Link MIC verification FAILED! Packet dropped.\n");
        return;
    }

    if (pkt->timestamp <= last_timestamps[pkt->src_id]) return;
    last_timestamps[pkt->src_id] = pkt->timestamp;

    if (pkt->dst_id == self_node_id) {
        if (pkt->type == PACKET_TYPE_DATA) {
            if (pkt->e2e_encrypted && pairwise_keys_set[pkt->src_id]) {
                uint16_t expected_e2e_mic = compute_e2e_mic(pkt, &pairwise_keys[pkt->src_id]);
                if (pkt->e2e_mic != expected_e2e_mic) {
                    debug_puts("[SECURITY] E2E MIC verification FAILED! Message dropped.\n");
                    return;
                }

                mesh_crypt_ctr(&pairwise_keys[pkt->src_id], pkt->timestamp, pkt->payload, pkt->payload_len);
                debug_puts("\n*** E2E MSG FROM NODE ");
            } else {
                mesh_crypt_ctr(&session_crypto, pkt->timestamp, pkt->payload, pkt->payload_len);
                debug_puts("\n*** LINK MSG FROM NODE ");
            }
            debug_puti(pkt->src_id);
            debug_puts(": [");
            for(int i=0; i<pkt->payload_len; i++) {
                if (pkt->payload[i] == 0) break;
                debug_putc(pkt->payload[i]);
            }
            debug_puts("] ***\n");
        }
    } else {
        pkt->ttl--;
        void lora_send_packet(mesh_packet_t *p);
        lora_send_packet(pkt);
    }
}

void mesh_send_data(uint8_t dst_id, const uint8_t *data, uint8_t len) {
    route_entry_t *route = mesh_find_route(dst_id);
    if (!route) {
        debug_puts("[AODV] No route to ");
        debug_puti(dst_id);
        debug_puts(", initiating route discovery\n");
        mesh_send_rreq(dst_id);
        return;
    }

    static uint32_t last_sent_ts = 0;
    if (internal_clock <= last_sent_ts) {
        internal_clock = last_sent_ts + 1;
    }
    last_sent_ts = internal_clock;

    mesh_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.src_id = self_node_id;
    pkt.dst_id = dst_id;
    pkt.type = PACKET_TYPE_DATA;
    pkt.ttl = MESH_DEFAULT_TTL;
    pkt.timestamp = internal_clock;
    pkt.payload_len = (len + 15) & ~15;

    memcpy(pkt.payload, data, len);

    if (pairwise_keys_set[dst_id]) {
        pkt.e2e_mic = compute_e2e_mic(&pkt, &pairwise_keys[dst_id]);
        mesh_crypt_ctr(&pairwise_keys[dst_id], pkt.timestamp, pkt.payload, pkt.payload_len);
        pkt.e2e_encrypted = 1;
        debug_puts("[MESH] Sent E2E encrypted packet to ");
    } else {
        pkt.e2e_mic = 0;
        mesh_crypt_ctr(&session_crypto, pkt.timestamp, pkt.payload, pkt.payload_len);
        pkt.e2e_encrypted = 0;
        debug_puts("[MESH] Sent link-encrypted packet to ");
    }

    pkt.link_mic = compute_link_mic(&pkt);

    debug_puti(dst_id);
    debug_puts(" T=");
    debug_puti(pkt.timestamp);
    debug_puts(" E2E_MIC=");
    debug_puti(pkt.e2e_mic);
    debug_puts(" LINK_MIC=");
    debug_puti(pkt.link_mic);
    debug_puts("\n");

    void lora_send_packet(mesh_packet_t *p);
    lora_send_packet(&pkt);
}

void mesh_rotate_session_key(const uint8_t *new_key) {
    mesh_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.src_id = self_node_id;
    pkt.dst_id = 255;
    pkt.type = PACKET_TYPE_KEY_ROTATION;
    pkt.ttl = 5;
    pkt.timestamp = internal_clock;
    pkt.payload_len = 32;

    kuznyechik_ctx_t master_ctx;
    kuznyechik_init(&master_ctx, master_key);

    uint8_t encrypted_key[32];
    memcpy(encrypted_key, new_key, 32);
    mesh_crypt_ctr(&master_ctx, pkt.timestamp, encrypted_key, 32);
    memcpy(pkt.payload, encrypted_key, 32);

    void lora_send_packet(mesh_packet_t *p);
    lora_send_packet(&pkt);

    kuznyechik_init(&session_crypto, new_key);
    debug_puts("[SECURITY] Master updated local session key\n");
}

void mesh_broadcast_time(void) {
    mesh_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.src_id = self_node_id;
    pkt.dst_id = 255;
    pkt.type = PACKET_TYPE_TIME;
    pkt.ttl = 5;
    pkt.timestamp = internal_clock;
    void lora_send_packet(mesh_packet_t *p);
    lora_send_packet(&pkt);
    debug_puts("[TIME] Broadcast time=");
    debug_puti(internal_clock);
    debug_puts("\n");
}

route_entry_t* mesh_find_route(uint8_t dest_id) {
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routing_table[i].valid && routing_table[i].dest_id == dest_id) {
            if (routing_table[i].lifetime > internal_clock) {
                return &routing_table[i];
            } else {
                routing_table[i].valid = 0;
            }
        }
    }
    return NULL;
}

void mesh_add_route(uint8_t dest_id, uint8_t next_hop, uint8_t hop_count, uint32_t seq_num) {
    route_entry_t *existing = mesh_find_route(dest_id);

    if (existing) {
        if (seq_num > existing->seq_num ||
            (seq_num == existing->seq_num && hop_count < existing->hop_count)) {
            existing->next_hop = next_hop;
            existing->hop_count = hop_count;
            existing->seq_num = seq_num;
            existing->lifetime = internal_clock + ROUTE_LIFETIME;
            debug_puts("[AODV] Updated route to ");
            debug_puti(dest_id);
            debug_puts(" via ");
            debug_puti(next_hop);
            debug_puts("\n");
        }
        return;
    }

    for (int i = 0; i < MAX_ROUTES; i++) {
        if (!routing_table[i].valid) {
            routing_table[i].dest_id = dest_id;
            routing_table[i].next_hop = next_hop;
            routing_table[i].hop_count = hop_count;
            routing_table[i].seq_num = seq_num;
            routing_table[i].lifetime = internal_clock + ROUTE_LIFETIME;
            routing_table[i].valid = 1;
            debug_puts("[AODV] Added route to ");
            debug_puti(dest_id);
            debug_puts(" via ");
            debug_puti(next_hop);
            debug_puts("\n");
            return;
        }
    }

    debug_puts("[AODV] WARNING: Routing table full!\n");
}

void mesh_update_route_lifetime(uint8_t dest_id) {
    route_entry_t *route = mesh_find_route(dest_id);
    if (route) {
        route->lifetime = internal_clock + ROUTE_LIFETIME;
    }
}

void mesh_invalidate_route(uint8_t dest_id) {
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routing_table[i].valid && routing_table[i].dest_id == dest_id) {
            routing_table[i].valid = 0;
            debug_puts("[AODV] Invalidated route to ");
            debug_puti(dest_id);
            debug_puts("\n");
            return;
        }
    }
}

void mesh_cleanup_routes(void) {
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routing_table[i].valid && routing_table[i].lifetime <= internal_clock) {
            debug_puts("[AODV] Route to ");
            debug_puti(routing_table[i].dest_id);
            debug_puts(" expired\n");
            routing_table[i].valid = 0;
        }
    }
}

void mesh_cleanup_old_data(void) {
    for (int i = 0; i < 256; i++) {
        if (last_timestamps[i] > 0 &&
            (internal_clock - last_timestamps[i]) > TIMESTAMP_MAX_AGE) {
            last_timestamps[i] = 0;
        }
        if (seen_rreq[i] > 0 &&
            (internal_clock - seen_rreq[i]) > TIMESTAMP_MAX_AGE) {
            seen_rreq[i] = 0;
        }
    }
    debug_puts("[MEMORY] Cleaned up old timestamps and RREQ records\n");
}

void mesh_send_rreq(uint8_t dest_id) {
    rreq_id++;
    self_seq_num++;

    mesh_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.src_id = self_node_id;
    pkt.dst_id = 255;
    pkt.type = PACKET_TYPE_RREQ;
    pkt.ttl = MESH_DEFAULT_TTL;
    pkt.timestamp = internal_clock;

    rreq_payload_t rreq;
    rreq.rreq_id = rreq_id;
    rreq.dest_id = dest_id;
    rreq.dest_seq_num = 0;
    rreq.orig_id = self_node_id;
    rreq.orig_seq_num = self_seq_num;
    rreq.hop_count = 0;

    memcpy(pkt.payload, &rreq, sizeof(rreq));
    pkt.payload_len = sizeof(rreq);

    pkt.link_mic = compute_link_mic(&pkt);

    void lora_send_packet(mesh_packet_t *p);
    lora_send_packet(&pkt);

    debug_puts("[AODV] Sent RREQ for dest=");
    debug_puti(dest_id);
    debug_puts(" rreq_id=");
    debug_puti(rreq_id);
    debug_puts("\n");
}

void mesh_process_rreq(mesh_packet_t *pkt, uint8_t from_node) {
    rreq_payload_t rreq;
    memcpy(&rreq, pkt->payload, sizeof(rreq));

    if (rreq.orig_id == self_node_id) return;

    if (seen_rreq[rreq.orig_id] >= rreq.rreq_id) {
        debug_puts("[AODV] Duplicate RREQ ignored\n");
        return;
    }
    seen_rreq[rreq.orig_id] = rreq.rreq_id;

    mesh_add_route(rreq.orig_id, from_node, rreq.hop_count + 1, rreq.orig_seq_num);

    if (rreq.dest_id == self_node_id) {
        self_seq_num++;

        mesh_packet_t rrep;
        memset(&rrep, 0, sizeof(rrep));
        rrep.src_id = self_node_id;
        rrep.dst_id = rreq.orig_id;
        rrep.type = PACKET_TYPE_RREP;
        rrep.ttl = MESH_DEFAULT_TTL;
        rrep.timestamp = internal_clock;

        rrep_payload_t rrep_data;
        rrep_data.dest_id = self_node_id;
        rrep_data.dest_seq_num = self_seq_num;
        rrep_data.orig_id = rreq.orig_id;
        rrep_data.hop_count = 0;
        rrep_data.lifetime = ROUTE_LIFETIME;

        memcpy(rrep.payload, &rrep_data, sizeof(rrep_data));
        rrep.payload_len = sizeof(rrep_data);

        rrep.link_mic = compute_link_mic(&rrep);

        void lora_send_packet(mesh_packet_t *p);
        lora_send_packet(&rrep);

        debug_puts("[AODV] Sent RREP to orig=");
        debug_puti(rreq.orig_id);
        debug_puts("\n");
    } else {
        rreq.hop_count++;
        pkt->ttl--;
        if (pkt->ttl > 0) {
            memcpy(pkt->payload, &rreq, sizeof(rreq));
            pkt->link_mic = compute_link_mic(pkt);

            void lora_send_packet(mesh_packet_t *p);
            lora_send_packet(pkt);

            debug_puts("[AODV] Forwarded RREQ\n");
        }
    }
}

void mesh_process_rrep(mesh_packet_t *pkt, uint8_t from_node) {
    rrep_payload_t rrep;
    memcpy(&rrep, pkt->payload, sizeof(rrep));

    mesh_add_route(rrep.dest_id, from_node, rrep.hop_count + 1, rrep.dest_seq_num);

    if (rrep.orig_id == self_node_id) {
        debug_puts("[AODV] Route established to dest=");
        debug_puti(rrep.dest_id);
        debug_puts("\n");
    } else {
        route_entry_t *route = mesh_find_route(rrep.orig_id);
        if (route) {
            rrep.hop_count++;
            pkt->ttl--;
            if (pkt->ttl > 0) {
                memcpy(pkt->payload, &rrep, sizeof(rrep));
                pkt->link_mic = compute_link_mic(pkt);

                void lora_send_packet(mesh_packet_t *p);
                lora_send_packet(pkt);

                debug_puts("[AODV] Forwarded RREP\n");
            }
        }
    }
}
