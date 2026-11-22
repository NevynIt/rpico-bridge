# Project state

**Prototype status — native USB and CDC bench routing verified.** Native
enumeration, diagnostics, MIDI ingress, hardware RESET recovery and CDC-only
routing have passed. The board currently runs a separate isolated hub
diagnostic build. An empty bus-powered USB-C hub completed configuration in an
isolated charger setup. Downstream routing and external-hub operation remain
unverified.
This is development firmware, not a release for distribution.

## Verification evidence

- Fresh default and CDC bench Release builds passed. Native and ASan/UBSan test
  suites each passed 2/2. Separate Release UF2 builds were byte-identical.
- ELF descriptors show CDC control/data and USB Audio Control/MIDI Streaming,
  with one embedded MIDI jack per direction and VID/PID `1209:0001`.
- Native enumeration showed all four interfaces. CDC diagnostics counted a Note
  On and Note Off sent to MIDI OUT as `rx=2`, with `host=0`. Hardware RESET
  recovered enumeration from bus 1 device 3 to device 4 with the same interfaces.
- The CDC harness passed five exact MIDI packets in each direction with no echo
  on either source. Linux bindings were restored after the test.
- CDC `BOOTSEL\n` from the published bench build entered RP2040 ROM without
  buttons. After the later LED image was flashed, `BOOTSEL\n` again entered ROM
  without buttons. It enumerated as bus 1 device 13.
- The LED image (`d94e3818…90de8`) passed the same five bidirectional MIDI
  packets with no echo and restored Linux bindings on bus 1 device 12.
  `picotool` then loaded and verified the isolated empty-hub diagnostic image
  (`dd6ea717…5004b88cf`), which re-enumerated as bus 1 device 14.
- With no physical hub attached, two reads reported
  `midi host=0 rx=0 tx=0 drops=0 invalid=0 hubs=0`. No downstream device has been tested.
- Regular firmware signals green when a host MIDI interface is mounted. The
  separate diagnostic signals green after hub configuration. That diagnostic
  green state was observed with the empty hub. Firmware also implements solid
  blue when ready with no host MIDI, amber activity on received MIDI, red on
  initialization failure and magenta for about 200 ms before CDC BOOTSEL in bench
  mode. Other updated LED states await visual verification.

## Current design and constraints

- One foreground loop owns both TinyUSB task pumps and the router. TinyUSB 0.21
  provides asynchronous host enumeration and the MIDI host packet API.
- Build limits are seven USB devices, two hub nodes and eight host MIDI
  interfaces. The attached hub's internal topology remains unverified.
- The router fans packets to destination cables and excludes the source. Bounded
  output queues can drop packets or abort a SysEx stream under congestion.
- Prototype host data pins are GPIO14/15. The available hub is bus-powered and
  its upstream cable ends in USB-C. The breakout is marked V, D−, D+, G. CC is
  unverified and no protected host supply is fitted.
- An empty bus-powered USB-C hub was configured through the four-pin breakout
  with the Pico powered by a standalone 5 V wall charger and the laptop
  disconnected. Solid green was observed. No CDC capture was made, and no
  downstream device or externally powered hub was tested. This fixed setup does
  not establish loaded standalone power or production host-port VBUS behavior.
- No host-port current limiter is available. Use a suitable VBUS arrangement
  before testing a downstream MIDI device. The production design still requires
  a PIO host connection to an externally powered hub.
- Bench firmware uses shared pid.codes test ID `1209:0001` for private in-house
  testing. It may collide with other test devices and must not be distributed or
  used on devices that are redistributed, sold or manufactured. See the
  [test PID terms](https://pid.codes/1209/0001/).
- Production identity is unallocated. Candidates are a pid.codes open-project
  request and Raspberry Pi's RP2040 silicon PID application. Neither is approved
  or reserved. See [Raspberry Pi's application](https://github.com/raspberrypi/usb-pid).

## Pinned firmware dependencies

| Dependency | Pinned revision |
| --- | --- |
| Pico SDK | 2.2.0, `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779` |
| TinyUSB | 0.21.0, SHA-256 `b7cdf35c5ccefb0f61640aff94a732ab4ebcb21498201e4a161545891b23ba01` |
| Pico-PIO-USB | 0.7.3, `b186e36bea0f3bcc2ee94ca1fa81e7f71281443a` |
| ARM GCC, picotool | 12.2.1, 2.2.0 |

## Next tasks

1. Implement and verify the built-in mouse activity test.
2. Test one downstream MIDI device with a suitable VBUS arrangement.
3. Validate the powered hub, downstream routing, reconnect, soak and loaded
   standalone power.
