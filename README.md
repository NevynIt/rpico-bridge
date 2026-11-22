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

## Flash

Hold **BOOT** and tap **RESET**, then release **BOOT**. Copy `build/rpico_bridge.uf2` to the `RPI-RP2` drive. The drive ejects and the board restarts. For the first hardware check, connect only the board to a computer over native USB. Expect the LED to be blue and the CDC diagnostic to report `host=0`; the RX counter should increase when MIDI reaches the bridge. These physical indications await bench confirmation. The PIO host adapter and powered-hub operation are not yet verified.

Bench firmware uses the shared pid.codes test identity `1209:0001`. Anyone may use it in their own private, in-house tests, but it may collide with other test devices. Do not distribute this firmware or use this ID on devices that will be redistributed, sold or manufactured; see the [pid.codes test PID terms](https://pid.codes/1209/0001/). See [project state](docs/STATE.md) for production identity status.

For Windows enumeration tests, leave the device attached to Windows. To test from the container instead, use WSL 2 USB/IP and confirm that Docker's Linux daemon can see the USB device node. Recheck the mapping after switching between BOOTSEL and application mode. [Microsoft WSL USB guide](https://learn.microsoft.com/en-us/windows/wsl/connect-usb)

The optional Compose service gives the container access to one Linux USB device node. Set `PICO_USB_DEVICE` to that node and `PICO_USB_GID` to its numeric group on the Docker daemon side.

```sh
docker compose -f .devcontainer/compose.usb.yaml run --rm usb
docker compose -f .devcontainer/compose.usb.yaml run --rm usb picotool info
```

## Unattended Codex (optional)

From WSL, use a normal Git checkout with a configured author and an SSH push URL. Sign in to Codex from the Dev Container first. Load a repository-scoped write-enabled deploy key into a dedicated SSH agent and set `PICO_DEV_SSH_AUTH_SOCK` to its socket. Place GitHub's verified public host key in `~/.ssh/pico-dev-known-hosts`, then run `bash .devcontainer/run-unattended.sh` from the checkout. The launcher uses the agent socket and leaves the private key in WSL. [GitHub deploy keys](https://docs.github.com/en/authentication/connecting-to-github-with-ssh/managing-deploy-keys) · [GitHub host-key fingerprints](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/githubs-ssh-key-fingerprints)

## License

[MIT](LICENSE)
