# Project state

**Milestone 2 — USB architecture.** The foundation, five agent definitions and
development container are on the public `nevynit/rpico-bridge` repository.

## Build evidence

- The Release build produces `build/rpico_bridge.uf2`. `picotool info -a`
  reports RP2040, `waveshare_rp2040_zero` and SDK 2.2.0. This image starts
  the SDK and leaves GPIO and application USB idle. Two build directories with
  different `SOURCE_DATE_EPOCH` values produced byte-identical UF2 files.
- Container versions used for the build and compatibility checks:

  | Dependency | Version | Commit |
  | --- | --- | --- |
  | Pico SDK | 2.2.0 | `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779` |
  | SDK TinyUSB | 0.18.0 | `86ad6e56c1700e85f1c5678607a762cfe3aa2f47` |
  | Pico-PIO-USB | 0.7.3 | `b186e36bea0f3bcc2ee94ca1fa81e7f71281443a` |

  ARM GCC 12.2.1, CMake 3.25.1, Ninja 1.11.1 and picotool 2.2.0.
- The installed Pico-PIO-USB `host_hid_to_device_cdc` example compiles and
  links for this board with the exact pins above. It covers native CDC device,
  PIO HID host and TinyUSB hub support at build time. TinyUSB's `midi_test`
  device example also builds for the board.
- Three GPT-6 Luna Medium specialists completed hardware, USB and testing
  research. Five TOML definitions match the Codex 0.171.0 schema. The local catalog
  includes both requested models and efforts. Launching the project-scoped files
  in the installed CLI is pending because session storage is read-only.

## Decisions and open items

- Use native USB-C as device and PIO USB as host. The owner plans to power the
  board via a power-only port on the existing seven-data-port hub, or via a laptop
  for testing. A photo or description of the computer end of the hub's data
  cable will identify the adapter connector. The hardware team still needs to
  verify upstream VBUS, backfeed and USB 2.0 hub topology on the bench.
- The SDK's TinyUSB pin has a MIDI device class but lacks a MIDI host class.
  Evaluate [TinyUSB 0.19.0](https://github.com/hathach/tinyusb/releases/tag/0.19.0)
  or a later pin with the upstream MIDI 1.0 host driver before writing a driver.
  Recheck SDK/Pico-PIO-USB builds, hub limits and reconnects.
- Prefer USB MIDI event packets and distinct logical ports per interface and
  cable. Define SysEx serialization when multiple sources share an output and
  exercise stalled-output overflow, disconnect and address reuse in native tests.
- Open acceptance items include native MIDI + CDC enumeration, dual USB operation,
  multi-device hub enumeration, routing and latency, reconnect, standalone power
  and soak on actual hardware. The container has no USB device node. Wokwi's
  partial Pico USB simulation cannot establish PIO-host or hub operation.

## Next three tasks

1. Build the native MIDI 1.0 + CDC interface and verify a pinned MIDI host
   dependency with the PIO host. Confirm the hub's topology and safe VBUS feed.
2. Implement bounded, transport-independent routing and native tests for fan-out,
   no self-echo, virtual cables, SysEx/realtime, backpressure and reconnect.
3. Verify the native interface, powered hub enumeration and MIDI transport on
   the RP2040-Zero.
