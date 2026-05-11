
#ifndef MESH_H
#define MESH_H

#include <stdint.h>
#include "kuznyechik.h"

#define MAX_PAYLOAD_SIZE 64
#define MESH_DEFAULT_TTL 20
#define MAX_NODES 256
#define MAX_ROUTES 32
#define ROUTE_LIFETIME 30000

typedef enum {
    PACKET_TYPE_DATA = 0,
    PACKET_TYPE_RREQ = 1,
    PACKET_TYPE_RREP = 2,
    PACKET_TYPE_TIME = 3,
    PACKET_TYPE_KEY_ROTATION = 4,
    PACKET_TYPE_RERR = 5
} packet_type_t;

typedef enum {
    ENCRYPT_NONE = 0,
    ENCRYPT_LINK_ONLY = 1,
    ENCRYPT_E2E = 2
} encrypt_mode_t;

typedef struct {
    uint8_t  dest_id;
    uint8_t  next_hop;
    uint8_t  hop_count;
    uint32_t seq_num;
    uint32_t lifetime;
    uint8_t  valid;
} route_entry_t;

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
void mesh_process_packet(mesh_packet_t *pkt);
void mesh_send_data(uint8_t dst_id, const uint8_t *data, uint8_t len);
void mesh_set_pairwise_key(uint8_t peer_id, const uint8_t *key);
void mesh_rotate_session_key(const uint8_t *new_key);
void mesh_broadcast_time(void);
void mesh_tick(void);
uint32_t mesh_get_time(void);

// AODV routing functions
route_entry_t* mesh_find_route(uint8_t dest_id);
void mesh_add_route(uint8_t dest_id, uint8_t next_hop, uint8_t hop_count, uint32_t seq_num);
void mesh_update_route_lifetime(uint8_t dest_id);
void mesh_invalidate_route(uint8_t dest_id);
void mesh_cleanup_routes(void);
void mesh_cleanup_old_data(void);
void mesh_send_rreq(uint8_t dest_id);
void mesh_process_rreq(mesh_packet_t *pkt, uint8_t from_node);
void mesh_process_rrep(mesh_packet_t *pkt, uint8_t from_node);

#endif // MESH_H
