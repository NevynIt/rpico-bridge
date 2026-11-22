#include "led_config.h"

#include <stddef.h>

void led_config_default(led_config_t *config)
{
    if (config == NULL) {
        return;
    }

    const led_config_t defaults = {
        .rgb = {
            {12u, 12u, 12u}, /* ready */
            {0u, 8u, 18u},   /* configured hub */
            {0u, 3u, 0u},    /* mounted MIDI interface */
            {0u, 18u, 18u},  /* mouse activity */
            {18u, 10u, 0u},  /* MIDI traffic */
            {18u, 0u, 18u},  /* BOOTSEL */
            {12u, 0u, 0u},   /* initialization error */
        },
        .midi_pulse_ms = 8u,
        .mouse_phase_ms = 250u,
        .mouse_hold_ms = 1500u,
    };
    *config = defaults;
}

bool led_config_validate(const led_config_t *config)
{
    return config != NULL &&
           config->midi_pulse_ms >= 2u && config->midi_pulse_ms <= 100u &&
           config->mouse_phase_ms >= 10u && config->mouse_phase_ms <= 1000u &&
           config->mouse_hold_ms >= config->mouse_phase_ms &&
           config->mouse_hold_ms <= 5000u;
}
