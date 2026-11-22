#ifndef RPICO_BRIDGE_TUSB_CONFIG_H
#define RPICO_BRIDGE_TUSB_CONFIG_H

#define CFG_TUSB_MCU OPT_MCU_RP2040
#define CFG_TUSB_OS OPT_OS_PICO

#define CFG_TUD_ENABLED 1
#define CFG_TUH_ENABLED 1
#define CFG_TUH_RPI_PIO_USB 1

#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_CDC 1
#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 256
#define CFG_TUD_CDC_EP_BUFSIZE 64
#define CFG_TUD_MIDI 1
#define CFG_TUD_MIDI_RX_BUFSIZE 256
#define CFG_TUD_MIDI_TX_BUFSIZE 256
#define CFG_TUD_MIDI_EP_BUFSIZE 64

/* USB MIDI host driver and asynchronous hub enumeration require TinyUSB 0.21.0. */
#define CFG_TUH_HUB 2
/* Seven hub ports, plus two hub nodes: worst-case 28 of PIO-USB's 32 EP slots. */
#define CFG_TUH_DEVICE_MAX 7
#define CFG_TUH_ENUMERATION_BUFSIZE 512
#define CFG_TUH_MIDI 8
#define CFG_TUH_MIDI_STREAM_API 0

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

#endif
