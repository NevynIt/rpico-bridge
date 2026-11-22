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

Clean-clone default and CDC bench UF2 hashes matched. Normal, bench and isolated
Release builds were warning-free. Native and ASan/UBSan Debug suites each passed
2/2, with LeakSanitizer disabled.

## CDC-only bench mode

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

The CDC bench UF2 passed flash verification and re-enumerated as `1209:0001`
with the same product. Earlier bench images entered ROM without buttons using
`BOOTSEL\n`. The final regular UF2 was verified and physically passed five MIDI
packets in each direction without echo. Its `BOOTSEL\n` command entered ROM
without buttons. The same UF2 was reloaded and the app re-enumerated. The Pico
currently runs the final regular image with the hub disconnected.

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

The run passed five packets in each direction, including Note On/Off, SysEx
start/end and realtime clock. Neither source received an echo. The harness
restored Linux driver bindings after the run. The hub was disconnected for this
regular-image run.
With CDC DTR asserted, `BOOTSEL\n` requests bootloader entry in every firmware
image after about 200 ms of magenta. The final regular image physically entered
ROM without buttons and then reflashed successfully. The magenta hue and timing
were not independently distinguished. Hardware BOOT/RESET bypasses firmware.

Regular firmware implements solid blue when ready with no host MIDI, green when
a host MIDI interface is attached, cyan on recognized mouse reports, amber on
received MIDI, and red on initialization failure. HID mouse handling only
observes reports. It does not route native pointer input or add controls. The
diagnostic image stays green after hub configuration and turns cyan on mouse
reports. Green idle and cyan on mouse movement were visually confirmed on the
temporary image with one HID receiver through the small hub on an isolated wall
charger. On the final regular image the user saw blue and activity flashes. The
rapid hues were hard to distinguish.

## Flash

Hold **BOOT** and tap **RESET**, then release **BOOT**. Copy the UF2 from the selected build directory to the `RPI-RP2` drive. Use `build/rpico_bridge.uf2` for the regular build or `build-cdc-bench/rpico_bridge.uf2` for CDC bench mode. The drive ejects and the board restarts. A native USB-only bench confirmed a blue LED and CDC diagnostics reporting `host=0`. Sending Note On and Note Off to native MIDI OUT increased the RX count from 0 to 2. An empty bus-powered USB-C hub was configured through the four-pin breakout with the Pico powered by a 5 V wall charger and the laptop disconnected. Solid green was observed. No CDC capture was made while standalone. Downstream MIDI devices and an externally powered hub remain untested. This fixed setup does not establish production VBUS or standalone power requirements.
Hold **BOOT** and tap **RESET**, then release **BOOT**. Copy the UF2 from the selected build directory to the `RPI-RP2` drive. Use `build/rpico_bridge.uf2` for the regular build or `build-cdc-bench/rpico_bridge.uf2` for CDC bench mode. The drive ejects and the board restarts. A native USB-only bench confirmed a blue LED and CDC diagnostics reporting `host=0`. Sending Note On and Note Off to native MIDI OUT increased the RX count from 0 to 2. An empty bus-powered USB-C hub was configured through the four-pin breakout with the Pico powered by a 5 V wall charger and the laptop disconnected. Solid green was observed. No CDC capture was made while standalone. The USB descriptor announces 100 mA. Combined charger draw is unmeasured, and a laptop-powered experimental hub cascade has not been tested. Downstream MIDI devices and an externally powered hub remain untested. This fixed setup does not establish loaded standalone power or production VBUS requirements.

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
