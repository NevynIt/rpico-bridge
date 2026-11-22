# Project state

**Built-in documentation drive.** The regular firmware's 512 KiB FAT12 volume
was enumerated and read-only behavior checked on a Linux host. CDC and USB MIDI
also enumerated. The configurator has not yet connected to the board from the
mounted volume, and audible live routing has not been tested in this session.

## Physical verification

- UF2 SHA-256
  `2660d9ad9f910563fcce85fc7c104c330141ec452a1d91b175b2c7d0e431cc10`
  matched the staged artifact and passed picotool flash verification. Linux
  recognized a 512 KiB removable disk with CDC ACM, USB MIDI and usb-storage
  kernel bindings.
- All 1024 drive sectors matched generated-image SHA-256
  `53c1a2e8ff993ff03e419437b405a889ce36de9d47bc27ca89a588be63859ac9`.
  MODE SENSE reported write protection. WRITE10 returned DATA PROTECT, and a
  free sector was unchanged.
- CDC `R GET` succeeded while MSC was mounted. The current and previous image
  reported `host=2`, MicroFreak and APC online, Circuit Tracks offline, and the
  saved APC control-cable rule `R RULE 4 1 255 3 0 255`.
- A fresh USB/IP software connection restored all standard device classes.
  Chromium rendered `CONFIG.HTM` extracted from the drive and exposed Web Serial
  and Web MIDI APIs. There was no live browser-to-board session, Windows mounted
  drive test or audible live routing test.

## Build and software verification

- Normal, CDC bench and `RPICO_DOCS_DRIVE=OFF` Release builds pass. The OFF UF2
  is bit-for-bit identical to the previous drive-free build.
- The deterministic 512 KiB FAT12 image passes fsck and mtools checks. Its used
  clusters fit the format limit and the embedded image stays below the reserved
  settings flash area. It contains `CONFIG.HTM`, `README.TXT`, `HARDWARE.MD` and
  `LICENSE.TXT` under volume label `RPICOBRIDGE`.
- Three Python drive-image tests, 10 configurator Node tests and native CTest
  2/2 pass. Browser tests cover the frontend, but not a live device session.
- The default build requires Node.js, Python 3 and one-time `npm ci
  --prefix configurator`. It creates `build/docs_drive/rpico-bridge.img` and
  embeds it in UF2. `-DRPICO_DOCS_DRIVE=OFF` omits the image and build dependency.
- Open `CONFIG.HTM` directly from the mounted volume in desktop Chrome or Edge.
  Allow Web Serial and Web MIDI access. The app runs offline without a server.
  Save routing and LED settings through CDC; the drive is read-only, not settings
  storage.

## Next checks

1. Connect the browser configurator to the board from the mounted drive and
   verify route and LED read/write.
2. Test the volume mount and permissions on Windows.
3. Verify audible multi-device routing and reconnect under live traffic.

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
