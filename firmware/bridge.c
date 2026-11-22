#include "bridge.h"

#include <string.h>

_Static_assert(USB_BRIDGE_ENDPOINT_COUNT == MIDI_ROUTER_MAX_ENDPOINTS,
               "USB and router endpoint counts must match");

void bridge_init(bridge_t *bridge)
{
    midi_router_init(&bridge->router);
    memset(bridge->endpoints, 0, sizeof(bridge->endpoints));
    memset(bridge->next_tx_cable, 0, sizeof(bridge->next_tx_cable));
    bridge->packets_received = 0;
    bridge->packets_sent = 0;
}

bool bridge_set_policy(bridge_t *bridge, const route_policy_t *policy)
{
    if (bridge == NULL) {
        return false;
    }
    if (policy == NULL) {
        route_policy_t all = {0};
        all.default_action = ROUTE_ALL;
        return midi_router_set_policy(&bridge->router, &all);
    }
    return midi_router_set_policy(&bridge->router, policy);
}

static void sync_endpoints(bridge_t *bridge)
{
    for (uint8_t index = 0; index < USB_BRIDGE_ENDPOINT_COUNT; ++index) {
        usb_bridge_endpoint_t current = {0};
        usb_bridge_endpoint_get(index, &current);
        usb_bridge_endpoint_t previous = bridge->endpoints[index];
        if (current.generation == previous.generation &&
            current.rx_cables == previous.rx_cables &&
            current.tx_cables == previous.tx_cables &&
            current.route_id == previous.route_id) {
            continue;
        }

        if (midi_router_set_endpoint(&bridge->router, index, current.rx_cables,
                                     current.tx_cables, current.generation,
                                     current.generation != 0)) {
            bridge->endpoints[index] = current;
            if (current.route_id != 0u) {
                (void)midi_router_set_route_id(&bridge->router, index,
                                               current.route_id);
            }
            bridge->next_tx_cable[index] = 0;
        }
    }
}

static void send_packet(bridge_t *bridge, uint8_t index)
{
    uint8_t cable_count = bridge->endpoints[index].tx_cables;
    uint8_t cable = bridge->next_tx_cable[index];
    for (uint8_t checked = 0; checked < cable_count; ++checked) {
        uint8_t next = (uint8_t)(cable + 1u);
        if (next == cable_count) {
            next = 0;
        }

        usb_bridge_packet_t packet;
        if (midi_router_peek(&bridge->router, index, cable, packet.bytes)) {
            if (usb_bridge_try_write(index, &packet)) {
                midi_router_consume(&bridge->router, index, cable);
                bridge->next_tx_cable[index] = next;
                ++bridge->packets_sent;
            }
            return;
        }
        cable = next;
    }
}

void bridge_step(bridge_t *bridge)
{
    usb_bridge_task();
    // Capture every mount change before routing packets to any destination.
    sync_endpoints(bridge);

    for (uint8_t index = 0; index < USB_BRIDGE_ENDPOINT_COUNT; ++index) {
        usb_bridge_packet_t packet;
        if (bridge->endpoints[index].rx_cables && usb_bridge_read_packet(index, &packet)) {
            midi_router_push(&bridge->router, index, packet.bytes);
            ++bridge->packets_received;
        }
    }

    for (uint8_t index = 0; index < USB_BRIDGE_ENDPOINT_COUNT; ++index) {
        send_packet(bridge, index);
    }
    usb_bridge_flush();
}
