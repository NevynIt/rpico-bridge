# Project state

**Built-in documentation view.** Current firmware is flashed and verified on
the Pico. Its 512 KiB FAT12 volume includes CONFIG.HTM with embedded guide
content, README.TXT and LICENSE.TXT. CDC reports two online MIDI devices and
the saved APC cable exclusion. The browser's earlier controller build worked
per user report; physical keyboard use on this update is unverified.

## Physical verification

- Current UF2 SHA-256
  `0750b25e0020ac8f365104659ee307897063a005b2f6cf3bbf553a9938b093e1`
  reproduced from checkout and passed picotool flash verification.
- Direct MSC read of all 1024 sectors matched the 512 KiB image SHA-256
  `9b58f7a679007b723c1eb866d66afea3ec21f039e82bc8f163d972d9a7c4b29f`.
  MODE SENSE reports read-only.
- CDC R GET passed on the updated board with two MIDI devices online. Names,
  IDs and saved APC rule `R RULE 4 1 255 3 0 255` were retained.
- Chromium rendered CONFIG.HTM from a local file and showed the controls.
  Live browser connection to the board and Windows drive mounting remain
  unverified. No physical key-playing test was reported for this update.

## Build and software verification

- Firmware and drive tests pass. The prior normal, bench and `RPICO_DOCS_DRIVE=OFF`
  builds succeeded; OFF was bit-identical to the prior drive-free UF2.
- The fixed FAT12 image is 512 KiB, uses `RPICOBRIDGE`, and contains
  CONFIG.HTM, README.TXT and LICENSE.TXT. Its full sector read matched the
  generated image; earlier image fsck/mtools checks passed.
- Python drive tests pass 3/3, native CTest 2/2, and the existing 10 protocol
  tests pass. Local-file browser checks rendered controls. Keyboard and console
  interactions have stub coverage; detailed harness/physical key use is pending.
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
