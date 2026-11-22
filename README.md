# pico-dev

`pico-dev` is a Linux development image for Raspberry Pi Pico SDK projects. The first target is the Waveshare RP2040-Zero. The workspace currently contains environment configuration only.

## Set up on Windows

Start Docker Desktop in Linux containers mode. Install the **Dev Containers** extension in VS Code. From the repository root in PowerShell, build the image:

```powershell
docker build -t pico-dev:latest -f .devcontainer/Dockerfile .
```

Open this folder in VS Code and run **Dev Containers: Reopen in Container**. After rebuilding the image, run **Dev Containers: Rebuild Container** to recreate the environment. The container runs as `pico` (UID and GID 1000), with no privileged mode or Docker socket mount. If you move a project onto a native Linux filesystem, make sure UID 1000 can write to its checkout.

### Sign in inside the container

```sh
bash .devcontainer/sign-in.sh
```

Complete the Codex device-code flow in your Windows browser. Device-code sign-in must be allowed in your ChatGPT account or workspace. The script also offers optional GitHub CLI sign-in for interactive use. Codex uses GPT-6.2 Sol with high reasoning and defaults subagents to GPT-6 Luna, subject to account and workspace access. [Codex authentication](https://learn.chatgpt.com/docs/auth) · [Codex subagents](https://learn.chatgpt.com/docs/agent-configuration/subagents)

The `pico-dev-codex` volume holds `/home/pico/.codex`, including its local configuration and authentication cache. The `pico-dev-gh` volume holds `/home/pico/.config/gh`. Docker seeds a new Codex volume with the image's `config.toml`. A reused volume retains its existing preferences, so edit `~/.codex/config.toml` inside the container if you want to apply changes from `.devcontainer/codex.toml`. Both volumes survive container rebuilds and are shared by projects that use these volume names. Use distinct volume names in another project's `devcontainer.json` if it needs separate accounts. Treat the volumes as credentials and run `codex logout` or `gh auth logout` in the container to sign out.

On Docker Desktop with WSL 2, the Dev Container disables Docker's outer seccomp filter for this container so Bubblewrap can create its namespaces. The container runs as `pico`, drops all Linux capabilities, and sets `no-new-privileges`. Codex still applies its own filesystem and syscall restrictions to sandboxed commands. The optional USB service keeps Docker's default seccomp profile. [Docker seccomp profiles](https://docs.docker.com/engine/security/seccomp/) · [Codex Linux sandbox](https://github.com/openai/codex/blob/main/codex-rs/linux-sandbox/README.md)

After updating an existing checkout, run **Dev Containers: Rebuild Container** to apply the runtime flags. Start Codex with `codex`. To check the sandbox from the container terminal, run `codex sandbox -- /usr/bin/id` and look for UID 1000. `--yolo` skips both Codex's sandbox and approval prompts, giving agent commands the development user's access to the Windows workspace mount and both credential volumes.

## Run unattended with SSH pushes

1. Run the sign-in script above in the VS Code Dev Container. The unattended container reuses the `pico-dev-codex` volume. You can skip the GitHub CLI sign-in prompt.
2. Enable Docker Desktop **Settings → Resources → WSL Integration** for your Ubuntu distro. Run the remaining commands from Ubuntu WSL with the same UID as `pico` (1000). Use a normal Git checkout with a `.git` directory, an `origin` SSH push URL for the GitHub repository, and a Git `user.name` and `user.email`. [Docker WSL integration](https://docs.docker.com/desktop/features/wsl/)
3. Create a passphrase-protected SSH key in your WSL home and add only its `.pub` file under the GitHub repository's **Settings → Deploy keys**, with **Allow write access** selected. Copy GitHub's published `github.com` host-key line into `~/.ssh/pico-dev-known-hosts` and make it readable by UID 1000. [GitHub deploy keys](https://docs.github.com/en/authentication/connecting-to-github-with-ssh/managing-deploy-keys) · [GitHub host keys](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/githubs-ssh-key-fingerprints)

```sh
ssh-keygen -t ed25519 -f ~/.ssh/pico-dev-deploy
eval "$(ssh-agent -s)"
ssh-add ~/.ssh/pico-dev-deploy
export PICO_DEV_SSH_AUTH_SOCK="$SSH_AUTH_SOCK"
cd /mnt/c/Code/rpico-bridge
git remote set-url --push origin git@github.com:OWNER/REPO.git
bash .devcontainer/run-unattended.sh
```

The launcher uses a dedicated agent with exactly one loaded key. It mounts the agent socket and the public host-key file, uses strict host-key checking, and passes the local Git author identity. The private key stays in WSL. The launcher bypasses VS Code credential forwarding and omits the personal GitHub CLI volume. Its Bubblewrap profile denies access to the Codex OAuth cache (`auth.json`) and the GitHub CLI credential directory, allows workspace and Git metadata writes, and enables command networking and live web search. Approval requests are disabled. Keep the SSH agent running for the job and stop it with `ssh-agent -k` when finished. The launcher's interactive terminal can also run `bash .devcontainer/run-unattended.sh exec 'TASK'` for a one-shot Codex task.

## Herdr inside the Dev Container

The image includes Herdr v0.9.5 for Linux amd64 and arm64, verified against the release checksums. In a running Dev Container, open a shell and run `herdr`. Detaching from Herdr leaves its processes running while that container stays up. Recreating the container ends those processes. The Codex and GitHub CLI login volumes survive recreation. For the credential-protected unattended launcher above, start it from a Herdr pane in Ubuntu WSL. [Herdr v0.9.5](https://github.com/herdrdev/herdr/releases/tag/v0.9.5)

## Toolchain and reuse

The image uses Debian 12 Bookworm Slim and Bookworm's ARM GNU GCC 12.2 package, pinned to `15:12.2.rel1-1`. Pico SDK and picotool use release `2.2.0`. The SDK checkout initializes its submodules, including the pinned TinyUSB revision in `$PICO_SDK_PATH/lib/tinyusb`. Pico-PIO-USB `0.7.3` is available at `$PICO_PIO_USB_PATH`. CMake, Ninja, Git, Python, GitHub CLI, libusb, usbutils, OpenOCD and GDB Multiarch come from Bookworm. The official Codex installer and unpinned Debian packages resolve at image build time. [Pico SDK releases](https://github.com/raspberrypi/pico-sdk/releases) · [Pico-PIO-USB releases](https://github.com/sekigon-gonnoc/Pico-PIO-USB/releases) · [Codex CLI installation](https://learn.chatgpt.com/docs/codex/cli)

For a future Pico SDK project, reuse the image and point its Dev Container configuration at `pico-dev:latest`. Import `$ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake` before `project()` in the project's CMake file, then call `pico_sdk_init()`. Use the SDK's TinyUSB targets as needed. If your TinyUSB integration accepts `PICO_PIO_USB_PATH` as a CMake variable, pass `-DPICO_PIO_USB_PATH="$PICO_PIO_USB_PATH"` at configure time. For direct Pico-PIO-USB use, add its sources with `add_subdirectory("$ENV{PICO_PIO_USB_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/pico-pio-usb")` and link `pico_pio_usb` to your target. Set `PICO_BOARD` per project.

Run this in the VS Code container terminal to verify a Waveshare build. It creates a throwaway program in `/tmp` and checks the resulting UF2.

```bash
(
  set -eu
  test "$(id -u)" -ne 0
  arm-none-eabi-gcc --version
  picotool version
  gh --version
  codex --version
  test -f "$PICO_SDK_PATH/lib/tinyusb/src/tusb.h"
  test -f "$PICO_PIO_USB_PATH/CMakeLists.txt"
  smoke_dir=$(mktemp -d)
  trap 'rm -rf -- "$smoke_dir"' EXIT
  cat > "$smoke_dir/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(pico_dev_smoke C CXX ASM)
pico_sdk_init()
add_subdirectory("$ENV{PICO_PIO_USB_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/pico-pio-usb")
add_executable(pico_dev_smoke main.c)
target_link_libraries(pico_dev_smoke PRIVATE pico_stdlib pico_pio_usb)
pico_add_extra_outputs(pico_dev_smoke)
EOF
  printf 'int main(void) { return 0; }\n' > "$smoke_dir/main.c"
  cmake -S "$smoke_dir" -B "$smoke_dir/build" -G Ninja -DPICO_BOARD=waveshare_rp2040_zero
  cmake --build "$smoke_dir/build"
  test -s "$smoke_dir/build/pico_dev_smoke.uf2"
  picotool info "$smoke_dir/build/pico_dev_smoke.uf2"
)
```

## Flashing and optional USB access

For UF2 flashing, build in the container, put the board into BOOTSEL mode, and copy the UF2 from the workspace to the `RPI-RP2` drive in Windows Explorer. For testing USB MIDI enumeration on Windows, leave the device attached to Windows. A device attached over USB/IP is owned by the Linux side until detached. [Waveshare RP2040-Zero guide](https://www.waveshare.com/wiki/RP2040-Zero) · [Microsoft WSL USB guide](https://learn.microsoft.com/en-us/windows/wsl/connect-usb)

Docker Desktop runs Linux containers in a Linux environment. Compose `devices` maps an existing Linux device node on the Docker daemon side. Windows device paths and drive letters cannot be mapped this way. [Docker Desktop USB FAQ](https://docs.docker.com/desktop/troubleshoot-and-support/faqs/general/)

### WSL 2 backend

1. Update WSL with `wsl --update`, install `usbipd-win`, and keep a regular WSL 2 distro terminal open. Use `usbipd list` in PowerShell to identify the Windows bus ID.
2. From an elevated PowerShell, share the device with `usbipd bind --busid <BUSID>`. From regular PowerShell, run `usbipd attach --wsl --busid <BUSID>`.
3. Check `lsusb` in WSL and verify that the corresponding `/dev/bus/usb/BBB/DDD` is visible to Docker Desktop's Linux daemon. With the WSL 2 backend, you can check the backend with `wsl -d docker-desktop -- ls -ln /dev/bus/usb/BBB/DDD` if that distro exposes a shell. Seeing the device in a regular WSL distro alone does not establish that Docker can map the node. Use the optional Compose command below to test the daemon's view.
4. Run `usbipd detach --busid <BUSID>` to return the device to Windows. After reconnecting, restarting WSL, or switching the board between BOOTSEL and application mode, check the attachment and Linux device path again.

[Microsoft WSL USB guide](https://learn.microsoft.com/en-us/windows/wsl/connect-usb) · [usbipd-win](https://github.com/dorssel/usbipd-win)

### Hyper-V backend

Docker Desktop's Hyper-V VM is separate from WSL 2. `usbipd attach --wsl` attaches to WSL, so the Hyper-V Docker daemon will have no device node from that command. Docker documents a USB/IP route for Hyper-V in which a USB/IP server exports the device and a temporary privileged helper enters the Docker VM namespaces to attach it. The helper is separate from `pico-dev`. Docker's guide demonstrates an emulated HID device and does not guarantee support for a specific board or probe. Once the VM sees the desired Linux device node, use the Compose configuration below. [Docker Desktop USB/IP guide](https://docs.docker.com/desktop/features/usbip/)

### Run the optional USB service

Set the USB node path and the numeric group that has read/write access to it in the Docker daemon's Linux environment. For example, `ls -ln /dev/bus/usb/001/005` shows the node's numeric owner and group. If needed, configure the Linux environment that owns the node to grant read/write access to an appropriate group. The Compose service adds that group to `pico` and maps only the selected node, with a read-only workspace mount and no credential volumes.

```powershell
$env:PICO_USB_DEVICE = '/dev/bus/usb/001/005'  # Replace with the daemon-side node.
$env:PICO_USB_GID = '46'                     # Replace with its read/write group.
docker compose -f .devcontainer/compose.usb.yaml run --rm usb
docker compose -f .devcontainer/compose.usb.yaml run --rm usb picotool info
```

Run `picotool info` with the RP2040-Zero in BOOTSEL mode. If `lsusb` sees it but `picotool` gets a permission error, check the node's group and mode. A stale node path requires a fresh attachment check and a new Compose run. For SWD debugging, map the USB debug probe and select the matching OpenOCD interface and target configuration. A board connected only through its own USB port exposes no SWD probe.
