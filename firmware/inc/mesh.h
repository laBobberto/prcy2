
#ifndef MESH_H
#define MESH_H

#include <stdint.h>
#include "kuznyechik.h"

#define MAX_PAYLOAD_SIZE 128
#define MESH_DEFAULT_TTL 20
#define MAX_NODES 16
#define MAX_ROUTES 16
#define ROUTE_LIFETIME 30000
#define MESH_FW_VERSION "1.0.0"

typedef enum {
    PACKET_TYPE_DATA = 0,
    PACKET_TYPE_RREQ = 1,
    PACKET_TYPE_RREP = 2,
    PACKET_TYPE_TIME = 3,
    PACKET_TYPE_KEY_ROTATION = 4,
    PACKET_TYPE_RERR = 5,
    PACKET_TYPE_DH_REQ = 6,
    PACKET_TYPE_DH_REP = 7,
    PACKET_TYPE_HEARTBEAT = 8
} packet_type_t;

typedef enum {
    ENCRYPT_NONE = 0,
    ENCRYPT_LINK_ONLY = 1,
    ENCRYPT_E2E = 2
} encrypt_mode_t;

#pragma pack(push, 1)
typedef struct {
    uint8_t  dest_id;
    uint8_t  next_hop;
    uint8_t  hop_count;
    uint32_t seq_num;
    uint32_t lifetime;
    uint8_t  valid;
    int8_t   last_rssi;   // RSSI of last packet from this next_hop
    int8_t   last_snr;    // SNR of last packet from this next_hop
} route_entry_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint32_t rreq_id;
    uint8_t  dest_id;
    uint32_t dest_seq_num;
    uint8_t  orig_id;
    uint32_t orig_seq_num;
    uint8_t  hop_count;
} rreq_payload_t;

typedef struct {
    uint8_t  dest_id;
    uint32_t dest_seq_num;
    uint8_t  orig_id;
    uint8_t  hop_count;
    uint32_t lifetime;
} rrep_payload_t;

typedef struct {
    uint8_t  unreachable_id;
    uint32_t unreachable_seq;
    uint8_t  orig_id;       // who detected the break
} rerr_payload_t;

typedef struct {
    uint32_t uptime;
    uint16_t battery_mv;
    int8_t   rssi;
    int8_t   snr;
    uint8_t  route_count;
    uint8_t  fw_version;    // 100 = v1.0.0
} heartbeat_payload_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint8_t  src_id;
    uint8_t  dst_id;
    uint8_t  type;
    uint8_t  ttl;
    uint32_t timestamp;
    uint8_t  payload_len;
    uint8_t  e2e_encrypted;
    uint8_t  payload[MAX_PAYLOAD_SIZE];
    uint32_t e2e_mic;    // Увеличено до 32 бит
    uint32_t link_mic;   // Увеличено до 32 бит
} mesh_packet_t;
#pragma pack(pop)

void mesh_init(uint8_t node_id);
int mesh_process_packet(mesh_packet_t *pkt);
void mesh_send_data(uint8_t dst_id, const uint8_t *data, uint8_t len);
void mesh_set_pairwise_key(uint8_t peer_id, const uint8_t *key);
void mesh_rotate_session_key(const uint8_t *new_key);
void mesh_broadcast_time(void);
void mesh_tick(void);
uint32_t mesh_get_time(void);

// Statistics
void mesh_print_stats(void);
void mesh_print_routes(void);
void mesh_notify_tx(void);  // called by lora_send_packet
void mesh_update_rssi(int16_t rssi, int8_t snr);  // called by lora_check_receive

// Identity and key exchange
void mesh_set_identity_key(const uint8_t *priv);
void mesh_set_node_identity(uint8_t id, const uint8_t *pub);
void mesh_init_dh(uint8_t peer_id);
void mesh_process_dh_req(mesh_packet_t *pkt);
void mesh_process_dh_rep(mesh_packet_t *pkt);
void mesh_get_random(uint8_t *buf, uint8_t len);

// AODV routing functions
route_entry_t* mesh_find_route(uint8_t dest_id);
void mesh_add_route(uint8_t dest_id, uint8_t next_hop, uint8_t hop_count, uint32_t seq_num);
void mesh_update_route_lifetime(uint8_t dest_id);
void mesh_invalidate_route(uint8_t dest_id);
void mesh_cleanup_routes(void);
void mesh_cleanup_old_data(void);
void mesh_send_rreq(uint8_t dest_id);

#endif // MESH_H
