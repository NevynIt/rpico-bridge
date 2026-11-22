#include "usb_host_hid.h"

#include <stdbool.h>
#include <string.h>

#include "tusb.h"

#define MOUSE_REPORT_INFO_COUNT 8u

typedef struct {
    uint8_t dev_addr;
    bool active;
    bool uses_report_ids;
    bool transfer_pending;
    uint8_t mouse_report_id_count;
    uint8_t mouse_report_ids[MOUSE_REPORT_INFO_COUNT];
} mouse_interface_t;

static mouse_interface_t mouse_interfaces[CFG_TUH_HID];
static uint32_t mouse_activity_count;

static bool descriptor_identifies_mouse(mouse_interface_t *mouse,
                                        const uint8_t *descriptor, uint16_t length)
{
    if ((descriptor == NULL) || (length == 0)) {
        return false;
    }

    tuh_hid_report_info_t reports[MOUSE_REPORT_INFO_COUNT];
    const uint8_t count = tuh_hid_parse_report_descriptor(
        reports, MOUSE_REPORT_INFO_COUNT, descriptor, length);
    bool has_mouse = false;
    bool has_other_application = false;
    for (uint8_t i = 0; i < count; ++i) {
        const bool is_mouse = (reports[i].usage_page == HID_USAGE_PAGE_DESKTOP) &&
                              (reports[i].usage == HID_USAGE_DESKTOP_MOUSE);
        if (reports[i].report_id != 0) {
            mouse->uses_report_ids = true;
        }
        if (is_mouse) {
            has_mouse = true;
            if (mouse->mouse_report_id_count < MOUSE_REPORT_INFO_COUNT) {
                mouse->mouse_report_ids[mouse->mouse_report_id_count++] = reports[i].report_id;
            }
        } else if ((reports[i].usage_page != 0) || (reports[i].usage != 0)) {
            has_other_application = true;
        }
    }
    /* If the interface mixes applications without IDs, this cannot exclude keyboard reports. */
    return has_mouse && (mouse->uses_report_ids || !has_other_application);
}

static void queue_mouse_report(uint8_t idx)
{
    mouse_interface_t *mouse = &mouse_interfaces[idx];
    if (mouse->active && !mouse->transfer_pending &&
        tuh_hid_receive_report(mouse->dev_addr, idx)) {
        mouse->transfer_pending = true;
    }
}

void usb_host_hid_task(void)
{
    for (uint8_t idx = 0; idx < CFG_TUH_HID; ++idx) {
        queue_mouse_report(idx);
    }
}

uint32_t usb_host_hid_activity_count(void)
{
    return mouse_activity_count;
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t idx,
                      const uint8_t *report_desc, uint16_t desc_len)
{
    if (idx >= CFG_TUH_HID) {
        return;
    }

    mouse_interface_t candidate = { .dev_addr = dev_addr };
    if (!descriptor_identifies_mouse(&candidate, report_desc, desc_len)) {
        return;
    }

    candidate.active = true;
    mouse_interfaces[idx] = candidate;
    queue_mouse_report(idx);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t idx)
{
    if ((idx < CFG_TUH_HID) && (mouse_interfaces[idx].dev_addr == dev_addr)) {
        memset(&mouse_interfaces[idx], 0, sizeof(mouse_interfaces[idx]));
    }
}

static bool is_mouse_report(const mouse_interface_t *mouse, const uint8_t *report,
                            uint16_t length)
{
    if (length == 0u) {
        return false;
    }

    if (mouse->uses_report_ids) {
        bool recognized_id = false;
        for (uint8_t i = 0; i < mouse->mouse_report_id_count; ++i) {
            if (report[0] == mouse->mouse_report_ids[i]) {
                recognized_id = true;
                break;
            }
        }
        if (!recognized_id) {
            return false;
        }
    }
    /* HID idle rate is set to zero by TinyUSB; each callback is a fresh input report. */
    return true;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t idx,
                                const uint8_t *report, uint16_t len)
{
    if ((idx >= CFG_TUH_HID) || !mouse_interfaces[idx].active ||
        (mouse_interfaces[idx].dev_addr != dev_addr)) {
        return;
    }

    mouse_interface_t *mouse = &mouse_interfaces[idx];
    mouse->transfer_pending = false;
    if (is_mouse_report(mouse, report, len)) {
        ++mouse_activity_count;
    }
    queue_mouse_report(idx);
}
