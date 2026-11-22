#ifndef RPICO_BRIDGE_LED_CONFIG_H
#define RPICO_BRIDGE_LED_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define LED_CONFIG_COLOR_COUNT 7u
#define LED_CONFIG_READY 0u
#define LED_CONFIG_HUB 1u
#define LED_CONFIG_MIDI 2u
#define LED_CONFIG_MOUSE 3u
#define LED_CONFIG_TRAFFIC 4u
#define LED_CONFIG_BOOTSEL 5u
#define LED_CONFIG_ERROR 6u

typedef struct {
    uint8_t rgb[LED_CONFIG_COLOR_COUNT][3];
    uint16_t midi_pulse_ms;
    uint16_t mouse_phase_ms;
    uint16_t mouse_hold_ms;
} led_config_t;

void led_config_default(led_config_t *config);
bool led_config_validate(const led_config_t *config);

#endif
