# Project state

**Prototype status — native USB and CDC bench routing verified.** Native
enumeration, CDC diagnostics, MIDI ingress, hardware RESET recovery and the
CDC-only routing harness have passed. The bench build uses no physical PIO host.
Hub operation and standalone power remain unverified. This is development
firmware, not a release for distribution.

## Verification evidence

- Native Release tests passed 2/2. AddressSanitizer and UndefinedBehaviorSanitizer
  passed with LeakSanitizer disabled. Release UF2 builds from separate directories
  were byte-identical.
- ELF descriptors show CDC control/data and USB Audio Control/MIDI Streaming.
  MIDI Streaming has one embedded jack per direction and VID/PID `1209:0001`.
- Approved `picotool` identified RP2040 revision B2 in BOOTSEL, loaded the bench
  UF2, verified flash and requested reboot. Physical BOOT/RESET entered BOOTSEL
  once for this load. The board re-enumerated with product `USB MIDI 1.0 Bridge`.
- With only native USB-C attached, elevated `lsusb` showed `1209:0001`, all four
  CDC/MIDI interfaces and the expected classes. The owner confirmed the onboard
  LED was blue.
- Native CDC DTR/read returned `midi host=0 rx=0 tx=0 drops=0 invalid=0`. A Note
  On and matching Note Off sent to native MIDI OUT each transferred four bytes.
  CDC then reported `midi host=0 rx=2 tx=0 drops=0 invalid=0`.
- Hardware RESET briefly removed the device from sysfs. It re-enumerated from
  bus 1 device 3 to device 4 with the same product, VID/PID and four interface
  numbers and classes. The owner reported the LED seemed uninterrupted.
- `python3 tests/cdc_bench.py --run --bus <BUS> --address <DEVICE>` passed five
  USB-MIDI packets in each direction. The set covered Note On/Off, SysEx
  start/end and realtime clock. Neither source received an echo. The harness
  restored Linux driver bindings. DTR diagnostics and packet lines can interleave.
- The CDC bench option defaults OFF and ran without a physical PIO host. `BOOTSEL`
  entry is compiled in that mode but has not been exercised over CDC. The bench
  result does not verify routing to physical host devices.

## Current design and constraints

- One foreground loop owns both TinyUSB task pumps and the router. TinyUSB 0.21
  provides asynchronous host enumeration and the MIDI host packet API.
- Build limits are seven USB devices, two hub nodes and eight host MIDI
  interfaces. The attached hub's internal topology is unverified.
- The router fans packets to destination cables and excludes the source. Bounded
  output queues can drop packets or abort a SysEx stream under congestion.
- Prototype host data pins are GPIO14/15. Breakout pin labels, host adapter,
  protected host 5 V feed, backfeed behavior and standalone power need validation.
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

1. Identify breakout pinout and confirm host wiring and 5 V power design.
2. Verify PIO host enumeration with the hub and MIDI devices.
3. Test downstream routing, reconnect and soak behavior, then standalone power,
   using [HARDWARE.md](HARDWARE.md).
