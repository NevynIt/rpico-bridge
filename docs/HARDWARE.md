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

The prototype firmware uses GPIO14 (D+) and GPIO15 (D−) for PIO USB. Pico-PIO-USB recommends 22 Ω series resistors on the two data lines. The first temporary hub check used GP14 to D+, GP15 to D−, and common ground through jumper wires. This detected one empty, bus-powered USB-C hub. Whether series resistors were fitted for that check was not recorded. [Pinned Pico-PIO-USB README](https://github.com/sekigon-gonnoc/Pico-PIO-USB/blob/b186e36bea0f3bcc2ee94ca1fa81e7f71281443a/README.md)

## Hub cable and USB-C breakout

The existing hub cable is USB 3.0 Standard-A to Standard-B. A USB-C plug to USB 3 Standard-B cable is a defined cable assembly for connecting a legacy hub to a USB-C host. The bridge would use its USB 2.0 data pair at full speed (12 Mbit/s). A USB-C plug to USB-A socket adapter is defined for direct-attach devices, but USB-IF does not guarantee operation when another standard cable is added. [USB Type-C specification, §2.2 and §3.1.3](https://www.usb.org/sites/default/files/USB%20Type-C%20Spec%20R2.0%20-%20August%202019.pdf)

A four-wire USB-C breakout labelled V, D−, D+ and G was used with a small USB-C hub's upstream plug. It was sufficient to detect that hub in the tested cable orientation. This does not establish correct attach handling in both orientations or suitability with other Type-C cables. Record the breakout and cable details before using them in a general host port.

## Host VBUS

For the temporary empty-hub check, a 5 V USB wall charger powered the Pico through its native USB-C port. A jumper connected the breakout's V pin to the Pico 5 V pin. The laptop was unplugged, the hub had no external power supply, and no downstream devices were connected. Do not repeat this direct VBUS jumper setup with the laptop attached to native USB-C, since it feeds the hub from the laptop's VBUS through the board. The check does not validate load capacity, fault protection, reverse-current behavior, or powered-hub operation.

The production host port still needs a defined 5 V source, current limiting and reverse-current protection. The powered seven-port hub's upstream VBUS behavior has not been measured. Keep the host VBUS feed isolated from native USB-C VBUS when the computer is attached. [USB 2.0 specification, §7.2.1.2](https://www.usb.org/document-library/usb-20-specification)

## Bench acceptance

The native USB-C check can start with the host adapter disconnected. Flash
the board as described in [README.md](../README.md#flash), plug native USB-C into
the computer, and check for one MIDI 1.0 port and one CDC serial port. Open CDC
and check for `host=0`. The LED should be blue. Send a MIDI note to the bridge
from the computer and check that `rx` increases while `tx` stays at zero.
Press **RESET** and verify both interfaces return.
Record the firmware commit, operating system and results.

The temporary empty bus-powered hub check passed. The Pico was powered by a
5 V wall charger through native USB-C with the laptop disconnected. With the
PIO host VBUS jumper in place, the status LED changed from blue (`hubs=0`) to
green after TinyUSB completed hub setup. This confirms detection of that one
hub with the current prototype firmware in that setup.

The owner reports that two MIDI devices routed through the externally powered
seven-port hub on the previous firmware with the yellow PIO VBUS jumper
disconnected. The Pico's native USB-C power source during this trial was not
confirmed. This report does not establish the hub's upstream VBUS behavior or
load and fault protection for a production host port.

For repeatable validation, record the seven-port hub's USB 2.0 topology on a
computer with its stock cable. On Linux, `lsusb -t` lists the hub nodes and
devices below them. Record the hub, MIDI devices and power connections used.

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
