# Project state

**Prototype status — native USB and CDC routing verified.** Native enumeration,
CDC diagnostics, MIDI ingress, RESET recovery and the CDC test peer passed. The
Pico currently runs the final regular image with the hub disconnected. Physical
downstream MIDI and powered-hub operation remain unverified. This is development
firmware, not a release for distribution.

## Verification evidence

- Default, bench and isolated Release images built without warnings. Native and
  ASan/UBSan tests passed 2/2. Mocked three-peer integration covered CDC, native
  USB and the final PIO host slot.
- ELF descriptors show CDC control/data and USB Audio Control/MIDI Streaming,
  one embedded jack per direction, VID/PID `1209:0001` and 100 mA native power.
- Final regular UF2 (`c04630c9…554181`) verified and enumerated as bus 1 device
  20. `BOOTSEL\n` entered ROM at device 21. The same UF2 reflashed and verified,
  then application mode returned at device 22. No button reset was needed.
- On the final regular image with the hub disconnected, the physical MIDI test
  passed five exact packets in each direction without echo. Linux bindings were
  restored. The PIO-enabled mouse/diagnostic image also entered ROM over CDC.
- Normal and bench images provide a CDC peer after eight real PIO host slots.
  With DTR asserted, `M <8 hex digits>\n` injects and monitors cable-zero packets
  through the router without self-echo or changing `host=`. Bench mode disables
  physical PIO USB MIDI/HID. `BOOTSEL\n` is available in every image, with about
  200 ms magenta before ROM entry.
- The temporary diagnostic image reported `hubs=0` twice with no hub attached.
  On the temporary mouse image (`930b0730…`), the user saw green idle and cyan
  during mouse movement with one HID receiver through the small hub and charger.
  This confirms physical HID input from one device. No downstream MIDI device or
  externally powered hub was tested.
- Regular firmware uses green for a mounted MIDI interface and cyan for mouse
  reports. The diagnostic uses green after hub configuration and cyan on mouse
  reports. The user saw blue and activity flashes on regular firmware but found
  rapid colors hard to distinguish. HID handling observes reports only. It does
  not route native pointer input or add controls.

## Current design and constraints

- One foreground loop owns both TinyUSB task pumps and the router. TinyUSB 0.21
  provides asynchronous host enumeration and the MIDI host packet API.
- Limits are seven USB devices, two hub nodes and eight host MIDI interfaces.
  The hub's full topology is not verified.
- Routing excludes the source and uses bounded output queues. Congestion can
  drop packets or abort a SysEx stream. Native pointer routing is not implemented.
- Host data pins are GPIO14/15. The available hub is bus-powered with a USB-C
  upstream plug. Breakout labels are V, D−, D+, G. CC and protected host supply
  are unverified. No host-port current limiter is available.
- USB descriptors announce 100 mA. Combined wall-charger draw is unmeasured. No
  laptop-powered experimental hub cascade has been tested. The empty-hub charger
  check does not validate production power with an externally powered hub or
  loaded standalone operation.
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

1. Test one downstream MIDI device with a suitable VBUS arrangement.
2. Validate the externally powered hub, routing and reconnect behavior.
3. Run soak and loaded standalone-power checks using [HARDWARE.md](HARDWARE.md).
