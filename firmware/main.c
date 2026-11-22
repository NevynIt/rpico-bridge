#include "bridge.h"

#include <inttypes.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "pico/status_led.h"
#include "cdc_bench.h"
#include "route_control.h"
#include "route_store.h"
#include "led_config.h"
#if !defined(RPICO_CDC_BENCH)
#include "tusb.h"
#include "usb_host_hid.h"
#endif

static bridge_t bridge;

#define LED_UPDATE_INTERVAL_MS 1u

#if !defined(RPICO_CDC_BENCH)
typedef struct {
    uint32_t events_seen;
    uint32_t active_until_ms;
    uint32_t next_toggle_ms;
    bool active;
    bool phase_on;
} activity_t;
#endif

static uint32_t midi_pulse_until_ms;
static bool midi_pulse_pending;
static bool midi_pulse_active;
static uint32_t last_route_activity;
#if !defined(RPICO_CDC_BENCH)
static activity_t mouse_activity;
#endif

static unsigned configured_hub_count(void)
{
#if !defined(RPICO_CDC_BENCH)
    unsigned count = 0;
    for (uint8_t address = CFG_TUH_DEVICE_MAX + 1u;
         address <= CFG_TUH_DEVICE_MAX + CFG_TUH_HUB; ++address) {
        count += tuh_mounted(address);
    }
    return count;
#else
    return 0;
#endif
}

static unsigned host_interface_count(void)
{
#if defined(RPICO_CDC_BENCH)
    return 0;
#else
    unsigned count = 0;
    for (uint8_t index = USB_BRIDGE_HOST_ENDPOINT_FIRST;
         index < USB_BRIDGE_HOST_ENDPOINT_FIRST + USB_BRIDGE_HOST_ENDPOINT_COUNT; ++index) {
        if (bridge.endpoints[index].generation != 0) {
            ++count;
        }
    }
    return count;
#endif
}

#if !defined(RPICO_CDC_BENCH)
static void update_mouse_activity(activity_t *activity, uint32_t event_count,
                                  uint32_t now_ms, uint16_t phase_ms,
                                  uint16_t hold_ms)
{
    if (event_count != activity->events_seen) {
        activity->events_seen = event_count;
        if (!activity->active) {
            activity->phase_on = true;
            activity->next_toggle_ms = now_ms + phase_ms;
        }
        activity->active = true;
        activity->active_until_ms = now_ms + hold_ms;
    }

    if (activity->active &&
        (int32_t)(now_ms - activity->active_until_ms) >= 0) {
        activity->active = false;
        activity->phase_on = false;
    } else if (activity->active &&
               (int32_t)(now_ms - activity->next_toggle_ms) >= 0) {
        activity->phase_on = !activity->phase_on;
        activity->next_toggle_ms = now_ms + phase_ms;
    }
}
#endif

static void update_status_led(uint32_t desired_color, uint32_t *displayed_color)
{
    if (*displayed_color == desired_color) {
        return;
    }

    // The SDK only sends a new color on an off-to-on transition.
    if (*displayed_color) {
        colored_status_led_set_state(false);
        *displayed_color = 0;
    } else if (desired_color != 0u) {
        colored_status_led_set_on_with_color(desired_color);
        *displayed_color = desired_color;
    }
}

static void report_status(unsigned host_count)
{
    char line[112];
    int length = snprintf(line, sizeof(line),
                          "midi host=%u rx=%" PRIu32 " tx=%" PRIu32
                          " drops=%" PRIu32 " invalid=%" PRIu32 "\r\n",
                          host_count, bridge.packets_received, bridge.packets_sent,
                          bridge.router.overflow_drops, bridge.router.invalid_packets);
    if (length > 0 && (size_t)length < sizeof(line)) {
        usb_bridge_cdc_write(line, (uint32_t)length);
    }
}

int main(void)
{
    // PIO USB requires an integer multiple of its 12 MHz bit clock.
    set_sys_clock_khz(120000, true);
    bridge_init(&bridge);
    route_store_init();
    (void)bridge_set_policy(&bridge, route_store_policy());
    route_control_init(&bridge);
    last_route_activity = bridge.packets_received;
    midi_pulse_until_ms = 0u;
    midi_pulse_pending = false;
    midi_pulse_active = false;
#if !defined(RPICO_CDC_BENCH)
    mouse_activity.events_seen = usb_host_hid_activity_count();
#endif
    bool usb_available = usb_bridge_init();
    // Initialize after PIO USB has reserved its instruction memory and state machines.
    bool led_available = status_led_init();
    if (!usb_available) {
        if (led_available) {
            const led_config_t *led = route_store_led();
            colored_status_led_set_on_with_color(PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(
                led->rgb[LED_CONFIG_ERROR][0], led->rgb[LED_CONFIG_ERROR][1],
                led->rgb[LED_CONFIG_ERROR][2]));
        }
        for (;;) {
            __wfi();
        }
    }
    uint32_t displayed_color = 0;
    uint32_t next_led_ms = 0;
    uint32_t next_report_ms = 0;

    for (;;) {
        bridge_step(&bridge);

#if !defined(RPICO_CDC_BENCH)
        usb_host_hid_task();
#endif

        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        bool route_midi_active = bridge.packets_received != last_route_activity;
        last_route_activity = bridge.packets_received;
        route_store_task(now_ms, route_midi_active);
        route_control_task();
        const led_config_t *led = route_store_led();
        if (route_midi_active) {
            midi_pulse_pending = true;
            if (midi_pulse_active && displayed_color == 0u) {
                midi_pulse_until_ms = now_ms + led->midi_pulse_ms;
            }
        }
        if (midi_pulse_active &&
            (int32_t)(now_ms - midi_pulse_until_ms) >= 0) {
            midi_pulse_active = false;
        }
#if !defined(RPICO_CDC_BENCH)
        update_mouse_activity(&mouse_activity, usb_host_hid_activity_count(), now_ms,
                              led->mouse_phase_ms, led->mouse_hold_ms);
#endif
        if (led_available && (int32_t)(now_ms - next_led_ms) >= 0) {
            bool show_traffic = false;
            uint32_t desired_color = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(
                led->rgb[LED_CONFIG_READY][0], led->rgb[LED_CONFIG_READY][1],
                led->rgb[LED_CONFIG_READY][2]);
            if (configured_hub_count() != 0) {
                desired_color = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(
                    led->rgb[LED_CONFIG_HUB][0], led->rgb[LED_CONFIG_HUB][1],
                    led->rgb[LED_CONFIG_HUB][2]);
            }
            if (host_interface_count() != 0) {
                desired_color = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(
                    led->rgb[LED_CONFIG_MIDI][0], led->rgb[LED_CONFIG_MIDI][1],
                    led->rgb[LED_CONFIG_MIDI][2]);
            }
            if (cdc_bench_bootsel_pending()) {
                desired_color = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(
                    led->rgb[LED_CONFIG_BOOTSEL][0], led->rgb[LED_CONFIG_BOOTSEL][1],
                    led->rgb[LED_CONFIG_BOOTSEL][2]);
            } else if (midi_pulse_pending || midi_pulse_active) {
                show_traffic = true;
                desired_color = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(
                    led->rgb[LED_CONFIG_TRAFFIC][0], led->rgb[LED_CONFIG_TRAFFIC][1],
                    led->rgb[LED_CONFIG_TRAFFIC][2]);
#if !defined(RPICO_CDC_BENCH)
            } else if (mouse_activity.active) {
                if (mouse_activity.phase_on) {
                    desired_color = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(
                        led->rgb[LED_CONFIG_MOUSE][0], led->rgb[LED_CONFIG_MOUSE][1],
                        led->rgb[LED_CONFIG_MOUSE][2]);
                }
#endif
            }
            update_status_led(desired_color, &displayed_color);
            if (midi_pulse_pending && show_traffic && displayed_color == desired_color) {
                midi_pulse_pending = false;
                midi_pulse_active = true;
                midi_pulse_until_ms = now_ms + led->midi_pulse_ms;
            }
            next_led_ms = now_ms + LED_UPDATE_INTERVAL_MS;
        }
        if ((int32_t)(now_ms - next_report_ms) >= 0) {
            report_status(host_interface_count());
            next_report_ms = now_ms + 1000u;
        }
        cdc_bench_service_reset(now_ms);
        route_store_service_restart(now_ms);
    }
}
