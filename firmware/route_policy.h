#ifndef RPICO_BRIDGE_ROUTE_POLICY_H
#define RPICO_BRIDGE_ROUTE_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROUTE_POLICY_MAX_RULES 24u
#define ROUTE_ANY 255u
#define ROUTE_SYSTEM_CHANNEL 16u

typedef enum {
    ROUTE_ALL = 0,
    ROUTE_DROP = 1,
    ROUTE_TO = 2,
    /* Globally remove one exact logical USB cable from the routing graph. */
    ROUTE_EXCLUDE = 3
} route_action_t;

typedef struct {
    uint8_t source_id;
    uint8_t source_cable;
    uint8_t channel;
    uint8_t action;
    uint8_t destination_id;
    uint8_t destination_cable;
} route_rule_t;

typedef struct {
    uint8_t count;
    uint8_t default_action;
    route_rule_t rules[ROUTE_POLICY_MAX_RULES];
} route_policy_t;

/* IDs: 0 wildcard, 1 native, 2 CDC, host device IDs start at 3. */
bool route_policy_validate(const route_policy_t *policy);

#ifdef __cplusplus
}
#endif

#endif
