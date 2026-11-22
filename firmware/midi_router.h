#ifndef RPICO_BRIDGE_MIDI_ROUTER_H
#define RPICO_BRIDGE_MIDI_ROUTER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Endpoint 0 is the native USB device; host interface IDs occupy 1..8. */
#define MIDI_ROUTER_MAX_ENDPOINTS 9u
#define MIDI_ROUTER_MAX_CABLES 16u
#define MIDI_ROUTER_QUEUE_DEPTH 16u
#define MIDI_ROUTER_PACKET_SIZE 4u

typedef struct {
    uint8_t bytes[MIDI_ROUTER_PACKET_SIZE];
    uint8_t source_endpoint;
    uint8_t source_cable;
    bool sysex;
    bool sysex_start;
    bool sysex_end;
    bool realtime;
} midi_router_item_t;

typedef struct {
    midi_router_item_t items[MIDI_ROUTER_QUEUE_DEPTH];
    uint8_t count;
    bool sysex_locked;
    uint8_t sysex_endpoint;
    uint8_t sysex_cable;
    bool pending_eox;
    /* One bit per possible source endpoint/cable; only admitted streams continue. */
    uint32_t admitted_sysex[5];
} midi_router_output_t;

typedef struct {
    bool connected;
    uint8_t rx_cables;
    uint8_t tx_cables;
    uint32_t generation;
} midi_router_endpoint_t;

typedef struct {
    midi_router_endpoint_t endpoints[MIDI_ROUTER_MAX_ENDPOINTS];
    midi_router_output_t outputs[MIDI_ROUTER_MAX_ENDPOINTS][MIDI_ROUTER_MAX_CABLES];
    bool source_sysex[MIDI_ROUTER_MAX_ENDPOINTS][MIDI_ROUTER_MAX_CABLES];
    uint32_t overflow_drops;
    uint32_t invalid_packets;
} midi_router_t;

typedef enum {
    MIDI_ROUTER_PUSH_INVALID = 0,
    MIDI_ROUTER_PUSH_ROUTED,
    MIDI_ROUTER_PUSH_NO_DESTINATION,
    /* Eligible destination ports existed, but each corresponding queue was full. */
    MIDI_ROUTER_PUSH_DROPPED
} midi_router_push_result_t;

void midi_router_init(midi_router_t *router);

/*
 * Set an endpoint's independent input/output cable counts. A changed
 * generation, connection state, or cable count flushes packets to and from
 * that endpoint so stale address/interface traffic cannot cross reconnects.
 * Cable counts must be <= MIDI_ROUTER_MAX_CABLES.
 */
bool midi_router_set_endpoint(midi_router_t *router, uint8_t endpoint,
                              uint8_t rx_cables, uint8_t tx_cables,
                              uint32_t generation, bool connected);

/* Disconnect and purge one endpoint. Generation is retained for diagnostics. */
void midi_router_remove_endpoint(midi_router_t *router, uint8_t endpoint,
                                 uint32_t generation);

/*
 * Push one USB-MIDI event packet (cable/CIN in byte 0, MIDI bytes 1..3).
 * It fans out to every configured TX cable except its exact source cable;
 * destination cable numbers replace only the high nibble of byte 0.
 * CIN 0xF is treated as a raw single-byte packet; CIN 0x5/0xF status bytes
 * >= 0xF8 may pass an active SysEx lock as realtime traffic.
 *
 * Overflow drops newest per destination cable. If that interrupts an admitted
 * SysEx stream, queued chunks from that source are purged; when F0 was already
 * sent, peek yields a synthetic destination-tagged CIN 5 / F7 packet before
 * other non-realtime traffic. Disconnecting the source does the same.
 */
midi_router_push_result_t midi_router_push(midi_router_t *router,
                                           uint8_t source_endpoint,
                                           const uint8_t packet[4]);

/* Copy the next eligible packet without removing it; false means no packet. */
bool midi_router_peek(const midi_router_t *router, uint8_t destination_endpoint,
                      uint8_t destination_cable, uint8_t packet[4]);

/* Remove the next eligible packet after USB accepts the packet returned by peek. */
bool midi_router_consume(midi_router_t *router, uint8_t destination_endpoint,
                         uint8_t destination_cable);

#ifdef __cplusplus
}
#endif

#endif
