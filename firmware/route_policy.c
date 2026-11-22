#include "route_policy.h"

bool route_policy_validate(const route_policy_t *policy)
{
    if (policy == 0 || policy->count > ROUTE_POLICY_MAX_RULES ||
        (policy->default_action != ROUTE_ALL &&
         policy->default_action != ROUTE_DROP)) {
        return false;
    }

    for (uint8_t i = 0; i < policy->count; ++i) {
        const route_rule_t *rule = &policy->rules[i];
        if (rule->source_cable != ROUTE_ANY && rule->source_cable > 15u) {
            return false;
        }
        if (rule->channel != ROUTE_ANY && rule->channel > ROUTE_SYSTEM_CHANNEL) {
            return false;
        }
        if (rule->action > ROUTE_EXCLUDE) {
            return false;
        }
        if (rule->action == ROUTE_EXCLUDE &&
            (rule->source_id == 0u || rule->source_cable > 15u ||
             rule->channel != ROUTE_ANY || rule->destination_id != 0u ||
             rule->destination_cable != ROUTE_ANY)) {
            return false;
        }
        if (rule->action == ROUTE_TO &&
            (rule->destination_id == 0u ||
             (rule->destination_cable != ROUTE_ANY &&
              rule->destination_cable > 15u))) {
            return false;
        }
    }
    return true;
}
