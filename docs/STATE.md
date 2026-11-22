# Project state

**Prototype status.** Current regular UF2 `8aedbee5…580e` enumerates as
`1209:0001`, bus 1 device 42, with the hub, MicroFreak and APC Key attached.
Saved policy is default ALL plus a global exclusion of APC Key ID 4 cable 1.
Final audible key/pad confirmation is still pending.

## Verification

- Native Release tests pass 2/2, including exclusion tests. Earlier normal and
  CDC bench Release builds were warning-free; ASan/UBSan also passed 2/2 with
  LeakSanitizer disabled.
- Current UF2 was installed and verified. Attached-hub ALL-policy save and
  restore both returned durable acknowledgements and rebooted. Enumeration went
  through bus 1 devices 36, 37 and 38. Device names and IDs persisted.
- Physical console checks passed for DTR welcome, `help` with CR, `help rule`
  with CRLF, `devices`, same-session `R GET` and malformed-rule rollback.
  A temporary default-drop policy routed channel 1 from CDC to native cable 0
  without echo and dropped channel 2. It survived software BOOTSEL and reboot.
  The original default-all/no-rules policy was restored durably.
- The owner reports MicroFreak (one cable pair) and APC Key 25 mk2 (two pairs)
  appeared by name and remained catalogued across reboot. APC-to-MicroFreak
  routing passed after reboot on the earlier firmware. The APC keybed/sustain is
  cable 0 and controls are cable 1 per [Akai's protocol, v1.1](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf).
- Current saved exclusion is `R RULE 4 1 255 3 0 255`. It globally excludes
  APC Key cable 1 as source and destination while leaving cable 0 available.
  Future routing edits use configuration; they do not need firmware updates.
- SDK flash writes pause routing and cancel the application-owned PIO frame
  timer before `flash_safe_execute`, which masks interrupts and coordinates
  flash-safe execution across cores. Readback is checked before commit ACK.
  See [Pico SDK flash safety](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#pico_flash).
  A prior automatic catalog save correlated with a host stall; causation was
  not established. Attached-hub save/restart now passed HIL.
- `configurator/dist/index.html` is a self-contained build of about 217 KB.
  Seven Node tests and JSDOM integration pass. A full-bundle headless Chromium
  run crashed; a file-origin API probe found browser APIs, but no live
  browser-to-board session has passed.
- Earlier CDC MIDI harness testing passed five exact packets each way without
  echo and restored Linux bindings. `tools/midi_log.py` fake-backend tests pass.
  No live OS MIDI app was run because the container lacks ALSA sequencer support.
- One HID receiver produced mouse reports through the small hub on a temporary
  image. Empty-hub green was observed with the laptop disconnected. Combined
  charger draw and loaded standalone power remain unverified.
- ELF descriptors expose CDC control/data and USB Audio Control/MIDI Streaming,
  one embedded MIDI jack per direction. The device announces 100 mA.

## Routing and constraints

- Up to 24 ordered rules match source device, cable and channel. Actions route to
  all, drop or one destination/cable. MIDI channel values remain unchanged.
  Action 3 globally excludes one exact remembered device cable in both
  directions before ordered rules. It cannot be overridden by rule order.
- `tools/midi_routes.py` supports `ports`, `devices`, `show` and `apply FILE`,
  including `action: "exclude"`. The React configurator uses Web Serial and
  Web MIDI. Open its standalone HTML directly in compatible Chromium first;
  use HTTPS or localhost if file-origin permissions fail.
- PIO supports seven devices, two hub nodes and eight MIDI interfaces. HID is
  observe-only. Host pins are GPIO14/15. Breakout labels are V, D−, D+, G;
  CC, protected host supply, production VBUS behavior and fault protection
  remain unverified. The reported APC trial used the externally powered
  seven-port hub with the yellow PIO VBUS jumper disconnected; Pico power
  source during that earlier trial was unconfirmed.
- Physical unplug/replug name rediscovery and final audible key/pad confirmation
  remain pending. Continue routing, reconnect, soak and loaded power checks.
- Shared pid.codes ID `1209:0001` is for private in-house tests, may collide,
  and must not be used on redistributed, sold or manufactured devices. The
  production identity is unallocated; candidate paths are a pid.codes open
  project request and Raspberry Pi's RP2040 silicon PID application.

## Pinned firmware dependencies

| Dependency | Pinned revision |
| --- | --- |
| Pico SDK | 2.2.0, `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779` |
| TinyUSB | 0.21.0, SHA-256 `b7cdf35c5ccefb0f61640aff94a732ab4ebcb21498201e4a161545891b23ba01` |
| Pico-PIO-USB | 0.7.3, `b186e36bea0f3bcc2ee94ca1fa81e7f71281443a` |
| ARM GCC, picotool | 12.2.1, 2.2.0 |
