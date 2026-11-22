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
#if !defined(RPICO_CDC_BENCH)
#include "tusb.h"
#include "usb_host_hid.h"
#endif

static bridge_t bridge;

#define LED_UPDATE_INTERVAL_MS 10u
#define MIDI_ACTIVITY_WINDOW_MS 1500u
#define MIDI_ACTIVITY_HALF_PERIOD_MS 250u

typedef struct {
    uint32_t events_seen;
    uint32_t active_until_ms;
    uint32_t next_toggle_ms;
    bool active;
    bool phase_on;
} activity_t;

static activity_t midi_activity;
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

static void update_activity(activity_t *activity, uint32_t event_count,
                            uint32_t now_ms)
{
    if (event_count != activity->events_seen) {
        activity->events_seen = event_count;
        if (!activity->active) {
            activity->phase_on = true;
            activity->next_toggle_ms = now_ms + MIDI_ACTIVITY_HALF_PERIOD_MS;
        }
        activity->active = true;
        activity->active_until_ms = now_ms + MIDI_ACTIVITY_WINDOW_MS;
    }

    if (activity->active &&
        (int32_t)(now_ms - activity->active_until_ms) >= 0) {
        activity->active = false;
        activity->phase_on = false;
    } else if (activity->active &&
               (int32_t)(now_ms - activity->next_toggle_ms) >= 0) {
        activity->phase_on = !activity->phase_on;
        activity->next_toggle_ms = now_ms + MIDI_ACTIVITY_HALF_PERIOD_MS;
    }
}

static void update_status_led(uint32_t desired_color, uint32_t *displayed_color)
{
    if (*displayed_color == desired_color) {
        return;
    }

    // The SDK only sends a new color on an off-to-on transition.
    if (*displayed_color) {
        colored_status_led_set_state(false);
        *displayed_color = 0;
    } else {
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
    midi_activity.events_seen = bridge.packets_received;
#if !defined(RPICO_CDC_BENCH)
    mouse_activity.events_seen = usb_host_hid_activity_count();
#endif
    bool usb_available = usb_bridge_init();
    // Initialize after PIO USB has reserved its instruction memory and state machines.
    bool led_available = status_led_init();
    if (!usb_available) {
        if (led_available) {
            colored_status_led_set_on_with_color(PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(12, 0, 0));
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
        update_activity(&midi_activity, bridge.packets_received, now_ms);
#if !defined(RPICO_CDC_BENCH)
        update_activity(&mouse_activity, usb_host_hid_activity_count(), now_ms);
#endif
        if (led_available && (int32_t)(now_ms - next_led_ms) >= 0) {
            const uint32_t gray = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(12, 12, 12);
            const uint32_t blue = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(0, 8, 18);
            const uint32_t green = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(0, 18, 0);
            const uint32_t amber = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(18, 10, 0);
#if !defined(RPICO_CDC_BENCH)
            const uint32_t cyan = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(0, 18, 18);
#endif
            uint32_t desired_color = gray;
            if (configured_hub_count() != 0) {
                desired_color = blue;
            }
            if (host_interface_count() != 0) {
                desired_color = green;
            }
            const uint32_t magenta = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(18, 0, 18);
            if (cdc_bench_bootsel_pending()) {
                desired_color = magenta;
            } else if (midi_activity.active) {
                desired_color = midi_activity.phase_on ? amber : desired_color;
#if !defined(RPICO_CDC_BENCH)
            } else if (mouse_activity.active) {
                desired_color = mouse_activity.phase_on ? cyan : desired_color;
#endif
            }
            update_status_led(desired_color, &displayed_color);
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
