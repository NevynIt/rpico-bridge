#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"

/* Development identity only. Replace with an assigned VID/PID for releases. */
#define RPICO_BRIDGE_USB_VID 0xCafe
#define RPICO_BRIDGE_USB_PID 0x4010

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_MIDI,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT   0x02
#define EPNUM_CDC_IN    0x82
#define EPNUM_MIDI_OUT  0x03
#define EPNUM_MIDI_IN   0x83

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)

tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = RPICO_BRIDGE_USB_VID,
    .idProduct = RPICO_BRIDGE_USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          0, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 5, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64)
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_fs_configuration;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t descriptor[32];
    static char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    static bool serial_ready;
    const char *value;
    uint8_t count;

    if (index == 0) {
        descriptor[0] = (TUSB_DESC_STRING << 8) | 4;
        descriptor[1] = 0x0409;
        return descriptor;
    }
    if (index == 1) {
        value = "rpico-bridge development";
    } else if (index == 2) {
        value = "USB MIDI 1.0 Bridge";
    } else if (index == 3) {
        if (!serial_ready) {
            pico_get_unique_board_id_string(serial, sizeof(serial));
            serial_ready = true;
        }
        value = serial;
    } else if (index == 4) {
        value = "Diagnostics";
    } else if (index == 5) {
        value = "MIDI";
    } else {
        return NULL;
    }

    count = (uint8_t)strlen(value);
    if (count > 31) {
        count = 31;
    }
    for (uint8_t i = 0; i < count; ++i) {
        descriptor[1 + i] = (uint8_t)value[i];
    }
    descriptor[0] = (TUSB_DESC_STRING << 8) | (2u * count + 2u);
    return descriptor;
}
