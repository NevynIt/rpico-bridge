# Hardware

## What you need to do

Send a photo of both sides of one USB-C breakout board, or its product link. The pad labels and any resistors will help us determine whether it can serve as the host socket.

Leave your current cables and board as they are for now. The team will work out the adapter and provide connection and test steps once the design is ready. The source links are there for reference.

## Board

The target is the Waveshare RP2040-Zero. The board's USB-C receptacle connects to the native RP2040 USB pins and supplies board power. The onboard WS2812 LED uses GPIO16. **BOOT** pulls QSPI chip select low for BOOTSEL, and **RESET** pulls RUN low. The Pico SDK board definition selects a default WS2812 pin of GPIO16 and 2 MiB of flash. [Waveshare schematic](https://files.waveshare.com/upload/4/4c/RP2040_Zero.pdf) · [Pico SDK board header](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/boards/include/boards/waveshare_rp2040_zero.h)

## Proposed connections

| Connection | Purpose |
| --- | --- |
| Native USB-C → hub power-only port | Board power in standalone use |
| Native USB-C → laptop | Board power and the native MIDI + CDC device interfaces |
| PIO USB host → hub upstream data connector | Enumerate and communicate with MIDI peripherals |
| Hub downstream data ports → MIDI peripherals | Peripheral power and data |

Use either the hub's power-only port for standalone board power or the laptop for native MIDI + CDC testing. The PIO host connection stays in place in both modes. The first bench target is the existing wall-powered hub with seven USB 3.0 data ports and four power-only ports. Its exact model and internal USB 2.0 topology remain unconfirmed.

GPIO14 (D+) and GPIO15 (D−) are candidates for the adjacent PIO USB data pins. The adapter also needs a shared signal ground. [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB/tree/b186e36bea0f3bcc2ee94ca1fa81e7f71281443a)

## Hub cable and USB-C breakout

The existing hub cable is USB 3.0 Standard-A to Standard-B. A USB-C plug to USB 3 Standard-B cable supports the proposed USB-C host socket. The bridge uses the cable's USB 2.0 data pair at full speed (12 Mbit/s). A USB-C plug to USB-A socket adapter can also be tried with the existing cable. The USB-IF specifies that adapter for direct-attach devices, so verify the cable-plus-adapter combination on the bench. [USB Type-C specification, §3.5.3 and §3.6.1](https://www.usb.org/sites/default/files/USB%20Type-C%20Spec%20R2.0%20-%20August%202019_0.pdf)

A USB-C host socket needs USB 2.0 data in both plug orientations. It uses host pull-ups on CC1 and CC2 to detect an attached device, then switches on protected 5 V at the socket. Power switches off on unplug. Check the breakout's CC wiring and any device-side pull-downs before drawing a wiring diagram. [USB Type-C specification, §2.3 and §4.4.2](https://www.usb.org/sites/default/files/USB%20Type-C%20Spec%20R2.0%20-%20August%202019_0.pdf)

## Host VBUS

The hub's wall supply powers the MIDI devices. A self-powered hub may still draw up to 100 mA from the host's 5 V supply at its upstream connector. The PIO GPIOs carry data only. An adapter power feed must stay separate from the board's native USB-C 5 V rail to prevent the laptop and hub supplies from driving each other. [USB 2.0 specification, §7.2.1.2](https://www.usb.org/document-library/usb-20-specification)

The team will check for 5 V coming back from the hub and verify the hub's USB 2.0 topology on the bench. Those checks guide the adapter power feed, protection and parts list.
