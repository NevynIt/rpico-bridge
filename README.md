# rpico-bridge

A USB MIDI 1.0 bridge prototype for the Waveshare RP2040-Zero. The firmware
includes native MIDI and CDC interfaces, a PIO USB MIDI host and a packet router.
Its ELF descriptors were verified to expose CDC control/data and USB Audio
Control/MIDI Streaming interfaces, with one MIDI cable in each direction.
Physical USB operation and hub wiring still need bench verification. See
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

## CDC-only bench mode

This opt-in bench build routes between the native USB-MIDI peer and a virtual
MIDI peer on CDC through the same router. It runs without a physical PIO host
or hub, keeps the existing device VID/PID, and defaults to OFF.

Build it in a separate directory:

```sh
cmake -S . -B build-cdc-bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DRPICO_CDC_BENCH=ON
cmake --build build-cdc-bench
```

The owner used physical BOOT/RESET once to enter BOOTSEL and install the bench
UF2. Flash verification succeeded, and the board re-enumerated as `1209:0001`
with the same product. The board currently runs this bench UF2.

After asserting CDC DTR, write packets as `M <8 hex digits>\n` to inject them
from the CDC peer. Native USB-MIDI packets routed to CDC are printed in the same
format. Each line carries one raw four-byte USB-MIDI event packet on cable zero.
Packet lines can interleave with `midi ...` diagnostics. For example,
`M 09903C40` sends a Note On from CDC to native USB-MIDI.

Run the bench harness with an accessible raw USB device node. Get the bus and
device numbers from `lsusb` and make sure the current user has permission to
read and write the node.

```sh
python3 tests/cdc_bench.py --run --bus <BUS> --address <DEVICE>
```

The run passed five packets in each direction, including Note On/Off, SysEx
start/end and realtime clock. Neither source received an echo. The harness
restored Linux driver bindings after the run. No physical PIO host was active.
In bench mode, `BOOTSEL\n` over CDC requests bootloader entry. This command is
compiled in the bench build, but software re-entry has not been separately
tested.

## Flash

Hold **BOOT** and tap **RESET**, then release **BOOT**. Copy the UF2 from the selected build directory to the `RPI-RP2` drive. Use `build/rpico_bridge.uf2` for the regular build or `build-cdc-bench/rpico_bridge.uf2` for CDC bench mode. The drive ejects and the board restarts. A native USB-only bench confirmed a blue LED and CDC diagnostics reporting `host=0`. Sending Note On and Note Off to native MIDI OUT increased the RX count from 0 to 2. The PIO host adapter and powered-hub operation remain unverified.

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
