#ifndef RPICO_BRIDGE_USB_BRIDGE_MOCK_H
#define RPICO_BRIDGE_USB_BRIDGE_MOCK_H

#include "usb_bridge.h"

void usb_mock_reset(void);
void usb_mock_set_endpoint(uint8_t endpoint, uint8_t rx_cables,
                           uint8_t tx_cables, uint32_t generation);
void usb_mock_push_input(uint8_t endpoint, const uint8_t packet[4]);
void usb_mock_set_write_ready(uint8_t endpoint, bool ready);
bool usb_mock_pop_output(uint8_t endpoint, uint8_t packet[4]);
uint32_t usb_mock_flush_count(void);

#endif
