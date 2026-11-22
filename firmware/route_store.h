#ifndef RPICO_BRIDGE_ROUTE_STORE_H
#define RPICO_BRIDGE_ROUTE_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "led_config.h"
#include "route_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROUTE_STORE_MAX_ID 26u
#define ROUTE_STORE_NAME_MAX_BYTES 40u

void route_store_init(void);
void route_store_task(uint32_t now_ms, bool midi_active);
const route_policy_t *route_store_policy(void);
bool route_store_save_policy(const route_policy_t *policy);
const led_config_t *route_store_led(void);
bool route_store_save_led(const led_config_t *config);
const char *route_store_last_error(void);
bool route_store_restart_pending(void);
void route_store_service_restart(uint32_t now_ms);
uint8_t route_store_register(uint64_t key, const char *name);
const char *route_store_device_name(uint8_t id);

#ifdef __cplusplus
}
#endif

#endif
