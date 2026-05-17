#include "mesh.h"
#include "mesh_crypto.h"
#include "lora.h"
#include "x25519.h"
#include "edsign.h"
#include "debug.h"
#include <string.h>

static uint8_t self_node_id;
static uint8_t self_identity_priv[32];
static uint8_t self_identity_pub[32];
static uint8_t node_identity_pubs[MAX_NODES][32];
static uint8_t node_identity_set[MAX_NODES];

static kuznyechik_ctx_t session_crypto;
static kuznyechik_ctx_t pairwise_keys[MAX_NODES];
static uint8_t pairwise_keys_set[MAX_NODES];
static uint32_t pairwise_packet_counts[MAX_NODES];
static uint8_t master_key[32] = "MASTER_KEY_2026_STAY_SAFE_!!!!!";
static uint32_t last_timestamps[MAX_NODES];

// Packet statistics
static struct {
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t rx_routed;
    uint32_t rx_dropped_mic;
    uint32_t rx_dropped_ttl;
    uint32_t rx_dropped_old;
    uint32_t rx_dropped_crc;
    uint32_t rreq_sent;
    uint32_t rreq_received;
    uint32_t rrep_sent;
    uint32_t rrep_received;
    uint32_t rerr_sent;
    uint32_t rerr_received;
    uint32_t e2e_decrypted;
    int16_t  last_rssi;
    int8_t   last_snr;
    // Link quality tracking
    int32_t  rssi_sum;      // for averaging
    int32_t  snr_sum;
    uint32_t rssi_count;
    int16_t  rssi_min;
    int16_t  rssi_max;
} mesh_stats;
static uint32_t internal_clock = 0;

// Forward declarations
static void mesh_process_rreq(mesh_packet_t *pkt, uint8_t from_node);
static void mesh_process_rrep(mesh_packet_t *pkt, uint8_t from_node);
static void mesh_process_rerr(mesh_packet_t *pkt);
static uint8_t is_time_master = 0;
static uint32_t time_sync_counter = 0;
static int32_t clock_offset = 0;

static route_entry_t routing_table[MAX_ROUTES];
static uint32_t self_seq_num = 0;
static uint32_t rreq_id = 0;
static uint32_t seen_rreq[MAX_NODES];
static uint32_t last_cleanup_time = 0;

static uint8_t dh_ephemeral_priv[32];
static uint8_t dh_ephemeral_pub[32];
static uint8_t dh_in_progress[MAX_NODES];
static uint32_t dh_start_time[MAX_NODES];
static uint8_t dh_retry_count[MAX_NODES];
#define DH_TIMEOUT_MS 10000
#define DH_MAX_RETRIES 3

static kuznyechik_ctx_t prng_ctx;
static uint8_t prng_counter[16];
static uint8_t prng_initialized = 0;

#define TIMESTAMP_CLEANUP_INTERVAL 10000
#define TIMESTAMP_MAX_AGE 60000

void mesh_get_random(uint8_t *buf, uint8_t len) {
    if (!prng_initialized) {
        uint8_t seed[32];
        memset(seed, 0, 32);
        seed[0] = self_node_id;
        uint32_t t = internal_clock;
        memcpy(seed + 4, &t, 4);
        // Add some fixed entropy for now, in real HW this would be RNG or ADC noise
        for(int i=8; i<32; i++) seed[i] = 0x55 ^ i ^ self_node_id;
        
        kuznyechik_init(&prng_ctx, seed);
        memset(prng_counter, 0, 16);
        prng_initialized = 1;
    }

    for (uint8_t i = 0; i < len; ) {
        uint8_t block[16];
        kuznyechik_encrypt_block(&prng_ctx, prng_counter, block);
        
        // Increment 128-bit counter
        for (int j = 15; j >= 0; j--) {
            if (++prng_counter[j] != 0) break;
        }

        uint8_t chunk = (len - i < 16) ? (len - i) : 16;
        memcpy(buf + i, block, chunk);
        i += chunk;
    }
}


static void secure_memset(void *v, int c, size_t n) {
    volatile uint8_t *p = (volatile uint8_t *)v;
    while (n--) *p++ = c;
}

void mesh_init_dh(uint8_t peer_id) {
    if (peer_id >= MAX_NODES || peer_id == self_node_id) return;
    if (dh_in_progress[peer_id] && dh_retry_count[peer_id] >= DH_MAX_RETRIES) {
        debug_puts("[CRYPTO] DH with node ");
        debug_puti(peer_id);
        debug_puts(" failed after max retries\n");
        dh_in_progress[peer_id] = 0;
        dh_retry_count[peer_id] = 0;
        return;
    }

    debug_puts("[CRYPTO] Initiating Authenticated DH with node ");
    debug_puti(peer_id);
    if (dh_retry_count[peer_id] > 0) {
        debug_puts(" (retry ");
        debug_puti(dh_retry_count[peer_id]);
        debug_puts("/");
        debug_puti(DH_MAX_RETRIES);
        debug_puts(")");
    }
    debug_puts("\n");

    mesh_get_random(dh_ephemeral_priv, 32);
    x25519_base(dh_ephemeral_pub, dh_ephemeral_priv);
    dh_in_progress[peer_id] = 1;
    dh_start_time[peer_id] = internal_clock;
    if (dh_retry_count[peer_id] == 0) dh_retry_count[peer_id] = 0; // will be incremented below
    dh_retry_count[peer_id]++;

    mesh_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.src_id = self_node_id;
    pkt.dst_id = peer_id;
    pkt.type = PACKET_TYPE_DH_REQ;
    pkt.ttl = MESH_DEFAULT_TTL;
    pkt.timestamp = internal_clock;
    
    // Payload: [X25519_PUB(32)] [ED25519_SIG(64)]
    pkt.payload_len = 32 + 64;
    memcpy(pkt.payload, dh_ephemeral_pub, 32);
    edsign_sign(pkt.payload + 32, self_identity_pub, self_identity_priv, dh_ephemeral_pub, 32);

    pkt.link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)&pkt, &session_crypto);


    lora_send_packet(&pkt);
}

void mesh_process_dh_req(mesh_packet_t *pkt) {
    debug_puts("[CRYPTO] Received DH_REQ from node ");
    debug_puti(pkt->src_id);
    debug_puts("\n");

    uint8_t peer_pub[32];
    uint8_t peer_sig[64];
    memcpy(peer_pub, pkt->payload, 32);
    memcpy(peer_sig, pkt->payload + 32, 64);

    if (node_identity_set[pkt->src_id]) {
        if (!edsign_verify(peer_sig, node_identity_pubs[pkt->src_id], peer_pub, 32)) {
            debug_puts("[SECURITY] DH_REQ signature verification FAILED! MITM suspected.\n");
            return;
        }
        debug_puts("[SECURITY] DH_REQ signature verified\n");
    } else {
        debug_puts("[SECURITY] WARNING: Identity for node ");
        debug_puti(pkt->src_id);
        debug_puts(" not set. Proceeding without authentication.\n");
    }

    uint8_t my_priv[32];
    uint8_t my_pub[32];
    mesh_get_random(my_priv, 32);
    x25519_base(my_pub, my_priv);

    uint8_t shared_secret[32];
    x25519(shared_secret, my_priv, peer_pub);

    mesh_set_pairwise_key(pkt->src_id, shared_secret);
    secure_memset(shared_secret, 0, 32);
    secure_memset(my_priv, 0, 32);

    // Respond with DH_REP [PUB(32)] [SIG(64)]
    mesh_packet_t rep;
    memset(&rep, 0, sizeof(rep));
    rep.src_id = self_node_id;
    rep.dst_id = pkt->src_id;
    rep.type = PACKET_TYPE_DH_REP;
    rep.ttl = MESH_DEFAULT_TTL;
    rep.timestamp = internal_clock;
    rep.payload_len = 32 + 64;
    memcpy(rep.payload, my_pub, 32);
    edsign_sign(rep.payload + 32, self_identity_pub, self_identity_priv, my_pub, 32);

    rep.link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)&rep, &session_crypto);


    lora_send_packet(&rep);
}

void mesh_process_dh_rep(mesh_packet_t *pkt) {
    if (!dh_in_progress[pkt->src_id]) {
        debug_puts("[CRYPTO] Unsolicited DH_REP from node ");
        debug_puti(pkt->src_id);
        debug_puts(", ignoring\n");
        return;
    }

    debug_puts("[CRYPTO] Received DH_REP from node ");
    debug_puti(pkt->src_id);
    debug_puts("\n");

    uint8_t peer_pub[32];
    uint8_t peer_sig[64];
    memcpy(peer_pub, pkt->payload, 32);
    memcpy(peer_sig, pkt->payload + 32, 64);

    if (node_identity_set[pkt->src_id]) {
        if (!edsign_verify(peer_sig, node_identity_pubs[pkt->src_id], peer_pub, 32)) {
            debug_puts("[SECURITY] DH_REP signature verification FAILED! MITM suspected.\n");
            return;
        }
        debug_puts("[SECURITY] DH_REP signature verified\n");
    }

    uint8_t shared_secret[32];
    x25519(shared_secret, dh_ephemeral_priv, peer_pub);

    mesh_set_pairwise_key(pkt->src_id, shared_secret);
    secure_memset(shared_secret, 0, 32);
    secure_memset(dh_ephemeral_priv, 0, 32);
    dh_in_progress[pkt->src_id] = 0;
}

void mesh_set_identity_key(const uint8_t *priv) {
    memcpy(self_identity_priv, priv, 32);
    edsign_sec_to_pub(self_identity_pub, self_identity_priv);
    debug_puts("[CRYPTO] Identity key set\n");
}

void mesh_set_node_identity(uint8_t id, const uint8_t *pub) {
    if (id >= MAX_NODES) return;
    memcpy(node_identity_pubs[id], pub, 32);
    node_identity_set[id] = 1;
    debug_puts("[CRYPTO] Identity public key stored for node ");
    debug_puti(id);
    debug_puts("\n");
}

void mesh_init(uint8_t node_id) {
    self_node_id = node_id;
    kuznyechik_init(&session_crypto, master_key);
    memset(last_timestamps, 0, sizeof(last_timestamps));
    memset(pairwise_keys_set, 0, sizeof(pairwise_keys_set));
    memset(pairwise_packet_counts, 0, sizeof(pairwise_packet_counts));
    memset(dh_in_progress, 0, sizeof(dh_in_progress));
    memset(node_identity_set, 0, sizeof(node_identity_set));

    // Generate identity key from MCU UID (deterministic but unique per chip)
    uint8_t identity_priv[32];
    uint32_t uid0 = *(volatile uint32_t*)0x1FFFF7E8;
    uint32_t uid1 = *(volatile uint32_t*)0x1FFFF7EC;
    uint32_t uid2 = *(volatile uint32_t*)0x1FFFF7F0;
    // Mix UID with node_id and master_key for deterministic derivation
    for (int i = 0; i < 32; i++) {
        identity_priv[i] = master_key[i] ^ (uint8_t)(uid0 >> (i % 4 * 8))
                         ^ (uint8_t)(uid1 >> ((i + 1) % 4 * 8))
                         ^ (uint8_t)(uid2 >> ((i + 2) % 4 * 8))
                         ^ (uint8_t)(node_id + i);
    }
    mesh_set_identity_key(identity_priv);
    secure_memset(identity_priv, 0, sizeof(identity_priv));

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
    pairwise_packet_counts[peer_id] = 0;
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

    // Check for DH timeouts and retry
    for (int i = 0; i < MAX_NODES; i++) {
        if (dh_in_progress[i] && (internal_clock - dh_start_time[i] >= DH_TIMEOUT_MS)) {
            debug_puts("[CRYPTO] DH timeout for node ");
            debug_puti(i);
            debug_puts("\n");
            dh_in_progress[i] = 0;
            if (dh_retry_count[i] < DH_MAX_RETRIES) {
                mesh_init_dh(i);  // will increment retry_count
            } else {
                debug_puts("[CRYPTO] DH with node ");
                debug_puti(i);
                debug_puts(" failed permanently\n");
                dh_retry_count[i] = 0;
            }
        }
    }

    if (internal_clock - last_cleanup_time >= TIMESTAMP_CLEANUP_INTERVAL) {
        mesh_cleanup_old_data();
        last_cleanup_time = internal_clock;
    }
}

uint32_t mesh_get_time(void) {
    return internal_clock;
}

void mesh_notify_tx(void) {
    mesh_stats.tx_count++;
}

void mesh_update_rssi(int16_t rssi, int8_t snr) {
    mesh_stats.last_rssi = rssi;
    mesh_stats.last_snr = snr;
    mesh_stats.rssi_sum += rssi;
    mesh_stats.snr_sum += snr;
    mesh_stats.rssi_count++;
    if (rssi < mesh_stats.rssi_min) mesh_stats.rssi_min = rssi;
    if (rssi > mesh_stats.rssi_max) mesh_stats.rssi_max = rssi;

    // Link quality warning
    if (rssi < -100) {
        debug_puts("[LINK] WARNING: Very weak signal (RSSI=");
        debug_puti(rssi);
        debug_puts(" dBm)\n");
    } else if (rssi < -80) {
        debug_puts("[LINK] Weak signal (RSSI=");
        debug_puti(rssi);
        debug_puts(" dBm)\n");
    }
    if (snr < -5) {
        debug_puts("[LINK] WARNING: Low SNR=");
        debug_puti(snr);
        debug_puts(" dB\n");
    }
}

void mesh_print_stats(void) {
    debug_puts("\n=== PRCY MESH v");
    debug_puts(MESH_FW_VERSION);
    debug_puts(" ===\n");
    debug_puts("  TX packets:      "); debug_puti(mesh_stats.tx_count); debug_puts("\n");
    debug_puts("  RX packets:      "); debug_puti(mesh_stats.rx_count); debug_puts("\n");
    debug_puts("  RX routed:       "); debug_puti(mesh_stats.rx_routed); debug_puts("\n");
    debug_puts("  RX dropped MIC:  "); debug_puti(mesh_stats.rx_dropped_mic); debug_puts("\n");
    debug_puts("  RX dropped TTL:  "); debug_puti(mesh_stats.rx_dropped_ttl); debug_puts("\n");
    debug_puts("  RX dropped old:  "); debug_puti(mesh_stats.rx_dropped_old); debug_puts("\n");
    debug_puts("  RX CRC errors:   "); debug_puti(mesh_stats.rx_dropped_crc); debug_puts("\n");
    debug_puts("  RREQ sent/rcv:   "); debug_puti(mesh_stats.rreq_sent); debug_puts("/"); debug_puti(mesh_stats.rreq_received); debug_puts("\n");
    debug_puts("  RREP sent/rcv:   "); debug_puti(mesh_stats.rrep_sent); debug_puts("/"); debug_puti(mesh_stats.rrep_received); debug_puts("\n");
    debug_puts("  RERR sent/rcv:   "); debug_puti(mesh_stats.rerr_sent); debug_puts("/"); debug_puti(mesh_stats.rerr_received); debug_puts("\n");
    debug_puts("  E2E decrypted:   "); debug_puti(mesh_stats.e2e_decrypted); debug_puts("\n");
    int dh_active = 0;
    for (int i = 0; i < MAX_NODES; i++) if (dh_in_progress[i]) dh_active++;
    debug_puts("  DH in progress:  "); debug_puti(dh_active); debug_puts("\n");
    debug_puts("  Last RSSI:       "); debug_puti(mesh_stats.last_rssi); debug_puts(" dBm\n");
    debug_puts("  Last SNR:        "); debug_puti(mesh_stats.last_snr); debug_puts(" dB\n");
    if (mesh_stats.rssi_count > 0) {
        int16_t rssi_avg = mesh_stats.rssi_sum / mesh_stats.rssi_count;
        int8_t snr_avg = mesh_stats.snr_sum / mesh_stats.rssi_count;
        debug_puts("  RSSI avg/min/max:"); debug_puti(rssi_avg); debug_puts("/"); debug_puti(mesh_stats.rssi_min); debug_puts("/"); debug_puti(mesh_stats.rssi_max); debug_puts(" dBm\n");
        debug_puts("  SNR avg:         "); debug_puti(snr_avg); debug_puts(" dB\n");
    }
    debug_puts("  Uptime:          "); debug_puti(internal_clock); debug_puts(" ticks\n");
    debug_puts("  Routes active:   ");
    int routes = 0;
    for (int i = 0; i < MAX_ROUTES; i++) if (routing_table[i].valid) routes++;
    debug_puti(routes);
    debug_puts("/");
    debug_puti(MAX_ROUTES);
    debug_puts("\n");
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routing_table[i].valid) {
            debug_puts("    -> node ");
            debug_puti(routing_table[i].dest_id);
            debug_puts(" via ");
            debug_puti(routing_table[i].next_hop);
            debug_puts(" hops=");
            debug_puti(routing_table[i].hop_count);
            debug_puts(" RSSI=");
            debug_puti(routing_table[i].last_rssi);
            debug_puts(" SNR=");
            debug_puti(routing_table[i].last_snr);
            debug_puts("\n");
        }
    }
    debug_puts("=======================\n\n");
}

int mesh_process_packet(mesh_packet_t *pkt) {
    mesh_stats.rx_count++;
    if (pkt->ttl == 0) { mesh_stats.rx_dropped_ttl++; return 0; }
    if (pkt->src_id >= MAX_NODES) return 0;
    if (pkt->dst_id >= MAX_NODES && pkt->dst_id != 255) return 0;

    if (pkt->type == PACKET_TYPE_KEY_ROTATION) {
        debug_puts("[SECURITY] Key Rotation request received!\n");
        kuznyechik_ctx_t master_ctx;
        kuznyechik_init(&master_ctx, master_key);

        uint8_t new_key[32];
        memcpy(new_key, pkt->payload, 32);
        mesh_crypto_ctr(&master_ctx, pkt->timestamp, new_key, 32);

        kuznyechik_init(&session_crypto, new_key);
        debug_puts("[SECURITY] Session key updated and synced\n");
        return 1;
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
        
            lora_send_packet(pkt);
        }
        return 1;
    }

    if (pkt->type == PACKET_TYPE_DH_REQ) {
        uint32_t expected_link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)pkt, &session_crypto);
        if (pkt->link_mic != expected_link_mic) {
            debug_puts("[SECURITY] Link MIC verification FAILED for DH_REQ! Packet dropped.\n");
            return 0;
        }
        mesh_process_dh_req(pkt);
        return 1;
    }

    if (pkt->type == PACKET_TYPE_DH_REP) {
        uint32_t expected_link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)pkt, &session_crypto);
        if (pkt->link_mic != expected_link_mic) {
            debug_puts("[SECURITY] Link MIC verification FAILED for DH_REP! Packet dropped.\n");
            return 0;
        }
        mesh_process_dh_rep(pkt);
        return 1;
    }

    if (pkt->type == PACKET_TYPE_RREQ) {
        uint32_t expected_link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)pkt, &session_crypto);
        if (pkt->link_mic != expected_link_mic) {
            debug_puts("[SECURITY] Link MIC verification FAILED! Packet dropped.\n");
            mesh_stats.rx_dropped_mic++;
            return 0;
        }
        mesh_stats.rreq_received++;
        mesh_process_rreq(pkt, pkt->src_id);
        return 1;
    }

    if (pkt->type == PACKET_TYPE_RREP) {
        uint32_t expected_link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)pkt, &session_crypto);
        if (pkt->link_mic != expected_link_mic) {
            debug_puts("[SECURITY] Link MIC verification FAILED! Packet dropped.\n");
            mesh_stats.rx_dropped_mic++;
            return 0;
        }
        mesh_stats.rrep_received++;
        mesh_process_rrep(pkt, pkt->src_id);
        return 1;
    }

    if (pkt->type == PACKET_TYPE_RERR) {
        uint32_t expected_link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)pkt, &session_crypto);
        if (pkt->link_mic != expected_link_mic) {
            debug_puts("[SECURITY] Link MIC verification FAILED for RERR! Packet dropped.\n");
            mesh_stats.rx_dropped_mic++;
            return 0;
        }
        mesh_stats.rerr_received++;
        mesh_process_rerr(pkt);
        return 1;
    }

    if (pkt->src_id == self_node_id) return 0;

    if (pkt->timestamp <= last_timestamps[pkt->src_id]) {
        if (internal_clock - pkt->timestamp > 60000) {
            debug_puts("[SECURITY] Packet too old, dropping\n");
            mesh_stats.rx_dropped_old++;
            return 0;
        }
    }
    last_timestamps[pkt->src_id] = pkt->timestamp;

    uint32_t expected_link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)pkt, &session_crypto);
    if (pkt->link_mic != expected_link_mic) {
        debug_puts("[SECURITY] Link MIC verification FAILED! Packet dropped.\n");
        return 0;
    }

    if (pkt->dst_id == self_node_id || pkt->dst_id == 255) {
        if (pkt->e2e_encrypted && pairwise_keys_set[pkt->src_id]) {
            // Sender computes E2E MIC on plaintext then encrypts,
            // so receiver must decrypt FIRST then verify MIC.
            mesh_crypto_ctr(&pairwise_keys[pkt->src_id], pkt->timestamp, pkt->payload, pkt->payload_len);
            uint32_t expected_e2e_mic = mesh_crypto_compute_e2e_mic((mesh_crypto_packet_t*)pkt, &pairwise_keys[pkt->src_id]);
            if (pkt->e2e_mic != expected_e2e_mic) {
                debug_puts("[SECURITY] E2E MIC verification FAILED! Data might be tampered.\n");
                mesh_stats.rx_dropped_mic++;
                return 0;
            }
            pairwise_packet_counts[pkt->src_id]++;
            mesh_stats.e2e_decrypted++;
        } else if (!pkt->e2e_encrypted) {
            mesh_crypto_ctr(&session_crypto, pkt->timestamp, pkt->payload, pkt->payload_len);
        }

        if (pkt->type == PACKET_TYPE_DATA) {
            debug_puts("[MESH] Received data from node ");
            debug_puti(pkt->src_id);
            debug_puts(": ");
            for(int i=0; i<pkt->payload_len; i++) {
                if (pkt->payload[i] == 0) break;
                debug_putc(pkt->payload[i]);
            }
            debug_puts("\n");
        }
    }

    if (pkt->dst_id != self_node_id && pkt->ttl > 0) {
        route_entry_t *route = mesh_find_route(pkt->dst_id);
        if (route && route->valid) {
            pkt->ttl--;
            pkt->link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)pkt, &session_crypto);
            mesh_stats.rx_routed++;
            lora_send_packet(pkt);
            debug_puts("[MESH] Relaying packet to node ");
            debug_puti(pkt->dst_id);
            debug_puts("\n");
        } else if (pkt->dst_id == 255) {
            pkt->ttl--;
            pkt->link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)pkt, &session_crypto);
        
            lora_send_packet(pkt);
        } else {
            mesh_send_rreq(pkt->dst_id);
        }
    }
    return 1;
}

void mesh_send_data(uint8_t dst_id, const uint8_t *data, uint8_t len) {
    if (dst_id >= MAX_NODES && dst_id != 255) return;
    mesh_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.src_id = self_node_id;
    pkt.dst_id = dst_id;
    pkt.type = PACKET_TYPE_DATA;
    pkt.ttl = MESH_DEFAULT_TTL;
    pkt.timestamp = internal_clock;
    pkt.payload_len = (len + 15) & ~15;
    if (pkt.payload_len > MAX_PAYLOAD_SIZE) pkt.payload_len = MAX_PAYLOAD_SIZE;
    memcpy(pkt.payload, data, len);

    if (pairwise_keys_set[dst_id]) {
        pkt.e2e_encrypted = 1;
        pkt.e2e_mic = mesh_crypto_compute_e2e_mic((mesh_crypto_packet_t*)&pkt, &pairwise_keys[dst_id]);
        mesh_crypto_ctr(&pairwise_keys[dst_id], pkt.timestamp, pkt.payload, pkt.payload_len);
        
        pairwise_packet_counts[dst_id]++;
        if (pairwise_packet_counts[dst_id] >= 1000) {
            debug_puts("[CRYPTO] Packet limit reached, rotating key for node ");
            debug_puti(dst_id);
            debug_puts("\n");
            mesh_init_dh(dst_id);
        }
    } else {
        if (!dh_in_progress[dst_id] && dst_id != 255) {
            mesh_init_dh(dst_id);
        }
        pkt.e2e_encrypted = 0;
        pkt.e2e_mic = 0;
        mesh_crypto_ctr(&session_crypto, pkt.timestamp, pkt.payload, pkt.payload_len);
    }

    pkt.link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)&pkt, &session_crypto);

    route_entry_t *route = mesh_find_route(dst_id);
    if (route && route->valid) {
    
        lora_send_packet(&pkt);
    } else {
        mesh_send_rreq(dst_id);
    }
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
    mesh_crypto_ctr(&master_ctx, pkt.timestamp, encrypted_key, 32);
    memcpy(pkt.payload, encrypted_key, 32);


    lora_send_packet(&pkt);

    kuznyechik_init(&session_crypto, new_key);
    debug_puts("[SECURITY] Initiated session key rotation\n");
}

void mesh_broadcast_time(void) {
    mesh_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.src_id = self_node_id;
    pkt.dst_id = 255;
    pkt.type = PACKET_TYPE_TIME;
    pkt.ttl = 5;
    pkt.timestamp = internal_clock;

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
            existing->last_rssi = mesh_stats.last_rssi;
            existing->last_snr = mesh_stats.last_snr;
            debug_puts("[AODV] Updated route to ");
            debug_puti(dest_id);
            debug_puts(" via ");
            debug_puti(next_hop);
            debug_puts(" RSSI=");
            debug_puti(mesh_stats.last_rssi);
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
            routing_table[i].last_rssi = mesh_stats.last_rssi;
            routing_table[i].last_snr = mesh_stats.last_snr;
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
        route->last_rssi = mesh_stats.last_rssi;
        route->last_snr = mesh_stats.last_snr;
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
    for (int i = 0; i < MAX_NODES; i++) {
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

    pkt.link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)&pkt, &session_crypto);


    lora_send_packet(&pkt);

    mesh_stats.rreq_sent++;
    debug_puts("[AODV] Sent RREQ for dest=");
    debug_puti(dest_id);
    debug_puts(" rreq_id=");
    debug_puti(rreq_id);
    debug_puts("\n");
}

void mesh_send_rerr(uint8_t unreachable_id, uint32_t unreachable_seq) {
    mesh_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.src_id = self_node_id;
    pkt.dst_id = 255; // broadcast
    pkt.type = PACKET_TYPE_RERR;
    pkt.ttl = MESH_DEFAULT_TTL;
    pkt.timestamp = internal_clock;

    rerr_payload_t rerr;
    rerr.unreachable_id = unreachable_id;
    rerr.unreachable_seq = unreachable_seq;
    rerr.orig_id = self_node_id;

    memcpy(pkt.payload, &rerr, sizeof(rerr));
    pkt.payload_len = sizeof(rerr);
    pkt.link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)&pkt, &session_crypto);

    lora_send_packet(&pkt);

    mesh_stats.rerr_sent++;
    debug_puts("[AODV] Sent RERR for unreachable=");
    debug_puti(unreachable_id);
    debug_puts("\n");
}

static void mesh_process_rerr(mesh_packet_t *pkt) {
    rerr_payload_t rerr;
    memcpy(&rerr, pkt->payload, sizeof(rerr));

    debug_puts("[AODV] RERR: node ");
    debug_puti(rerr.unreachable_id);
    debug_puts(" unreachable (from ");
    debug_puti(rerr.orig_id);
    debug_puts(")\n");

    // Invalidate route to the unreachable node
    mesh_invalidate_route(rerr.unreachable_id);

    // Invalidate any routes that go through the unreachable node
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routing_table[i].valid && routing_table[i].next_hop == rerr.unreachable_id) {
            debug_puts("[AODV] Also invalidating route to ");
            debug_puti(routing_table[i].dest_id);
            debug_puts(" (went through ");
            debug_puti(rerr.unreachable_id);
            debug_puts(")\n");
            routing_table[i].valid = 0;
        }
    }

    // Forward RERR if TTL allows
    if (pkt->ttl > 1) {
        pkt->ttl--;
        pkt->link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)pkt, &session_crypto);
        lora_send_packet(pkt);
    }
}

static void mesh_process_rreq(mesh_packet_t *pkt, uint8_t from_node) {
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

        rrep.link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)&rrep, &session_crypto);

    
        lora_send_packet(&rrep);
        mesh_stats.rrep_sent++;
        debug_puts("[AODV] Sent RREP to orig=");
        debug_puti(rreq.orig_id);
        debug_puts("\n");
    } else {
        rreq.hop_count++;
        pkt->ttl--;
        if (pkt->ttl > 0) {
            memcpy(pkt->payload, &rreq, sizeof(rreq));
            pkt->link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)pkt, &session_crypto);

        
            lora_send_packet(pkt);

            debug_puts("[AODV] Forwarded RREQ\n");
        }
    }
}

static void mesh_process_rrep(mesh_packet_t *pkt, uint8_t from_node) {
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
                pkt->link_mic = mesh_crypto_compute_link_mic((mesh_crypto_packet_t*)pkt, &session_crypto);

            
                lora_send_packet(pkt);

                debug_puts("[AODV] Forwarded RREP\n");
            }
        }
    }
}
