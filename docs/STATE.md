# Project state

**Prototype status — native USB runtime remains unverified.** Native routing
checks and compiled firmware evidence are recorded below. BOOTSEL enumeration
was observed through WSL 2 USB/IP, but application-mode USB enumeration and
physical MIDI+CDC operation were not observed. Host adapter wiring and full hub
bench operation also remain unverified. This is development firmware, not a
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
- Via USB/IP, WSL 2 sysfs enumerated the RP2040 as `2e8a:0003` (`RP2 Boot`) in
  BOOTSEL mode. The Dev Container has neither `/dev/bus/usb` nor an `RPI-RP2`
  mount. Escalated `picotool` could not open the device.
- During a 60-second observation, the USB specialist did not see application
  identity `1209:0001` or its interfaces. The BOOTSEL observation confirms only
  bootloader enumeration. Compiled descriptor checks do not establish runtime
  application enumeration. Application-mode enumeration and native MIDI+CDC
  operation remain unverified.

## Current design and constraints

- One foreground loop owns the router and both TinyUSB task pumps. TinyUSB 0.21
  provides asynchronous host enumeration and the host MIDI packet API.
- Build-time limits are seven USB devices, two hub nodes and eight host MIDI
  interfaces. The attached hub's internal topology has not been verified.
- Each advertised destination cable receives the original CIN and MIDI bytes,
  tagged with its local cable number. The source logical cable is excluded.
  Output queues are bounded; congestion may drop packets or abort a SysEx stream
  on the affected output while other outputs continue.
- Prototype host data pins are GPIO14/15. The first physical test should use
  only the board and a computer to check native USB; it does not depend on the
  host breakout. The hardware owner reports a four-pin breakout, but exact pin
  labels are unconfirmed. The host adapter and full powered-hub bench have not
  been verified.
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

1. When hardware is available, test native USB with only the board and a
   computer; record enumeration, blue LED, `host=0` and expected RX counter.
2. Identify breakout pin labels and confirm the host adapter and power design.
3. Validate hub operation with multiple devices and standalone power using the
   acceptance steps in [HARDWARE.md](HARDWARE.md).
