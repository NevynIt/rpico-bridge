# Project state

**Built-in documentation view.** The latest firmware and 512 KiB FAT12 drive
are flashed and verified. CONFIG.HTM contains the embedded guide; README.TXT
points to it and LICENSE.TXT carries the project license. CDC retained the
catalog and APC exclusion. IDs 3–5 were offline in this check. The user reports
the earlier browser controller worked. Direct browser connection to this build
and PIO routing were not tested.

## Physical verification

- UF2 SHA-256
  `6f30d1bfc011fcfd40ea26eacc95b84212b24cc84d0ff098b4f8de012a9b0a94`
  matched an independent staged build and passed picotool flash verification.
- Direct MSC read of all 1024 sectors matched image SHA-256
  `2874a5b409776b7a824ec3459296c7f6b51de734a0759e0c75036b88c5b1f775`.
  MODE SENSE reported read-only.
- CDC GET returned the same seven RGB values and timings twice. The catalog and
  APC exclusion `R RULE 4 1 255 3 0 255` remained unchanged. Device IDs 3–5
  were offline in this check.
- Executable firmware bytes match the previous build outside the documentation
  image. No PIO routing test was run for this update.
- Local Chromium with mocked Web Serial verified automatic LED load, manual
  reload without writes, failed initial read/retry and exact values. Direct
  browser-to-Pico and Windows drive tests remain unverified.

## Build and software verification

- Firmware and drive tests pass. Normal, bench and `RPICO_DOCS_DRIVE=OFF`
  builds succeeded; OFF was bit-identical to the prior drive-free UF2.
- The fixed FAT12 image is 512 KiB, uses `RPICOBRIDGE`, and contains
  CONFIG.HTM, README.TXT and LICENSE.TXT. Its full sector read matched the
  generated image; earlier image fsck/mtools checks passed.
- Python drive tests pass 3/3, native CTest 2/2 and 10 protocol tests pass.
  Keyboard and console interactions have stub coverage; detailed harness and
  physical key use remain pending.
- The default build requires Node.js, Python 3 and one-time `npm ci
  --prefix configurator`. It generates `build/docs_drive/rpico-bridge.img` and
  embeds it in UF2. `-DRPICO_DOCS_DRIVE=OFF` omits the image and build dependency.
- Open CONFIG.HTM from the mounted volume in desktop Chrome or Edge. Allow Web
  Serial and Web MIDI access. The app runs offline without a server. Save route
  and LED settings through CDC; the drive is read-only, not settings storage.

## Next checks

1. Verify live browser serial/MIDI connection, keyboard and console behavior on
   the updated image. Check Windows drive mounting.
2. Test audible multi-device routing, reconnect and soak under traffic.
3. Verify loaded standalone power with the chosen hub and supply.

## Constraints

- Ordered routing preserves channels and selects destination cables. Fallback
  is all or drop; global action 3 excludes one remembered cable both ways.
- Host wiring uses GPIO14/15 and a four-pin breakout labeled V, D−, D+ and G.
  Pin mapping, CC behavior, production host VBUS, protection and loaded power
  remain subject to hardware verification. The board announces 100 mA.
- Shared pid.codes ID `1209:0001` is for private in-house tests, may collide,
  and must not be distributed on firmware or devices. Production identity is
  unallocated.

| Dependency | Pinned revision |
| --- | --- |
| Pico SDK | 2.2.0, `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779` |
| TinyUSB | 0.21.0, SHA-256 `b7cdf35c5ccefb0f61640aff94a732ab4ebcb21498201e4a161545891b23ba01` |
| Pico-PIO-USB | 0.7.3, `b186e36bea0f3bcc2ee94ca1fa81e7f71281443a` |
| ARM GCC, picotool | 12.2.1, 2.2.0 |
| littlefs | 2.11.3, `6cb4e86540eca0d9ba62500a298385c9d863c8be` |
