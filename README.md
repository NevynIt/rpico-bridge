# rpico-bridge

A USB MIDI 1.0 bridge for the Waveshare RP2040-Zero. The native USB-C port will expose one MIDI port plus CDC diagnostics. A second USB interface built with PIO will host MIDI devices through a powered hub. The bridge will forward MIDI to every other connected logical port and run from USB power with or without a computer.

The current firmware is a foundation image that starts the Pico SDK and idles. See [project state](docs/STATE.md) for what has been verified and [hardware](docs/HARDWARE.md) for board and host-port details.

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

The container pins Pico SDK and picotool 2.2.0, the SDK's TinyUSB 0.18.0 submodule, Pico-PIO-USB 0.7.3 and ARM GCC 12.2. Outside the container, set `PICO_SDK_PATH` to an initialized SDK checkout. The project selects `waveshare_rp2040_zero` by default. See [exact dependency revisions](docs/STATE.md#build-evidence).

## Flash

Hold **BOOT** and tap **RESET**, then release **BOOT**. Copy `build/rpico_bridge.uf2` to the `RPI-RP2` drive. The drive ejects and the board restarts. The foundation image leaves the application USB interfaces and RGB LED idle.

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
