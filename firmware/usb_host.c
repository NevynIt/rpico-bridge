#include "pico/stdlib.h"
#include "pio_usb.h"
#include "tusb.h"

#include <stddef.h>

#include "usb_bridge.h"

#if TUSB_VERSION_NUMBER != 2100
#error "rpico-bridge USB transport requires TinyUSB 0.21.0"
#endif

/*
 * TinyUSB and PIO-USB callbacks run on this foreground core. TinyUSB 0.21's
 * host enumeration is asynchronous, so both bus roles can be serviced without
 * a second core or a cross-core packet queue.
 */
#define USB_BRIDGE_PIO_DP_PIN 14u

static usb_bridge_endpoint_t endpoints[USB_BRIDGE_ENDPOINT_COUNT];
static uint32_t next_generation = 1;

static uint32_t generation_next(void) {
    uint32_t result = next_generation++;
    if (result == 0) {
        result = next_generation++;
    }
    return result;
}

static void endpoint_activate(uint8_t endpoint, uint8_t rx_cables, uint8_t tx_cables) {
    endpoints[endpoint].rx_cables = rx_cables;
    endpoints[endpoint].tx_cables = tx_cables;
    endpoints[endpoint].generation = generation_next();
}

static void endpoint_deactivate(uint8_t endpoint) {
    endpoints[endpoint].rx_cables = 0;
    endpoints[endpoint].tx_cables = 0;
    endpoints[endpoint].generation = 0;
}

bool usb_bridge_init(void) {
    pio_usb_configuration_t pio_config = PIO_USB_DEFAULT_CONFIG;
    pio_config.pin_dp = USB_BRIDGE_PIO_DP_PIN;

    const tusb_rhport_init_t host_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_FULL
    };
    const tusb_rhport_init_t device_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL
    };

    if (!tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_config) ||
        !tusb_init(1, &host_init)) {
        return false;
    }
    return tusb_init(0, &device_init);
}

void usb_bridge_task(void) {
    tud_task_ext(0, false);
    tuh_task_ext(0, false);
}

void usb_bridge_flush(void) {
    tud_cdc_write_flush();
    for (uint8_t idx = 0; idx < USB_BRIDGE_HOST_ENDPOINT_COUNT; ++idx) {
        const uint8_t endpoint = (uint8_t)(idx + USB_BRIDGE_HOST_ENDPOINT_FIRST);
        if (endpoints[endpoint].generation != 0 && endpoints[endpoint].tx_cables != 0) {
            (void)tuh_midi_write_flush(idx);
        }
    }
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
    return tuh_midi_packet_read((uint8_t)(endpoint - USB_BRIDGE_HOST_ENDPOINT_FIRST), out->bytes);
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
    return tuh_midi_packet_write((uint8_t)(endpoint - USB_BRIDGE_HOST_ENDPOINT_FIRST), packet->bytes);
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

void tud_mount_cb(void) {
    endpoint_activate(0, 1, 1);
}

void tud_umount_cb(void) {
    endpoint_deactivate(0);
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *mount_info) {
    (void)mount_info;
    if (idx < USB_BRIDGE_HOST_ENDPOINT_COUNT) {
        endpoint_activate((uint8_t)(idx + USB_BRIDGE_HOST_ENDPOINT_FIRST),
                          tuh_midi_get_rx_cable_count(idx),
                          tuh_midi_get_tx_cable_count(idx));
    }
}

void tuh_midi_umount_cb(uint8_t idx) {
    if (idx < USB_BRIDGE_HOST_ENDPOINT_COUNT) {
        endpoint_deactivate((uint8_t)(idx + USB_BRIDGE_HOST_ENDPOINT_FIRST));
    }
}
