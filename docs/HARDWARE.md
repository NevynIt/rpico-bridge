# Hardware

The target is the Waveshare RP2040-Zero. The board's USB-C receptacle goes to the native RP2040 USB pins and supplies board power. The WS2812B uses GPIO16. **BOOT** pulls QSPI chip select low for BOOTSEL, and **RESET** pulls RUN low. The Pico SDK board definition reserves GPIO16 for the LED and selects 2 MiB of flash. [Waveshare schematic](https://files.waveshare.com/upload/4/4c/RP2040_Zero.pdf) · [Pico SDK board header](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/boards/include/boards/waveshare_rp2040_zero.h)

## Proposed connections

| Connection | Purpose |
| --- | --- |
| Native USB-C → hub power-only port | Board power in standalone use |
| Native USB-C → laptop | Board power and the native MIDI + CDC device interfaces |
| PIO USB host → hub upstream data connector | Enumerate and communicate with MIDI peripherals |
| Hub downstream data ports → MIDI peripherals | Peripheral power and data |

The two native USB-C connections are alternatives. The PIO port remains a host in either mode. The board gets power through native USB-C in this setup. The first bench target is the existing wall-powered hub, reported as Venter or Aenter, with seven USB 3.0 data ports and four power-only ports. Its exact model and internal USB 2.0 topology remain unconfirmed.

GPIO14 (D+) and GPIO15 (D−) are exposed adjacent pins and a candidate for the PIO USB data pair. The host interface also needs a shared signal ground. Confirm the hub's upstream connector, series resistors, connector protection and VBUS behavior before finalizing the pinout and BOM. [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB)

## Host VBUS

The hub's wall supply powers its downstream ports. Its upstream USB interface still expects host VBUS for attach detection and may draw some current from it. The PIO data pins supply no VBUS. Keep the host-port VBUS feed separate from the board's native USB-C 5 V rail so a laptop and hub supply cannot drive each other. [USB 2.0 specification, §7.2.1.2](https://www.usb.org/document-library/usb-20-specification)

Before finalizing the adapter, identify the upstream connector and check the hub's USB 2.0 topology, VBUS detection and upstream backfeed. A seven-port enclosure may contain multiple cascaded USB hubs. The host VBUS source, protection and BOM remain open. The proposed connections have not been tested on a board.
