#include "midi_router.h"

#include <string.h>

static bool policy_excludes_port(const route_policy_t *policy, uint8_t route_id,
                                uint8_t cable);

static void saturating_increment(uint32_t *value)
{
    if (*value != UINT32_MAX) {
        ++*value;
    }
}

static uint16_t source_key(uint8_t endpoint, uint8_t cable)
{
    return (uint16_t)endpoint * MIDI_ROUTER_MAX_CABLES + cable;
}

static bool stream_admitted(const midi_router_output_t *output,
                            uint8_t endpoint, uint8_t cable)
{
    const uint16_t key = source_key(endpoint, cable);
    return (output->admitted_sysex[key / 32u] & (1u << (key % 32u))) != 0u;
}

static void set_stream_admitted(midi_router_output_t *output, uint8_t endpoint,
                                uint8_t cable, bool admitted)
{
    const uint16_t key = source_key(endpoint, cable);
    const uint32_t mask = 1u << (key % 32u);
    if (admitted) {
        output->admitted_sysex[key / 32u] |= mask;
    } else {
        output->admitted_sysex[key / 32u] &= ~mask;
    }
}

void midi_router_init(midi_router_t *router)
{
    if (router != NULL) {
        memset(router, 0, sizeof(*router));
        router->policy.default_action = ROUTE_ALL;
    }
}

static void append_synthetic_eox(midi_router_output_t *output)
{
    output->pending_eox = true;
}

/* Abort only the current, unterminated source stream. Older complete SysEx
 * packets from the same source remain queued. */
static void abort_source_sysex(midi_router_output_t *output, uint8_t endpoint,
                               uint8_t source_cable,
                               uint32_t *overflow_drops)
{
    if (!stream_admitted(output, endpoint, source_cable)) {
        return;
    }

    int last_start = -1;
    for (uint8_t i = 0; i < output->count; ++i) {
        const midi_router_item_t *item = &output->items[i];
        if (item->source_endpoint == endpoint &&
            item->source_cable == source_cable && item->sysex_start) {
            last_start = (int)i;
        }
    }

    uint8_t kept = 0;
    for (uint8_t i = 0; i < output->count; ++i) {
        const midi_router_item_t *item = &output->items[i];
        if ((int)i >= last_start && item->sysex &&
            item->source_endpoint == endpoint && item->source_cable == source_cable) {
            saturating_increment(overflow_drops);
            continue;
        }
        if (kept != i) {
            output->items[kept] = output->items[i];
        }
        ++kept;
    }
    output->count = kept;

    if (last_start < 0 && output->sysex_locked && output->sysex_endpoint == endpoint &&
        output->sysex_cable == source_cable) {
        append_synthetic_eox(output);
    }
    set_stream_admitted(output, endpoint, source_cable, false);
}

static void purge_endpoint_from_output(midi_router_output_t *output,
                                       uint8_t endpoint)
{
    uint8_t kept = 0;
    for (uint8_t i = 0; i < output->count; ++i) {
        if (output->items[i].source_endpoint == endpoint) {
            continue;
        }
        if (kept != i) {
            output->items[kept] = output->items[i];
        }
        ++kept;
    }
    output->count = kept;
    if (output->sysex_locked && output->sysex_endpoint == endpoint) {
        append_synthetic_eox(output);
    }
    for (uint8_t cable = 0; cable < MIDI_ROUTER_MAX_CABLES; ++cable) {
        set_stream_admitted(output, endpoint, cable, false);
    }
}

static void clear_endpoint_traffic(midi_router_t *router, uint8_t endpoint)
{
    for (uint8_t destination = 0; destination < MIDI_ROUTER_MAX_ENDPOINTS;
         ++destination) {
        for (uint8_t cable = 0; cable < MIDI_ROUTER_MAX_CABLES; ++cable) {
            midi_router_output_t *output = &router->outputs[destination][cable];
            if (destination == endpoint) {
                output->count = 0;
                output->sysex_locked = false;
                output->pending_eox = false;
                memset(output->admitted_sysex, 0, sizeof(output->admitted_sysex));
                continue;
            }

            purge_endpoint_from_output(output, endpoint);
        }
    }
    memset(router->source_sysex[endpoint], 0,
           sizeof(router->source_sysex[endpoint]));
}

static void purge_source_from_output(midi_router_output_t *output,
                                     uint8_t source_endpoint,
                                     uint8_t source_cable,
                                     bool destination_excluded)
{
    const bool locked_source = output->sysex_locked &&
                               output->sysex_endpoint == source_endpoint &&
                               output->sysex_cable == source_cable;
    uint8_t kept = 0;
    for (uint8_t i = 0; i < output->count; ++i) {
        const midi_router_item_t *item = &output->items[i];
        if (item->source_endpoint == source_endpoint &&
            item->source_cable == source_cable) {
            continue;
        }
        if (kept != i) {
            output->items[kept] = output->items[i];
        }
        ++kept;
    }
    output->count = kept;
    set_stream_admitted(output, source_endpoint, source_cable, false);

    if (locked_source) {
        if (destination_excluded) {
            output->sysex_locked = false;
            output->pending_eox = false;
        } else {
            /* The receiver already saw this F0. Close it before any other
             * queued stream can proceed. */
            output->pending_eox = true;
        }
    }
}

static void reconcile_exclusions(midi_router_t *router,
                                 const route_policy_t *next_policy)
{
    for (uint8_t destination = 0; destination < MIDI_ROUTER_MAX_ENDPOINTS;
         ++destination) {
        const midi_router_endpoint_t *target = &router->endpoints[destination];
        for (uint8_t destination_cable = 0;
             destination_cable < MIDI_ROUTER_MAX_CABLES; ++destination_cable) {
            const bool was_destination_excluded = policy_excludes_port(
                &router->policy, target->route_id, destination_cable);
            const bool is_destination_excluded = policy_excludes_port(
                next_policy, target->route_id, destination_cable);
            midi_router_output_t *output =
                &router->outputs[destination][destination_cable];

            if (!was_destination_excluded && is_destination_excluded) {
                /* No queued packet, including a synthetic EOX, may reach a
                 * cable after it becomes excluded. */
                memset(output, 0, sizeof(*output));
                continue;
            }

            for (uint8_t source_endpoint = 0;
                 source_endpoint < MIDI_ROUTER_MAX_ENDPOINTS;
                 ++source_endpoint) {
                const uint8_t source_id =
                    router->endpoints[source_endpoint].route_id;
                if (source_id == 0u) {
                    continue;
                }
                for (uint8_t source_cable = 0;
                     source_cable < MIDI_ROUTER_MAX_CABLES; ++source_cable) {
                    const bool was_source_excluded = policy_excludes_port(
                        &router->policy, source_id, source_cable);
                    const bool is_source_excluded = policy_excludes_port(
                        next_policy, source_id, source_cable);
                    if (!was_source_excluded && is_source_excluded) {
                        purge_source_from_output(output, source_endpoint,
                                                 source_cable,
                                                 is_destination_excluded);
                    }
                }
            }
        }
    }
}

bool midi_router_set_endpoint(midi_router_t *router, uint8_t endpoint,
                              uint8_t rx_cables, uint8_t tx_cables,
                              uint32_t generation, bool connected)
{
    if (router == NULL || endpoint >= MIDI_ROUTER_MAX_ENDPOINTS ||
        rx_cables > MIDI_ROUTER_MAX_CABLES ||
        tx_cables > MIDI_ROUTER_MAX_CABLES) {
        return false;
    }

    midi_router_endpoint_t *state = &router->endpoints[endpoint];
    const uint8_t effective_rx = connected ? rx_cables : 0u;
    const uint8_t effective_tx = connected ? tx_cables : 0u;
    if (state->generation != generation || state->connected != connected ||
        state->rx_cables != effective_rx || state->tx_cables != effective_tx) {
        clear_endpoint_traffic(router, endpoint);
    }
    state->generation = generation;
    state->connected = connected;
    state->rx_cables = effective_rx;
    state->tx_cables = effective_tx;
    if (state->route_id == 0u) {
        state->route_id = endpoint == 0u ? 1u :
                          endpoint == MIDI_ROUTER_MAX_ENDPOINTS - 1u ? 2u :
                          (uint8_t)(endpoint + 2u);
    }
    return true;
}

bool midi_router_set_route_id(midi_router_t *router, uint8_t endpoint,
                              uint8_t route_id)
{
    if (router == NULL || endpoint >= MIDI_ROUTER_MAX_ENDPOINTS ||
        route_id == 0u) {
        return false;
    }
    if (router->endpoints[endpoint].route_id != route_id) {
        clear_endpoint_traffic(router, endpoint);
        router->endpoints[endpoint].route_id = route_id;
    }
    return true;
}

bool midi_router_set_policy(midi_router_t *router,
                            const route_policy_t *policy)
{
    if (router == NULL || !route_policy_validate(policy)) {
        return false;
    }
    reconcile_exclusions(router, policy);
    router->policy = *policy;
    return true;
}

void midi_router_remove_endpoint(midi_router_t *router, uint8_t endpoint,
                                 uint32_t generation)
{
    if (router == NULL || endpoint >= MIDI_ROUTER_MAX_ENDPOINTS) {
        return;
    }
    clear_endpoint_traffic(router, endpoint);
    router->endpoints[endpoint].connected = false;
    router->endpoints[endpoint].rx_cables = 0;
    router->endpoints[endpoint].tx_cables = 0;
    router->endpoints[endpoint].generation = generation;
}

static bool packet_is_valid(const uint8_t packet[4])
{
    const uint8_t cin = (uint8_t)(packet[0] & 0x0fu);
    return cin >= 0x02u && cin <= 0x0fu;
}

static bool has_sysex_start(const uint8_t packet[4])
{
    const uint8_t cin = (uint8_t)(packet[0] & 0x0fu);
    const uint8_t length = cin == 0x04u || cin == 0x07u ? 3u :
                           cin == 0x06u ? 2u :
                           cin == 0x05u || cin == 0x0fu ? 1u : 0u;
    for (uint8_t i = 0; i < length; ++i) {
        if (packet[i + 1u] == 0xf0u) {
            return true;
        }
    }
    return false;
}

static bool has_sysex_end(const uint8_t packet[4])
{
    const uint8_t cin = (uint8_t)(packet[0] & 0x0fu);
    const uint8_t length = cin == 0x04u || cin == 0x07u ? 3u :
                           cin == 0x06u ? 2u :
                           cin == 0x05u || cin == 0x0fu ? 1u : 0u;
    for (uint8_t i = 0; i < length; ++i) {
        if (packet[i + 1u] == 0xf7u) {
            return true;
        }
    }
    return false;
}

static bool interrupts_sysex(const uint8_t packet[4], bool realtime,
                             bool starts, bool ends)
{
    /* A new F0 or an unrelated status byte ends the old stream. */
    return !realtime && (starts || (!ends && (packet[1] & 0x80u) != 0u));
}

static bool push_to_output(midi_router_t *router, uint8_t destination,
                           uint8_t cable, const midi_router_item_t *item)
{
    midi_router_output_t *output = &router->outputs[destination][cable];
    if (output->count == MIDI_ROUTER_QUEUE_DEPTH) {
        saturating_increment(&router->overflow_drops);
        if (item->sysex && !item->sysex_start) {
            abort_source_sysex(output, item->source_endpoint, item->source_cable,
                               &router->overflow_drops);
        }
        return false;
    }
    output->items[output->count++] = *item;
    output->items[output->count - 1u].bytes[0] =
        (uint8_t)((item->bytes[0] & 0x0fu) | (uint8_t)(cable << 4));
    return true;
}

static uint8_t packet_channel(const uint8_t packet[4])
{
    const uint8_t cin = (uint8_t)(packet[0] & 0x0fu);
    const uint8_t status = packet[1];
    if (cin >= 0x08u && cin <= 0x0eu &&
        (status >> 4) == cin && status < 0xf0u) {
        return (uint8_t)(status & 0x0fu);
    }
    return ROUTE_SYSTEM_CHANNEL;
}

static const route_rule_t *matching_rule(const midi_router_t *router,
                                         uint8_t source_id,
                                         uint8_t source_cable,
                                         uint8_t channel)
{
    for (uint8_t i = 0; i < router->policy.count; ++i) {
        const route_rule_t *rule = &router->policy.rules[i];
        if (rule->action == ROUTE_EXCLUDE) {
            continue;
        }
        if ((rule->source_id == 0u || rule->source_id == source_id) &&
            (rule->source_cable == ROUTE_ANY ||
             rule->source_cable == source_cable) &&
            (rule->channel == ROUTE_ANY || rule->channel == channel)) {
            return rule;
        }
    }
    return NULL;
}

static bool policy_excludes_port(const route_policy_t *policy, uint8_t route_id,
                                uint8_t cable)
{
    for (uint8_t i = 0; i < policy->count; ++i) {
        const route_rule_t *rule = &policy->rules[i];
        if (rule->action == ROUTE_EXCLUDE && rule->source_id == route_id &&
            rule->source_cable == cable) {
            return true;
        }
    }
    return false;
}

static bool destination_allowed(const midi_router_t *router,
                                const route_rule_t *rule,
                                uint8_t destination)
{
    if (rule == NULL) {
        return router->policy.default_action == ROUTE_ALL;
    }
    if (rule->action == ROUTE_ALL) {
        return true;
    }
    return rule->action == ROUTE_TO &&
           router->endpoints[destination].route_id == rule->destination_id;
}

midi_router_push_result_t midi_router_push(midi_router_t *router,
                                           uint8_t source_endpoint,
                                           const uint8_t packet[4])
{
    if (router == NULL || packet == NULL ||
        source_endpoint >= MIDI_ROUTER_MAX_ENDPOINTS ||
        !router->endpoints[source_endpoint].connected ||
        !packet_is_valid(packet)) {
        if (router != NULL) {
            saturating_increment(&router->invalid_packets);
        }
        return MIDI_ROUTER_PUSH_INVALID;
    }

    const uint8_t source_cable = (uint8_t)(packet[0] >> 4);
    midi_router_endpoint_t *source = &router->endpoints[source_endpoint];
    if (source_cable >= source->rx_cables) {
        saturating_increment(&router->invalid_packets);
        return MIDI_ROUTER_PUSH_INVALID;
    }

    const uint8_t cin = (uint8_t)(packet[0] & 0x0fu);
    const uint8_t channel = packet_channel(packet);
    const route_rule_t *rule = matching_rule(router, source->route_id,
                                              source_cable, channel);
    const bool source_excluded = policy_excludes_port(&router->policy,
                                                       source->route_id,
                                                       source_cable);
    const bool realtime = (cin == 0x05u || cin == 0x0fu) &&
                          packet[1] >= 0xf8u;
    const bool starts = has_sysex_start(packet);
    const bool ends = has_sysex_end(packet);
    bool *source_in_sysex = &router->source_sysex[source_endpoint][source_cable];
    const bool aborting = *source_in_sysex &&
                          interrupts_sysex(packet, realtime, starts, ends);
    if (aborting) {
        for (uint8_t destination = 0;
             destination < MIDI_ROUTER_MAX_ENDPOINTS; ++destination) {
            for (uint8_t cable = 0; cable < router->endpoints[destination].tx_cables;
                 ++cable) {
                abort_source_sysex(&router->outputs[destination][cable],
                                   source_endpoint, source_cable,
                                   &router->overflow_drops);
            }
        }
        *source_in_sysex = false;
    }
    const bool continuation = *source_in_sysex && !starts;
    const bool is_sysex = !realtime && (*source_in_sysex || starts || ends);
    if (starts && !ends) {
        *source_in_sysex = true;
    } else if (ends) {
        *source_in_sysex = false;
    }

    midi_router_item_t item = {0};
    memcpy(item.bytes, packet, MIDI_ROUTER_PACKET_SIZE);
    item.source_endpoint = source_endpoint;
    item.source_cable = source_cable;
    item.sysex = is_sysex;
    item.sysex_start = starts;
    item.sysex_end = is_sysex && ends;
    item.realtime = realtime;

    if (source_excluded) {
        return MIDI_ROUTER_PUSH_NO_DESTINATION;
    }

    bool has_destination = false;
    bool queued = false;
    for (uint8_t destination = 0; destination < MIDI_ROUTER_MAX_ENDPOINTS;
         ++destination) {
        const midi_router_endpoint_t *target = &router->endpoints[destination];
        if (!target->connected) {
            continue;
        }
        for (uint8_t cable = 0; cable < target->tx_cables; ++cable) {
            if (destination == source_endpoint && cable == source_cable) {
                continue;
            }
            if (policy_excludes_port(&router->policy, target->route_id, cable)) {
                continue;
            }
            midi_router_output_t *output = &router->outputs[destination][cable];
            /* A SysEx continuation must reach every destination that received
             * its F0, even if the active policy changed mid-stream. */
            if (continuation && !realtime &&
                stream_admitted(output, source_endpoint, source_cable)) {
                has_destination = true;
                const bool output_queued = push_to_output(router, destination,
                                                           cable, &item);
                if (output_queued && item.sysex_end) {
                    set_stream_admitted(output, source_endpoint, source_cable,
                                        false);
                }
                queued = output_queued || queued;
                continue;
            }
            if ((rule != NULL && rule->action == ROUTE_DROP) ||
                !destination_allowed(router, rule, destination) ||
                (rule != NULL && rule->action == ROUTE_TO &&
                 rule->destination_cable != ROUTE_ANY &&
                 cable != rule->destination_cable)) {
                continue;
            }
            if (continuation && !realtime &&
                !stream_admitted(output, source_endpoint, source_cable)) {
                continue;
            }
            has_destination = true;
            const bool output_queued =
                push_to_output(router, destination, cable, &item);
            if (output_queued) {
                if (item.sysex_start && !item.sysex_end) {
                    set_stream_admitted(output, source_endpoint, source_cable,
                                        true);
                }
                if (item.sysex_end) {
                    set_stream_admitted(output, source_endpoint, source_cable,
                                        false);
                }
            }
            queued = output_queued || queued;
        }
    }
    if (queued) {
        return MIDI_ROUTER_PUSH_ROUTED;
    }
    return has_destination ? MIDI_ROUTER_PUSH_DROPPED :
                             MIDI_ROUTER_PUSH_NO_DESTINATION;
}

static int eligible_index(const midi_router_output_t *output)
{
    if (output->pending_eox) {
        return -2;
    }
    if (output->count == 0u) {
        return -1;
    }
    if (!output->sysex_locked) {
        return 0;
    }
    for (uint8_t i = 0; i < output->count; ++i) {
        const midi_router_item_t *item = &output->items[i];
        if (item->realtime || (item->source_endpoint == output->sysex_endpoint &&
             item->source_cable == output->sysex_cable)) {
            return (int)i;
        }
    }
    return -1;
}

bool midi_router_peek(const midi_router_t *router, uint8_t destination_endpoint,
                      uint8_t destination_cable, uint8_t packet[4])
{
    if (router == NULL || packet == NULL ||
        destination_endpoint >= MIDI_ROUTER_MAX_ENDPOINTS ||
        destination_cable >= MIDI_ROUTER_MAX_CABLES) {
        return false;
    }
    const midi_router_output_t *output =
        &router->outputs[destination_endpoint][destination_cable];
    const int index = eligible_index(output);
    if (index == -2) {
        packet[0] = (uint8_t)(0x05u | (destination_cable << 4));
        packet[1] = 0xf7u;
        packet[2] = 0u;
        packet[3] = 0u;
        return true;
    }
    if (index < 0) {
        return false;
    }
    memcpy(packet, output->items[index].bytes, MIDI_ROUTER_PACKET_SIZE);
    return true;
}

bool midi_router_consume(midi_router_t *router, uint8_t destination_endpoint,
                         uint8_t destination_cable)
{
    if (router == NULL || destination_endpoint >= MIDI_ROUTER_MAX_ENDPOINTS ||
        destination_cable >= MIDI_ROUTER_MAX_CABLES) {
        return false;
    }
    midi_router_output_t *output =
        &router->outputs[destination_endpoint][destination_cable];
    const int selected = eligible_index(output);
    if (selected == -2) {
        output->pending_eox = false;
        output->sysex_locked = false;
        return true;
    }
    if (selected < 0) {
        return false;
    }
    midi_router_item_t item = output->items[selected];
    for (uint8_t i = (uint8_t)selected; i + 1u < output->count; ++i) {
        output->items[i] = output->items[i + 1u];
    }
    --output->count;

    if (item.sysex) {
        if (item.sysex_end) {
            if (output->sysex_locked &&
                output->sysex_endpoint == item.source_endpoint &&
                output->sysex_cable == item.source_cable) {
                output->sysex_locked = false;
            }
        } else if (!output->sysex_locked) {
            output->sysex_locked = true;
            output->sysex_endpoint = item.source_endpoint;
            output->sysex_cable = item.source_cable;
        }
    }
    return true;
}
