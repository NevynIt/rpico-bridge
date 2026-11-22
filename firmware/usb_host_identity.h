#ifndef RPICO_BRIDGE_USB_HOST_IDENTITY_H
#define RPICO_BRIDGE_USB_HOST_IDENTITY_H

#include <stdint.h>

void usb_host_identity_mount(uint8_t idx, uint8_t daddr, uint8_t interface_number);
void usb_host_identity_unmount(uint8_t idx);
void usb_host_identity_task(void);
uint8_t usb_host_identity_route_id(uint8_t idx, uint8_t daddr);

#endif
