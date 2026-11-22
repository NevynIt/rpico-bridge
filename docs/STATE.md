# Project state

**Prototype status — native USB and OS MIDI integration prepared.** The Pico
currently runs default UF2 `e288af09…aa9fb` with no hub attached. Physical native
MIDI, RESET recovery, CDC routing and one HID mouse receiver have been checked.
Live OS MIDI application testing and downstream MIDI remain pending.

## Verification evidence

- Normal and bench Release builds were warning-free. Native CTest passed 2/2.
  Earlier clean-clone UF2 hashes matched. ASan/UBSan Debug CTest passed 2/2 with
  LeakSanitizer disabled.
- Final default UF2 was flashed, verified and enumerated as `1209:0001` on bus 1
  device 24 after CDC BOOTSEL. The physical MIDI harness passed five exact packets
  each way without echo and restored Linux bindings.
- Normal and bench images provide the CDC test peer after eight physical PIO host
  endpoint slots. With DTR asserted, `M <8 hex digits>\n` injects and monitors
  cable-zero packets through the router, without self-echo or changing `host=`.
  Bench mode disables physical PIO USB MIDI/HID. The peer is one merged port.
- `BOOTSEL\n` is available in every image after asserted DTR. It indicates
  magenta for about 200 ms before ROM. The default image entered ROM by software
  and re-enumerated after reflashing.
- `tools/midi_log.py` uses python-rtmidi 1.5.8. Fake-backend tests passed for
  product matching, SysEx, F8/FE, monitor/file output, sends, Note Off, no loopback
  and cleanup on errors. No live OS MIDI app ran because the container lacks ALSA
  sequencer support. Windows and macOS have not been tested.
- The user confirmed green idle and cyan during mouse movement on a temporary
  image with one HID receiver through the small hub. The Pico used an isolated
  wall charger with laptop disconnected. This proves HID input from one device.
  No downstream MIDI device or externally powered hub has been tested.
- Installed LED behavior is gray ready, blue after the hub accepts
  `SET_CONFIGURATION`,
  green for host MIDI, cyan for recognized mouse reports, amber for MIDI activity
  (250 ms half-period for a 1.5 s window), magenta before BOOTSEL and red on fault.
  Blue confirms configuration acceptance, not class readiness. Gray and slower
  amber were not visually confirmed. User found rapid colors hard to distinguish.
- ELF descriptors show CDC control/data, USB Audio Control/MIDI Streaming, one
  embedded jack per direction and VID/PID `1209:0001`. Native USB announces 100 mA.

## Current design and constraints

- One foreground loop owns both TinyUSB task pumps and the router. TinyUSB 0.21
  supports asynchronous host enumeration and MIDI host packets.
- Limits are seven USB devices, two hub nodes and eight host MIDI interfaces.
  The CDC peer is appended after those eight slots and excluded from `host=`.
- Routing excludes the source and uses bounded queues. Congestion can drop
  packets or abort a SysEx stream. HID mouse reports are observe-only, with no
  native pointer routing or controls.
- Host pins are GPIO14/15. The available hub is bus-powered with a USB-C upstream
  plug. Breakout labels are V, D−, D+, G. CC and protected host supply are
  unverified. No host-port current limiter is available.
- The empty-hub charger test did not measure combined current draw. No laptop-
  powered experimental cascade or loaded standalone-power test has run. The
  production design requires PIO host operation with an externally powered hub.
- Bench firmware uses shared pid.codes ID `1209:0001` for private in-house tests.
  It may collide with other test devices and must not be distributed or used on
  devices that are redistributed, sold or manufactured. See the
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

1. Run the OS MIDI tool with the live bridge and exercise both directions.
2. Test one downstream MIDI device with a suitable VBUS arrangement.
3. Validate powered-hub routing, reconnect, soak and loaded standalone power.
