#ifndef RPICO_BRIDGE_TUSB_CONFIG_H
#define RPICO_BRIDGE_TUSB_CONFIG_H

#define CFG_TUSB_MCU OPT_MCU_RP2040
#define CFG_TUSB_OS OPT_OS_PICO

#define CFG_TUD_ENABLED 1
#if defined(RPICO_CDC_BENCH)
#define CFG_TUH_ENABLED 0
#else
#define CFG_TUH_ENABLED 1
#define CFG_TUH_RPI_PIO_USB 1
#endif

#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_CDC 1
#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 256
#define CFG_TUD_CDC_EP_BUFSIZE 64
#define CFG_TUD_MIDI 1
#define CFG_TUD_MIDI_RX_BUFSIZE 256
#define CFG_TUD_MIDI_TX_BUFSIZE 256
#define CFG_TUD_MIDI_EP_BUFSIZE 64
#if defined(RPICO_DOCS_DRIVE)
#define CFG_TUD_MSC 1
/* One 512-byte sector buffer; USB bulk endpoints themselves remain 64 bytes. */
#define CFG_TUD_MSC_EP_BUFSIZE 512
#else
#define CFG_TUD_MSC 0
#endif

/* USB MIDI host driver and asynchronous hub enumeration require TinyUSB 0.21.0. */
#if !defined(RPICO_CDC_BENCH)
#define CFG_TUH_HUB 2
/* Seven hub ports, plus two hub nodes: worst-case 28 of PIO-USB's 32 EP slots. */
#define CFG_TUH_DEVICE_MAX 7
#define CFG_TUH_ENUMERATION_BUFSIZE 512
#define CFG_TUH_MIDI 8
#define CFG_TUH_MIDI_STREAM_API 0
#define CFG_TUH_HID 8
/* Do not change boot/report protocol on unrelated HID interfaces. */
#define CFG_TUH_HID_SET_PROTOCOL_ON_ENUM 0
#endif

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

#endif
