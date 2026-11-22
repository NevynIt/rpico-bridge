# Hardware

## What you need to do

Start with the [native USB-C check](#bench-acceptance) when the board is available. It only needs the RP2040-Zero, a USB-C data cable and the computer. Leave the breakout and hub disconnected for this check.

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

The prototype firmware uses GPIO14 (D+) and GPIO15 (D−) for PIO USB. The proposed adapter also needs a shared signal ground. The pin pair and adapter still need bench verification. [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB/tree/b186e36bea0f3bcc2ee94ca1fa81e7f71281443a)

## Hub cable and USB-C breakout

The existing hub cable is USB 3.0 Standard-A to Standard-B. A USB-C plug to USB 3 Standard-B cable supports the proposed USB-C host socket. The bridge uses the cable's USB 2.0 data pair at full speed (12 Mbit/s). A USB-C plug to USB-A socket adapter can also be tried with the existing cable. The USB-IF specifies that adapter for direct-attach devices, so verify the cable-plus-adapter combination on the bench. [USB Type-C specification, §3.5.3 and §3.6.1](https://www.usb.org/sites/default/files/USB%20Type-C%20Spec%20R2.0%20-%20August%202019_0.pdf)

A USB-C host socket needs USB 2.0 data in both plug orientations. It uses host pull-ups on CC1 and CC2 to detect an attached device, then switches on protected 5 V at the socket. Power switches off on unplug. Check the breakout's CC wiring and any device-side pull-downs before drawing a wiring diagram. [USB Type-C specification, §2.3 and §4.4.2](https://www.usb.org/sites/default/files/USB%20Type-C%20Spec%20R2.0%20-%20August%202019_0.pdf)

The available breakout has four pins. If the pins are `VBUS`, `GND`, `D+` and `D−`, the header gives no access to CC1 or CC2. Confirm the labels and board circuitry before selecting an adapter. The native USB-C check requires no breakout.

## Host VBUS

The hub's wall supply powers the MIDI devices. A self-powered hub may still draw up to 100 mA from the host's 5 V supply at its upstream connector. The PIO GPIOs carry data only. An adapter power feed must stay separate from the board's native USB-C 5 V rail to prevent the laptop and hub supplies from driving each other. [USB 2.0 specification, §7.2.1.2](https://www.usb.org/document-library/usb-20-specification)

The team will check for 5 V coming back from the hub and verify the hub's USB 2.0 topology on the bench. Those checks guide the adapter power feed, protection and parts list.

## Bench acceptance

The native USB-C check can start with the host adapter disconnected. Flash
the board as described in [README.md](../README.md#flash), plug native USB-C into
the computer, and check for one MIDI 1.0 port and one CDC serial port. Open CDC
and check for `host=0`. The LED should be blue. Send a MIDI note to the bridge
from the computer and check that `rx` increases while `tx` stays at zero.
Press **RESET** and verify both interfaces return.
Record the firmware commit, operating system and results.

Before wiring PIO USB, connect the hub to a computer with its stock cable and
check its USB 2.0 hub tree. On Linux, `lsusb -t` lists the hub nodes and the
devices below them. Complete the remaining checks after verifying the adapter
and its 5 V feed. Record the hub and MIDI devices used.

1. Connect two or more MIDI devices through the hub and check that CDC reports
   the expected number of MIDI interfaces. Verify the LED turns green.
2. Send notes and control changes from each input. Check channel, data bytes and
   delivery to every other logical port. Exercise both cables on a multi-cable
   device and confirm that the originating port receives no echo.
3. Send long SysEx while another source sends notes and MIDI clock. Check SysEx
   order and termination, and check that realtime messages get through.
4. Unplug and reconnect a device during traffic. Check that the other devices
   keep working and that delivery resumes without stale packets. If possible,
   saturate one output and confirm the drop counter rises while others work.
5. Repeat with the board powered from the hub and the computer disconnected.
   Run a 30-minute multi-device session and record any latency spikes, drops or
   loss of enumeration. Repeat with both orientations of the USB-C host plug.
