# rpico-bridge

A USB MIDI 1.0 bridge prototype for the Waveshare RP2040-Zero. The firmware
includes native MIDI and CDC interfaces, a PIO USB MIDI host and a packet router.
Its ELF descriptors were verified to expose CDC control/data and USB Audio
Control/MIDI Streaming interfaces, with one MIDI cable in each direction.
Native USB was bench-checked. An empty bus-powered hub was configured in one
isolated charger setup. Downstream MIDI devices and production hub wiring still
need verification. See
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

## Native tests

```sh
cmake -S tests -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Normal and bench Release builds were warning-free. Native CTest passed 2/2.
Earlier clean-clone default and bench UF2 hashes matched, and ASan/UBSan Debug
CTest passed 2/2 with LeakSanitizer disabled.

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

Earlier bench images entered ROM without buttons using `BOOTSEL\n`. Final
regular UF2 `e288af09…aa9fb` was flashed and verified, then enumerated as
`1209:0001` on bus 1 device 24 after software BOOTSEL. The Pico currently runs
this image with the hub disconnected.

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

The prior bench run passed five packets in each direction, including Note
On/Off, SysEx start/end and realtime clock. Neither source received an echo. The
harness restored Linux driver bindings. The final regular image also passed five
packets each way without echo with the hub disconnected, then restored Linux
CDC/MIDI bindings on bus 1 device 24.
With CDC DTR asserted, `BOOTSEL\n` requests bootloader entry in every firmware
image after about 200 ms of magenta. The final regular image physically entered
ROM without buttons. The magenta hue and timing were not independently
distinguished. Hardware BOOT/RESET bypasses firmware.

The installed palette is gray for ready, blue after the hub accepts
`SET_CONFIGURATION`, green for a host MIDI interface, cyan for recognized mouse
reports, amber for received MIDI, magenta for about 200 ms before BOOTSEL, and
red for initialization failure. Blue confirms configuration acceptance, not
class readiness. Amber pulses at a 250 ms half-period for 1.5 seconds after
activity. A temporary image showed green idle and cyan on mouse movement with
one HID receiver through the small hub and wall charger. An earlier regular
image showed blue and activity flashes, though the user found rapid hues hard to
distinguish. HID handling only observes reports. It does not route native
pointer input or add controls. Hardware BOOT/RESET bypasses the palette.

## OS MIDI logger and sender

Install the pinned Python backend and list OS MIDI ports. Use `python3` on
Linux/macOS or `python` on Windows:

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

For a short two-device check, connect two USB MIDI devices through the verified
PIO host wiring, then connect the bridge's native USB-C data port to the laptop.
This simultaneous setup requires verified wiring. Run `ports`, start `monitor`
on the bridge input, and play each device. Confirm the other device responds
and the monitor logs the traffic. From a second terminal, send a note to the
bridge output and confirm both devices respond. A power-only native USB
connection can run standalone automation but cannot carry OS logger data.

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
hub cascade has not been tested. No downstream MIDI device or externally powered
hub was tested. This does not validate loaded standalone power or production VBUS
requirements.

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
