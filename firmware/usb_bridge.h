#ifndef RPICO_BRIDGE_USB_BRIDGE_H
#define RPICO_BRIDGE_USB_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

/* Endpoint 0 is native MIDI; PIO MIDI slots are 1..8; CDC MIDI is slot 9. */
#define USB_BRIDGE_ENDPOINT_COUNT 10u
#define USB_BRIDGE_HOST_ENDPOINT_FIRST 1u
#define USB_BRIDGE_HOST_ENDPOINT_COUNT 8u
#define USB_BRIDGE_CDC_ENDPOINT \
    (USB_BRIDGE_HOST_ENDPOINT_FIRST + USB_BRIDGE_HOST_ENDPOINT_COUNT)

typedef struct {
    uint8_t rx_cables;
    uint8_t tx_cables;
    /* Zero means inactive. A new value identifies every mount/unmount change. */
    uint32_t generation;
} usb_bridge_endpoint_t;

/* A raw USB-MIDI 1.0 event packet. Byte 0 retains cable number and CIN. */
typedef struct {
    uint8_t bytes[4];
} usb_bridge_packet_t;

/* Call once before entering the foreground loop. */
bool usb_bridge_init(void);

/* Pump native-device and host stacks without waiting for transfers to finish. */
void usb_bridge_task(void);

/* Flush queued nonblocking USB writes where supported. */
void usb_bridge_flush(void);

/* Snapshot an endpoint. Inactive endpoints return generation zero. */
bool usb_bridge_endpoint_get(uint8_t endpoint, usb_bridge_endpoint_t *out);

/* Read one packet from the endpoint's receive queue. */
bool usb_bridge_read_packet(uint8_t endpoint, usb_bridge_packet_t *out);

/* Queue one packet for transmission; false means inactive or backpressured. */
bool usb_bridge_try_write(uint8_t endpoint, const usb_bridge_packet_t *packet);

/* Nonblocking CDC diagnostics. Write accepts all bytes or returns zero. */
uint32_t usb_bridge_cdc_write(const void *data, uint32_t length);
uint32_t usb_bridge_cdc_read(void *data, uint32_t capacity);

#endif
