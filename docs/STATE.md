# Project state

**Prototype status — native USB and MIDI ingress verified.** The board
enumerated over native USB-C, CDC diagnostics responded, and MIDI ingress was
confirmed with no host destinations. The onboard LED was blue. Forwarding, the
PIO host adapter and standalone power remain unverified. Hardware RESET
recovery of native enumeration is verified. This is development firmware, not a
release for distribution.

## Verification evidence

- Native Release test suite: 2/2 tests passed.
- Native AddressSanitizer and UndefinedBehaviorSanitizer run passed; LeakSanitizer
  was disabled.
- Release UF2 builds from two separate build directories were byte-identical.
- Independent ELF descriptor verification found four interfaces: CDC control
  and data, plus USB Audio Control and MIDI Streaming. MIDI Streaming advertises
  one cable in each direction. The rebuilt image's descriptors use VID/PID
  `1209:0001`.
- WSL 2 USB/IP and sysfs first enumerated BOOTSEL as `2e8a:0003` (`RP2 Boot`).
  Approved `picotool` identified RP2040 revision B2, loaded the bench UF2
  (`e4cfb88f15362c98bb20d90decc4d8a23b81702497a3a49e7bba3570f78dc045`),
  verified flash OK and requested an application reboot.
- Approved elevated `lsusb` later showed application PID `1209:0001` with
  product `USB MIDI 1.0 Bridge`. It enumerated CDC ACM control/data and USB Audio
  Control/MIDI Streaming interfaces, with one embedded MIDI jack per direction.
- With only native USB-C connected, CDC DTR and reads returned
  `midi host=0 rx=0 tx=0 drops=0 invalid=0`. A Note On and matching Note Off
  sent to native MIDI OUT each transferred 4 bytes. CDC then reported
  `midi host=0 rx=2 tx=0 drops=0 invalid=0`. This verifies CDC diagnostics and
  MIDI ingress with no host destinations. MIDI forwarding remains unverified.
  Linux drivers rebound after DTR cleared. The owner confirmed the onboard LED
  was blue, then pressed RESET. Sysfs briefly lost the device and saw it
  re-enumerate as bus 1 device 4, with the same product, VID/PID and four CDC/MIDI
  interface numbers and classes as bus 1 device 3. This verifies hardware RESET
  recovery of native USB enumeration. The owner reported the LED seemed
  uninterrupted.

## Current design and constraints

- One foreground loop owns the router and both TinyUSB task pumps. TinyUSB 0.21
  provides asynchronous host enumeration and the host MIDI packet API.
- Build-time limits are seven USB devices, two hub nodes and eight host MIDI
  interfaces. The attached hub's internal topology has not been verified.
- Each advertised destination cable receives the original CIN and MIDI bytes,
  tagged with its local cable number. The source logical cable is excluded.
  Output queues are bounded; congestion may drop packets or abort a SysEx stream
  on the affected output while other outputs continue.
- Prototype host data pins are GPIO14/15. The hardware owner reports a four-pin
  breakout, but exact pin labels are unconfirmed. The host adapter and full
  powered-hub bench have not been verified.
- Board power is intended to come from native USB-C, supplied by a computer or
  an externally powered hub. USB-C breakout pinout, CC configuration, protected
  host 5 V feed and backfeed behavior remain open hardware checks.
- Bench builds use the shared pid.codes test identity `1209:0001`. Anyone may
  use it in their own private, in-house tests; it may collide with other test
  devices and must not be used in distributed firmware or on devices that are
  redistributed, sold or manufactured. See the
  [pid.codes test PID terms](https://pid.codes/1209/0001/).
- A production identity is not allocated. Candidates for future review are a
  pid.codes open-project PID request and Raspberry Pi's RP2040 silicon PID
  application; neither is approved or reserved. See
  [Raspberry Pi's application](https://github.com/raspberrypi/usb-pid).

## Pinned firmware dependencies

| Dependency | Pinned revision |
| --- | --- |
| Pico SDK | 2.2.0, `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779` |
| TinyUSB | 0.21.0, archive SHA-256 `b7cdf35c5ccefb0f61640aff94a732ab4ebcb21498201e4a161545891b23ba01` |
| Pico-PIO-USB | 0.7.3, `b186e36bea0f3bcc2ee94ca1fa81e7f71281443a` |
| ARM GCC, picotool | 12.2.1, 2.2.0 |

## Next tasks

1. Identify breakout pin labels and confirm host wiring and 5 V power design.
2. Verify PIO host enumeration with the hub and MIDI devices.
3. Test downstream routing, reconnect and soak behavior, plus standalone power,
   using the acceptance steps in [HARDWARE.md](HARDWARE.md).
