#ifndef RPICO_BRIDGE_USB_HOST_HID_H
#define RPICO_BRIDGE_USB_HOST_HID_H

#include <stdint.h>

/* Main-loop service and monotonic event count for recognized mouse reports. */
void usb_host_hid_task(void);
uint32_t usb_host_hid_activity_count(void);

#endif
