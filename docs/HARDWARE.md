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

The prototype firmware uses GPIO14 (D+) and GPIO15 (D−) for PIO USB. As a reference only, Pico-PIO-USB recommends 22 Ω series resistors on the two data lines. A later adapter would connect GP14 through 22 Ω to D+, GP15 through 22 Ω to D−, and board ground to the host cable's signal ground. The pin pair, resistors and adapter remain unverified on hardware. [Pinned Pico-PIO-USB README](https://github.com/sekigon-gonnoc/Pico-PIO-USB/blob/b186e36bea0f3bcc2ee94ca1fa81e7f71281443a/README.md)

## Hub cable and USB-C breakout

The existing hub cable is USB 3.0 Standard-A to Standard-B. A USB-C plug to USB 3 Standard-B cable is a defined cable assembly for connecting a legacy hub to a USB-C host. The bridge would use its USB 2.0 data pair at full speed (12 Mbit/s). A USB-C plug to USB-A socket adapter is defined for direct-attach devices, but USB-IF does not guarantee operation when another standard cable is added. [USB Type-C specification, §2.2 and §3.1.3](https://www.usb.org/sites/default/files/USB%20Type-C%20Spec%20R2.0%20-%20August%202019.pdf)

A USB-C host receptacle needs orientation-independent USB 2.0 D+/D− wiring and Rp terminations on CC1 and CC2 with attach detection. Keep 5 V VBUS off until a sink is detected, then turn it off on detach. Select a known 5 V source and a current-limited switch with reverse-current blocking. Select components after confirming the breakout and 5 V source. [USB Type-C specification, §2.1, §4.4.2 and §4.5.2](https://www.usb.org/sites/default/files/USB%20Type-C%20Spec%20R2.0%20-%20August%202019.pdf)

The reported breakout has four pins, but its labels and circuitry are unknown. Pin count alone does not show whether CC1/CC2 have source terminations or attach control, whether duplicate D pins support both plug orientations, or what protection is present. Before drawing a wiring diagram, inspect the unpowered breakout's label, part number, both sides and available schematic. Map VBUS, ground, D+/D− and CC circuitry without connecting power. If the four pins are only VBUS, ground, D+ and D− and CC handling is absent, the breakout alone is not a USB-C host receptacle. If CC handling cannot be verified, leave the module unpowered and review host options. The native USB-C check needs no breakout.

## Host VBUS

The hub's wall supply powers the MIDI devices. A self-powered hub may still draw up to 100 mA from the host's 5 V supply at its upstream connector. The PIO GPIOs carry data only. An adapter power feed must stay separate from the board's native USB-C 5 V rail to prevent the laptop and hub supplies from driving each other. [USB 2.0 specification, §7.2.1.2](https://www.usb.org/document-library/usb-20-specification)

The 5 V source, adapter current limit, reverse-current protection and hub upstream VBUS behavior are open decisions. Unpowered inspection cannot establish whether a powered hub sources voltage on its upstream VBUS or how much current the proposed source can provide. Keep the adapter disconnected from the RP2040-Zero until those points are established independently. Then confirm the hub's USB 2.0 topology and VBUS behavior on the bench before finalizing wiring or parts. Do not connect the host-port 5 V rail to the board's native USB-C 5 V rail.

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
