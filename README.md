# rpico-bridge

A USB MIDI 1.0 bridge for the Waveshare RP2040-Zero. The firmware includes
native MIDI and CDC interfaces, a PIO USB host, ordered routing rules and a
local configurator. Its ELF descriptors expose CDC control/data and USB Audio
Control/MIDI Streaming interfaces, with one MIDI cable in each direction, plus
a read-only USB documentation drive.
Earlier firmware passed native USB bench checks. The user reports two-device
routing on an earlier image. Production hub wiring and power still need
verification. See
[project state](docs/STATE.md) for evidence and [hardware](docs/HARDWARE.md) for
wiring assumptions and bench steps.

## Build

Open the repository in the VS Code Dev Container. On Windows, start Docker Desktop in Linux containers mode, install the VS Code Dev Containers extension, and build the image from the checkout.

```sh
docker build -t pico-dev:latest -f .devcontainer/Dockerfile .
```

Use **Dev Containers: Reopen in Container**. After rebuilding the image, use **Dev Containers: Rebuild Container**. To sign in to Codex or GitHub CLI, run `bash .devcontainer/sign-in.sh` from the interactive container terminal. The credential volumes are shared across checkouts that use the same volume names.

Inside the container, run:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
picotool info build/rpico_bridge.uf2
```

The container pins Pico SDK and picotool 2.2.0, TinyUSB 0.21.0, Pico-PIO-USB 0.7.3 and ARM GCC 12.2. Rebuild older images to install TinyUSB 0.21.0. Outside the container, set `PICO_SDK_PATH`, `PICO_TINYUSB_PATH` and `PICO_PIO_USB_PATH` to the corresponding source directories. The project selects `waveshare_rp2040_zero`.

The default firmware build includes the 512 KiB read-only FAT12 drive. The
Dev Container includes Node.js, npm and Python 3. Before the first build, install
the configurator dependencies once:

```sh
cd configurator
npm ci
cd ..
```

The build generates the drive image at `build/docs_drive/rpico-bridge.img` and
embeds it in the UF2. To omit the drive and its build dependencies, configure
with `-DRPICO_DOCS_DRIVE=OFF`, for example:

```sh
cmake -S . -B build-no-docs -G Ninja -DCMAKE_BUILD_TYPE=Release -DRPICO_DOCS_DRIVE=OFF
```

The drive is named `RPICOBRIDGE` and contains `CONFIG.HTM`, `README.TXT` and
`LICENSE.TXT`. Open `CONFIG.HTM` directly from the drive in desktop Chrome or
Edge, then choose the Documentation view for quick start, routing, LED, serial
and hardware guidance. Allow Web Serial and Web MIDI access when prompted. The
bundled configurator runs locally without a network connection or server. The
drive content is firmware content and is read-only. Save routing and LED
settings through the configurator's CDC connection; those settings remain in
littlefs on the board.

An earlier regular UF2 matched the staged SHA-256 and passed picotool flash
verification. Linux recognized its 512 KiB removable drive alongside CDC ACM
and USB MIDI. All 1024 sectors matched its generated image. The volume reported
write protection and rejected WRITE10 with DATA PROTECT. The user reports that
the browser controller worked on that build. This firmware now moves the
hardware guide into the Documentation view, so the updated drive image still
needs a physical flash check. See [project state](docs/STATE.md) for detailed
drive and current board evidence.

## Native tests

```sh
cmake -S tests -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Normal and bench Release builds were warning-free. Native CTest passed 2/2,
including cable-exclusion routing. Earlier clean-clone default and bench UF2
hashes matched, and ASan/UBSan Debug CTest passed 2/2 with LeakSanitizer disabled.

## CDC test peer and bench mode

All firmware images provide CDC diagnostics, a MIDI test peer, and the
`BOOTSEL\n` command when DTR is asserted. The peer injects and monitors cable-zero
USB-MIDI event packets in the `M <8 hex digits>\n` format. Its test traffic uses
the same router, does not echo back to its source, and does not change `host=`.
It is appended after the eight real host endpoint slots. It is available in
regular and CDC bench builds.

The opt-in CDC bench build defaults OFF. It runs without the physical PIO USB
MIDI/HID host, for native-only tests. Regular firmware keeps the PIO host active
so the CDC peer can be tested alongside real host endpoints. Both modes keep
the existing device VID/PID.

Build it in a separate directory:

```sh
cmake -S . -B build-cdc-bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DRPICO_CDC_BENCH=ON
cmake --build build-cdc-bench
```

After asserting CDC DTR, write packets as `M <8 hex digits>\n` to inject them
from the CDC peer. Native USB-MIDI packets routed to CDC are printed in the same
format. Each line carries one raw four-byte USB-MIDI event packet on cable zero.
Packet lines can interleave with `midi ...` diagnostics. For example,
`M 09903C40` sends a Note On from CDC to native USB-MIDI.

Run the harness with an accessible raw USB device node. Get bus and device
numbers from `lsusb` and give the current user read and write permission.

```sh
python3 tests/cdc_bench.py --run --bus <BUS> --address <DEVICE>
```

An earlier bench run passed five packets in each direction, including Note
On/Off, SysEx start/end and realtime clock. Neither source received an echo. The
harness restored Linux driver bindings. The default image passed the CDC route
and persistence checks below, then restored Linux CDC/MIDI bindings.
With CDC DTR asserted, `BOOTSEL\n` requests bootloader entry in every firmware
image after about 200 ms of magenta. The default image physically entered
ROM without buttons. The magenta hue and timing were not independently
distinguished. Hardware BOOT/RESET bypasses firmware.

## Status LED

The configurator's **Status LED** panel reads and saves seven RGB colors plus
MIDI pulse and mouse activity timing. Defaults are ready gray, hub blue, MIDI
interface green (`#000300`), mouse cyan, MIDI traffic amber, BOOTSEL magenta and
initialization error red. The MIDI traffic pulse defaults to 8 ms. Pulses merge
during bursts and can be set from 2 to 100 ms. Mouse phase defaults to 250 ms
and hold to 1500 ms. These describe firmware defaults, not visual confirmation
of the current board.

The panel automatically reads the saved palette when the board connects. Use
**Read from board** to reload it later. A successful read replaces pending LED
edits in the panel without saving them.

On the CDC console, `help led` lists LED commands and `led` prints current or
staged settings. Stage changes with `led color ready|hub|midi|mouse|traffic|bootsel|error RRGGBB`,
`led timing MIDI_PULSE_MS MOUSE_PHASE_MS MOUSE_HOLD_MS` or `led defaults`. Use
`led save` to persist staged values or `led abort` to discard them. Timing
ranges are pulse 2–100 ms, phase 10–1000 ms and hold from phase through 5000 ms.
With asserted DTR, `R LED GET` returns one `R LED` line containing pulse, phase,
hold, then seven six-digit RGB values in the same order. `R LED SET` uses that
same order: pulse, phase, hold, ready, hub, midi, mouse, traffic, bootsel,
error. It returns one `R OK` only after the durable write is verified. Saving
while a PIO host is attached pauses routing, quiesces the PIO frame timer and
restarts the board; reconnect CDC and the hub afterward. See
[CDC wire protocol](#cdc-wire-protocol) and the configurator.

LED settings, routing policy and device catalog are stored in littlefs v2.11.3
(`6cb4e86540eca0d9ba62500a298385c9d863c8be`). On first boot after upgrading,
firmware migrates the legacy policy and device catalog into littlefs and applies
default LED settings. Existing rules and remembered device IDs are retained.
The regular UF2 was flashed and verified. CDC checks read the migrated APC Key
cable exclusion and the MicroFreak, APC Key and Circuit Tracks catalog entries,
saved and restored LED settings with durable acknowledgements, and confirmed
policy/catalog readback after each restart. `host=0` throughout; no attached
instruments, audible routing or LED colors were verified in this check. See
[project state](docs/STATE.md) for the image hash and detailed results.

## Configure routing

Rules are evaluated from top to bottom. The first matching `all`, `drop`, or
`to` rule wins. A `to` rule selects its destination USB cable independently;
MIDI channel values stay unchanged. The fallback routes to all other ports or
drops the message. A separate global cable exclusion removes one remembered
device cable from the routing graph, both as a source and destination. It takes
precedence over every ordered rule, and other cables on that device remain
available. Device names and IDs persist, and devices are discovered
automatically, including in standalone operation.

Connect to the CDC port with a serial terminal at 115200 baud and assert DTR.
The board prints a welcome line. Type `help` or `help rule` for command help.
Use `devices` to see remembered device IDs, online state and cable counts. Use
`rules` to inspect the active default and ordered rules. To route cable 0 from
one observed device to another on cable 0 for every channel, while dropping
unmatched traffic, enter:

```text
devices
begin drop
rule SOURCE_ID 0 any to DESTINATION_ID 0
rule DESTINATION_ID 0 any to SOURCE_ID 0
save
```

Replace both IDs with the numbers printed by `devices`. `save` validates,
persists and activates the policy. `abort` discards a staged edit. `bootsel`
requests RP2040 bootloader mode. Cable selectors accept `0` through `15` or
`any`; channel selectors accept `1` through `16`, `system` or `any`. `begin`
without an argument uses the `all` fallback. Rule actions are `all`, `drop`, or
`to DESTINATION CABLE`. To globally exclude one cable on a remembered device,
use `rule DEVICE CABLE any exclude`. For example, an earlier local catalog had
the APC Key 25 mk2 at ID 4; `rule 4 1 any exclude` blocks cable 1 in both
directions. The Akai protocol assigns the keybed and sustain to cable 0 and
control messages to cable 1, so this leaves the keyboard and sustain available
while suppressing that control cable. [Akai APC Key 25 mk2 communications
protocol, v1.1](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf)

The device accepts an exclusion before or after other rules because it applies
globally. It cannot be overridden by rule order. New rule edits use the CDC
configurator or CLI; the current firmware image already includes this capability.

An earlier default image passed DTR welcome, `help` with CR, `help rule` with
CRLF, `devices`, same-session `R GET` and malformed-rule rollback. A temporary
default-drop policy routed channel 1 from CDC to native USB cable 0 without echo
and dropped channel 2. The saved rule survived software BOOTSEL and picotool
reboot, and routing passed again. The original default-all/no-rules policy was
then restored durably. No instruments were attached during this console test.

With a PIO hub attached, harmless ALL-policy save and restore both returned
durable acknowledgements and rebooted the board. Enumeration advanced through
bus 1 devices 36, 37 and 38. Device names and IDs remained in the catalog.
After installing the exclusion-capable firmware, this route edit is a policy
change and does not require another firmware update.

### CDC wire protocol

Assert DTR to activate CDC and receive `rpico-bridge ready. Type help for
commands.` Both CR and LF terminate an input line; CRLF is accepted as one
terminator. `R GET` returns `R CONFIG <default>`, zero or more
`R DEVICE <id> <online0/1> <rx> <tx> <UTF8-name-hex>` lines, zero or more
`R RULE <src-id> <src-cable> <channel> <action> <dest-id> <dest-cable>` lines,
then `R END`. Device names are UTF-8 bytes encoded as hex.

To replace the saved policy, send `R BEGIN <0|1>` where 0 means route unmatched
traffic to all other ports and 1 means drop it. Add ordered rules with
`R RULE <src-id> <src-cable> <channel> <action> <dest-id> <dest-cable>`, then
send `R COMMIT`. Use `R ABORT` to discard a staged edit. Begin, rule and abort
commands return `R OK` on success or `R ERR <reason>` on failure. Commit returns
`R OK` only after the policy is durable and verified; otherwise it returns
`R ERR <reason>`. Send one machine command at a time.

Wire values are source ID 0 for any source, cable 0–15 or 255 for any cable,
channel 0–15 for MIDI channels 1–16, 16 for system messages or 255 for any
channel, and action 0 for all, 1 for drop, 2 for to or 3 for global cable
exclusion. Action 3 requires one remembered source ID, one exact cable 0–15,
channel 255 and canonical destination fields `0 255`. It removes that source
cable as both source and destination, regardless of rule order. For action 2,
destination ID and cable select the target independently; the MIDI channel is
unchanged. The current APC Key 25 mk2 exclusion is `R RULE 4 1 255 3 0 255`.
Lines such as the DTR welcome, help text, `midi ...` diagnostics, `M ...` packet
traffic and `R INFO ...` notices may interleave with responses. Ignore lines
that do not match the current response. Only one request may be outstanding.

Before policy, catalog or LED settings writes, firmware pauses routing and
cancels its application-owned PIO frame timer. Pico SDK `flash_safe_execute`
masks IRQs and ensures the other core is not executing or reading flash. This
firmware uses core 0 only and sets `PICO_FLASH_ASSUME_CORE1_SAFE=1` because core
1 is unused. LittleFS writes the settings file and verifies the resulting data
before acknowledging a save. An attached PIO host is paused and the board
restarts after a successful save, then devices enumerate again.
[Pico SDK flash safety](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#pico_flash)
explains this guarantee. The firmware verifies flash readback before `R COMMIT`
returns `R OK`; an attached PIO host then triggers a controlled reboot and
re-enumeration. An earlier automatic catalog save correlated with a host stall;
cause was not established. Attached-hub save and restore were subsequently
verified, with ACK and reboot on each save.

The optional Python helper can list the CDC port, show remembered devices, print
the saved policy as JSON and apply a JSON policy. Install `pyserial` from the
requirements file first:

```sh
python3 -m pip install -r tools/requirements.txt
python3 tools/midi_routes.py ports
python3 tools/midi_routes.py devices
python3 tools/midi_routes.py show
python3 tools/midi_routes.py apply "routing.json"
```

For example, replace IDs `3` and `4` with two IDs from `devices` to route
cable-zero traffic both ways on every channel and drop unmatched traffic:

```json
{
  "default": "drop",
  "rules": [
    { "source": 3, "source_cable": 0, "channel": "any", "action": "to", "destination": 4, "destination_cable": 0 },
    { "source": 4, "source_cable": 0, "channel": "any", "action": "to", "destination": 3, "destination_cable": 0 }
  ]
}
```

The current local APC Key policy uses `default: "all"` and this rule to remove
its control cable globally:

```json
{
  "default": "all",
  "rules": [
    { "source": 4, "source_cable": 1, "action": "exclude" }
  ]
}
```

The preferred graphical workflow is the local React configurator. It connects
to CDC to inspect devices, add or reorder rules, and select **Exclude cable**.
Exclusions apply globally before ordered rules. Its MIDI monitor and keyboard
use the browser's native MIDI access. Use a compatible Chromium browser and
grant Web Serial and Web MIDI access when prompted. Build the standalone page:

```sh
cd configurator
npm ci
npm run build
npm test
```

Open `configurator/dist/index.html` directly in the browser and select
**Connect board** and **Enable MIDI**. The build produces a self-contained page
that uses no remote service. If browser permissions are unavailable from the
file, serve `configurator/dist` on localhost or HTTPS and open the served page.
The 217 KB build passed seven Node tests and JSDOM integration checks. A full
headless Chromium run crashed, and no live browser-to-board session has passed.
The Python JSON workflow remains available for scripted configuration.

## OS MIDI logger and sender

Install the pinned Python backend and list OS MIDI ports. Use `python3` on
Linux/macOS or replace it with `python` on Windows:

```sh
python3 -m pip install -r tools/requirements.txt
python3 tools/midi_log.py ports
```

`python-rtmidi` is pinned to 1.5.8. The tool prefers a unique port matching the
bridge product name. Port indexes printed by `ports` are zero-based. Specify an
index when multiple matching ports appear.

Monitor the bridge input. Add `--port-index N` when selecting an explicit
zero-based input. Add `--log-file "PATH"` to append timestamped records:

```sh
python3 tools/midi_log.py monitor --log-file "bridge-midi.log"
python3 tools/midi_log.py monitor --port-index 0
```

SysEx, realtime messages and active sensing are included by default. Monitoring
only reads and logs. It never sends messages back through the port. OS MIDI
exposes one merged bridge port without per-source labels.

Send a note pair with optional channel, velocity and duration settings, or send
one complete MIDI message:

```sh
python3 tools/midi_log.py send --note 60 --channel 1 --velocity 80 --hold-ms 250
python3 tools/midi_log.py send --hex "90 3C 64"
```

The note command sends Note On and then Note Off. Hex messages accept forms such
as `90 3C 64` or `F8`.

On an earlier firmware, the user reports two-instrument routing through an
externally powered seven-port hub with the yellow PIO VBUS jumper disconnected.
The Pico's power source during that trial was not confirmed. This does not
establish upstream VBUS behavior or production load and fault protection. To
use the OS MIDI logger, connect the bridge's native USB-C data port to the
laptop using the wiring in
[hardware notes](docs/HARDWARE.md). The reported hub test did not verify this
simultaneous setup. Run `ports`, start `monitor` on the bridge input, and play
each device. Confirm the other device responds and the monitor logs traffic. A
power-only native USB connection cannot carry OS logger data.

The Python I/O tests passed with a fake backend, including SysEx, MIDI Clock,
Active Sensing, explicit sends, Note Off and cleanup on interruption. The
container has no ALSA sequencer, so no live OS MIDI application test has run.
Windows and macOS have not been tested.

## Flash

Hold **BOOT** and tap **RESET**, then release **BOOT**. Copy the UF2 from the selected build directory to the `RPI-RP2` drive. Use `build/rpico_bridge.uf2` for the regular build or `build-cdc-bench/rpico_bridge.uf2` for CDC bench mode. The drive ejects and the board restarts.

A native USB-only bench confirmed a blue LED and CDC diagnostics reporting
`host=0`. Sending Note On and Note Off to native MIDI OUT increased RX from 0 to
2. An empty bus-powered USB-C hub was configured through the four-pin breakout with
the Pico powered by a 5 V wall charger and the laptop disconnected. Solid green
was observed. No CDC capture was made in this setup. The USB descriptor announces
100 mA. Combined charger draw is unmeasured, and a laptop-powered experimental
hub cascade has not been tested. On an earlier firmware, the user reports
two-instrument routing through an externally powered seven-port hub with the
yellow PIO VBUS jumper disconnected. The Pico's power source during that trial
was not confirmed. This does not validate production power wiring or loaded
standalone power.

Bench firmware uses the shared pid.codes test identity `1209:0001`. Anyone may use it in their own private, in-house tests, but it may collide with other test devices. Do not distribute this firmware or use this ID on devices that will be redistributed, sold or manufactured; see the [pid.codes test PID terms](https://pid.codes/1209/0001/). See [project state](docs/STATE.md) for production identity status.

For Windows enumeration tests, leave the device attached to Windows. To test from the container instead, use WSL 2 USB/IP and confirm that Docker's Linux daemon can see the USB device node. Recheck the mapping after switching between BOOTSEL and application mode. [Microsoft WSL USB guide](https://learn.microsoft.com/en-us/windows/wsl/connect-usb)

If Windows Explorer cannot see `RPI-RP2` because the board is attached to WSL, run `usbipd list` in Windows PowerShell. Use the Pico's Windows bus ID in `usbipd detach --busid <BUSID>`. This returns the device to Windows so Explorer can mount the BOOTSEL drive. [Microsoft WSL USB guide](https://learn.microsoft.com/en-us/windows/wsl/connect-usb)

The optional Compose service gives the container access to one Linux USB device node. Set `PICO_USB_DEVICE` to that node and `PICO_USB_GID` to its numeric group on the Docker daemon side.

```sh
docker compose -f .devcontainer/compose.usb.yaml run --rm usb
docker compose -f .devcontainer/compose.usb.yaml run --rm usb picotool info
```

## Unattended Codex (optional)

From WSL, use a normal Git checkout with a configured author and an SSH push URL. Sign in to Codex from the Dev Container first. Load a repository-scoped write-enabled deploy key into a dedicated SSH agent and set `PICO_DEV_SSH_AUTH_SOCK` to its socket. Place GitHub's verified public host key in `~/.ssh/pico-dev-known-hosts`, then run `bash .devcontainer/run-unattended.sh` from the checkout. The launcher uses the agent socket and leaves the private key in WSL. [GitHub deploy keys](https://docs.github.com/en/authentication/connecting-to-github-with-ssh/managing-deploy-keys) · [GitHub host-key fingerprints](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/githubs-ssh-key-fingerprints)

## License

[MIT](LICENSE)
