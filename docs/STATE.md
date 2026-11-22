# Project state

**Prototype status.** Regular littlefs/LED firmware is flashed and verified on the
Pico. CDC checks confirmed migration of the saved routing policy and device
catalog, LED save/readback, and controlled restart. `host=0` throughout these
checks. A PIO root was reported during the save-triggered watchdog restart, but
physical hub and instrument attachment were not confirmed. Visual palette,
connected instrument routing and audible output for this image remain unverified.

## Verification

- Regular UF2 SHA-256
  `69c4c94741fea452041af3b0b6e9478f668155008b5d249c96bba4b1e033834e` matched
  the clean-stage artifact and passed picotool flash verification. Bench UF2
  hash begins `afc27ecf` and ends `6c4`.
- On-board CDC showed migrated rule `R RULE 4 1 255 3 0 255` and catalog
  entries ID 3 Arturia MicroFreak, ID 4 APC Key 25 mk2 and ID 5 Circuit Tracks.
  `help led`, LED show/stage/abort and rejection of a 1 ms pulse passed.
- Changing pulse to 9 ms and ready color returned a durable ACK, restarted the
  board and persisted on readback. Restoring defaults returned an ACK and
  restarted the board. Readback showed 8 ms and ready `0C0C0C`; the same policy
  and catalog remained present. Both A/B images reported `host=0` and catalog
  IDs 3–5 offline, with the saved APC rule present. No physical MIDI interfaces
  were mounted. Physical hub and instrument attachment were not confirmed.
- Normal and CDC bench Release firmware builds are warning-free. Native Release
  CTest passes 2/2. Native flash mocks simulated v1 migration, failed-write
  rollback and interrupted format.
- Configurator has 10 Node tests and JSDOM Web Serial integration coverage for
  LED load/save and routing. No live browser-to-board session has passed. No live
  OS MIDI logger test ran because the container lacks ALSA sequencer support.
- Default palette is ready `0C0C0C`, hub `000812`, MIDI interface `000300`,
  mouse `001212`, traffic `120A00`, BOOTSEL `120012`, error `0C0000`. Traffic
  pulse defaults to 8 ms (allowed 2–100); mouse phase/hold default to 250/1500
  ms. Current-image LED colors have not been visually confirmed.
- LittleFS 2.11.3 is pinned at
  `6cb4e86540eca0d9ba62500a298385c9d863c8be`. It stores LED settings, policy
  and catalog. New-filesystem initialization migrates legacy policy/catalog.
  Flash writes pause routing, cancel the PIO frame timer, use SDK
  `flash_safe_execute` and verify data before ACK. With a PIO root, the board
  restarts after saves and host devices must enumerate again.
- ELF descriptors expose CDC control/data and USB Audio Control/MIDI Streaming,
  with one embedded MIDI jack each direction. The board announces 100 mA.
  Production hub power, protection and loaded standalone operation remain
  unverified.

## Next checks

1. Visually check amber MIDI pulse, green host MIDI state and routing across
   multiple cables with attached instruments.
2. Physically reconnect devices and verify rediscovery, routing and soak behavior.
3. Verify standalone power with the chosen supply and loaded hub/device setup.

## Constraints and pinned dependencies

- Ordered routing preserves MIDI channels, selects destination cables and uses
  all/drop fallback. Global action 3 excludes one remembered cable both ways
  before ordered rules.
- Host wiring uses GPIO14/15 and a four-pin breakout labeled V, D−, D+ and G.
  Pin mapping, CC behavior, production host VBUS, protection and loaded power
  limits remain subject to hardware verification.
- Shared pid.codes ID `1209:0001` is for private in-house tests, may collide,
  and must not be distributed on firmware or devices. Production identity is
  unallocated; candidate paths are pid.codes and Raspberry Pi silicon PID.

| Dependency | Pinned revision |
| --- | --- |
| Pico SDK | 2.2.0, `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779` |
| TinyUSB | 0.21.0, SHA-256 `b7cdf35c5ccefb0f61640aff94a732ab4ebcb21498201e4a161545891b23ba01` |
| Pico-PIO-USB | 0.7.3, `b186e36bea0f3bcc2ee94ca1fa81e7f71281443a` |
| ARM GCC, picotool | 12.2.1, 2.2.0 |
| littlefs | 2.11.3, `6cb4e86540eca0d9ba62500a298385c9d863c8be` |
