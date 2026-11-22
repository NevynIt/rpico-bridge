#include "pico/stdlib.h"
#include "pico/time.h"
#if !defined(RPICO_CDC_BENCH)
#include "pio_usb.h"
#endif
#include "tusb.h"
#if !defined(RPICO_CDC_BENCH)
#include "host/hcd.h"
#endif

#include <stddef.h>

#include "usb_bridge.h"
#include "cdc_bench.h"
#if !defined(RPICO_CDC_BENCH)
#include "usb_host_identity.h"
#endif

#if TUSB_VERSION_NUMBER != 2100
#error "rpico-bridge USB transport requires TinyUSB 0.21.0"
#endif

#if !defined(RPICO_CDC_BENCH)
/*
 * TinyUSB and PIO-USB callbacks run on this foreground core. TinyUSB 0.21's
 * host enumeration is asynchronous, so both bus roles can be serviced without
 * a second core or a cross-core packet queue.
 */
#define USB_BRIDGE_PIO_DP_PIN 14u
static repeating_timer_t pio_frame_timer;
static bool pio_frame_timer_started;
static volatile bool pio_host_paused;

static bool pio_host_frame_timer_callback(repeating_timer_t *timer) {
    (void)timer;
    if (!pio_host_paused) {
        pio_usb_host_frame();
    }
    return true;
}
#endif

static usb_bridge_endpoint_t endpoints[USB_BRIDGE_ENDPOINT_COUNT];
static uint32_t next_generation = 1;
static bool cdc_dtr_active;

static uint32_t generation_next(void) {
    uint32_t result = next_generation++;
    if (result == 0) {
        result = next_generation++;
    }
    return result;
}

static void endpoint_activate(uint8_t endpoint, uint8_t rx_cables, uint8_t tx_cables,
                              uint8_t route_id) {
    endpoints[endpoint].rx_cables = rx_cables;
    endpoints[endpoint].tx_cables = tx_cables;
    endpoints[endpoint].generation = generation_next();
    endpoints[endpoint].route_id = route_id;
}

static void endpoint_deactivate(uint8_t endpoint) {
    endpoints[endpoint].rx_cables = 0;
    endpoints[endpoint].tx_cables = 0;
    endpoints[endpoint].generation = 0;
    endpoints[endpoint].route_id = 0;
}

bool usb_bridge_init(void) {
    const tusb_rhport_init_t device_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL
    };

#if defined(RPICO_CDC_BENCH)
    return tusb_init(0, &device_init);
#else
    pio_usb_configuration_t pio_config = PIO_USB_DEFAULT_CONFIG;
    pio_config.pin_dp = USB_BRIDGE_PIO_DP_PIN;
    pio_config.skip_alarm_pool = true;
    const tusb_rhport_init_t host_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_FULL
    };
    if (!tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_config) ||
        !tusb_init(1, &host_init)) {
        return false;
    }
    // Pin this host's frame loop to an application-owned timer so flash writes
    // can cancel it synchronously before disabling IRQs.
    if (!tusb_init(0, &device_init)) {
        return false;
    }
    pio_host_paused = false;
    pio_frame_timer_started = add_repeating_timer_us(-1000,
        pio_host_frame_timer_callback, NULL, &pio_frame_timer);
    return pio_frame_timer_started;
#endif
}

void usb_bridge_task(void) {
    tud_task_ext(0, false);
#if !defined(RPICO_CDC_BENCH)
    if (!pio_host_paused) {
        tuh_task_ext(0, false);
        usb_host_identity_task();
    }
    for (uint8_t idx = 0; idx < USB_BRIDGE_HOST_ENDPOINT_COUNT; ++idx) {
        uint8_t endpoint = (uint8_t)(idx + USB_BRIDGE_HOST_ENDPOINT_FIRST);
        tuh_itf_info_t info;
        if (endpoints[endpoint].generation == 0 && tuh_midi_itf_get_info(idx, &info)) {
            uint8_t route_id = usb_host_identity_route_id(idx, info.daddr);
            if (route_id != 0u) {
                endpoint_activate(endpoint, tuh_midi_get_rx_cable_count(idx),
                                  tuh_midi_get_tx_cable_count(idx), route_id);
            }
        }
    }
#endif
}

void usb_bridge_flush(void) {
    tud_cdc_write_flush();
#if !defined(RPICO_CDC_BENCH)
    if (!pio_host_paused) {
        for (uint8_t idx = 0; idx < USB_BRIDGE_HOST_ENDPOINT_COUNT; ++idx) {
            const uint8_t endpoint = (uint8_t)(idx + USB_BRIDGE_HOST_ENDPOINT_FIRST);
            if (endpoints[endpoint].generation != 0 && endpoints[endpoint].tx_cables != 0) {
                (void)tuh_midi_write_flush(idx);
            }
        }
    }
#endif
}

bool usb_bridge_endpoint_get(uint8_t endpoint, usb_bridge_endpoint_t *out) {
    if ((endpoint >= USB_BRIDGE_ENDPOINT_COUNT) || (out == NULL)) {
        return false;
    }
    *out = endpoints[endpoint];
    return true;
}

bool usb_bridge_read_packet(uint8_t endpoint, usb_bridge_packet_t *out) {
    if ((out == NULL) || (endpoint >= USB_BRIDGE_ENDPOINT_COUNT) ||
        (endpoints[endpoint].generation == 0)) {
        return false;
    }
    if (endpoint == 0) {
        return tud_midi_packet_read(out->bytes);
    }
    if (endpoint == USB_BRIDGE_CDC_ENDPOINT) {
        return cdc_bench_read_packet(out);
    }
#if !defined(RPICO_CDC_BENCH)
    if (pio_host_paused) return false;
    if ((endpoint >= USB_BRIDGE_HOST_ENDPOINT_FIRST) &&
        (endpoint < USB_BRIDGE_CDC_ENDPOINT)) {
        return tuh_midi_packet_read((uint8_t)(endpoint - USB_BRIDGE_HOST_ENDPOINT_FIRST), out->bytes);
    }
#endif
    return false;
}

bool usb_bridge_try_write(uint8_t endpoint, const usb_bridge_packet_t *packet) {
    if ((packet == NULL) || (endpoint >= USB_BRIDGE_ENDPOINT_COUNT) ||
        (endpoints[endpoint].generation == 0)) {
        return false;
    }
    const uint8_t cable = packet->bytes[0] >> 4;
    if (cable >= endpoints[endpoint].tx_cables) {
        return false;
    }
    if (endpoint == 0) {
        return tud_midi_packet_write(packet->bytes);
    }
    if (endpoint == USB_BRIDGE_CDC_ENDPOINT) {
        return cdc_bench_write_packet(packet);
    }
#if !defined(RPICO_CDC_BENCH)
    if (pio_host_paused) return false;
    if ((endpoint >= USB_BRIDGE_HOST_ENDPOINT_FIRST) &&
        (endpoint < USB_BRIDGE_CDC_ENDPOINT)) {
        return tuh_midi_packet_write((uint8_t)(endpoint - USB_BRIDGE_HOST_ENDPOINT_FIRST), packet->bytes);
    }
#endif
    return false;
}

uint32_t usb_bridge_cdc_write(const void *data, uint32_t length) {
    if ((data == NULL) || (length == 0) || !tud_cdc_connected() ||
        (tud_cdc_write_available() < length)) {
        return 0;
    }
    return (tud_cdc_write(data, length) == length) ? length : 0;
}

uint32_t usb_bridge_cdc_read(void *data, uint32_t capacity) {
    if ((data == NULL) || (capacity == 0) || !tud_cdc_connected()) {
        return 0;
    }
    return tud_cdc_read(data, capacity);
}

bool usb_bridge_pause_pio_host(void) {
#if defined(RPICO_CDC_BENCH)
    return false;
#else
    if (!pio_frame_timer_started || pio_host_paused) return false;
    bool connected = hcd_port_connect_status(1);
    pio_host_paused = true;
    (void)cancel_repeating_timer(&pio_frame_timer);
    pio_frame_timer_started = false;
    return connected;
#endif
}

void usb_bridge_resume_pio_host(void) {
#if !defined(RPICO_CDC_BENCH)
    if (!pio_host_paused) return;
    pio_host_paused = false;
    pio_frame_timer_started = add_repeating_timer_us(-1000,
        pio_host_frame_timer_callback, NULL, &pio_frame_timer);
#endif
}

void tud_mount_cb(void) {
    endpoint_activate(0, 1, 1, 1);
}

void tud_umount_cb(void) {
    endpoint_deactivate(0);
    endpoint_deactivate(USB_BRIDGE_CDC_ENDPOINT);
    cdc_bench_reset_session(false);
    cdc_dtr_active = false;
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)rts;
    if (itf != 0) {
        return;
    }

    if (dtr != cdc_dtr_active) {
        cdc_dtr_active = dtr;
        cdc_bench_reset_session(true);
        if (dtr) {
            endpoint_activate(USB_BRIDGE_CDC_ENDPOINT, 1, 1, 2);
        } else {
            endpoint_deactivate(USB_BRIDGE_CDC_ENDPOINT);
        }
    }
}

#if !defined(RPICO_CDC_BENCH)
void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *mount_info) {
    if (idx < USB_BRIDGE_HOST_ENDPOINT_COUNT && mount_info != NULL) {
        usb_host_identity_mount(idx, mount_info->daddr, mount_info->bInterfaceNumber);
    }
}

void tuh_midi_umount_cb(uint8_t idx) {
    if (idx < USB_BRIDGE_HOST_ENDPOINT_COUNT) {
        usb_host_identity_unmount(idx);
        endpoint_deactivate((uint8_t)(idx + USB_BRIDGE_HOST_ENDPOINT_FIRST));
    }
}
#endif
