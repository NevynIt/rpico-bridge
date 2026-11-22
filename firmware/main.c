#include "bridge.h"

#include <inttypes.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "pico/status_led.h"

static bridge_t bridge;

static unsigned host_interface_count(void)
{
#if defined(RPICO_CDC_BENCH)
    return 0;
#else
    unsigned count = 0;
    for (uint8_t index = USB_BRIDGE_HOST_ENDPOINT_FIRST; index < USB_BRIDGE_ENDPOINT_COUNT; ++index) {
        if (bridge.endpoints[index].generation != 0) {
            ++count;
        }
    }
    return count;
#endif
}

static void update_status_led(unsigned host_count, uint32_t *displayed_color)
{
    const uint32_t blue = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(0, 0, 12);
    const uint32_t green = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(0, 12, 0);
    uint32_t desired_color = host_count ? green : blue;
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
        uint8_t discarded_cdc[32];
        usb_bridge_cdc_read(discarded_cdc, sizeof(discarded_cdc));
#endif

        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (led_available && (int32_t)(now_ms - next_led_ms) >= 0) {
            update_status_led(host_interface_count(), &displayed_color);
            next_led_ms = now_ms + 10u;
        }
        if ((int32_t)(now_ms - next_report_ms) >= 0) {
            report_status(host_interface_count());
            next_report_ms = now_ms + 1000u;
        }
    }
}
