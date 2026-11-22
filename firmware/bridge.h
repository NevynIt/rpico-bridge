#ifndef RPICO_BRIDGE_H
#define RPICO_BRIDGE_H

#include "midi_router.h"
#include "route_policy.h"
#include "usb_bridge.h"

typedef struct {
    midi_router_t router;
    usb_bridge_endpoint_t endpoints[USB_BRIDGE_ENDPOINT_COUNT];
    uint8_t next_tx_cable[USB_BRIDGE_ENDPOINT_COUNT];
    uint32_t packets_received;
    uint32_t packets_sent;
} bridge_t;

void bridge_init(bridge_t *bridge);
/* Apply a validated policy snapshot; NULL restores the default All policy. */
bool bridge_set_policy(bridge_t *bridge, const route_policy_t *policy);
void bridge_step(bridge_t *bridge);

#endif
