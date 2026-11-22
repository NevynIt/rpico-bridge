# rpico-bridge

Build a small, standalone USB MIDI 1.0 bridge for the Waveshare RP2040-Zero.
Use native USB-C for one bidirectional MIDI port and CDC diagnostics. Use PIO USB
as host for multiple class-compliant MIDI devices on one externally powered hub.
Enumerate automatically and route each event to all other logical MIDI ports.
Preserve MIDI channels, virtual cable identities, SysEx, realtime and other valid
traffic. USB power must suffice for the board when a computer is absent. Use the
onboard RGB LED for status and the hardware reset button for recovery. v1 has no
routing configuration, channel mapping, filtering, extra controls or peripherals.

## Responsibilities

- Main agent owns milestones, architecture, task boundaries, review, integration,
  verification and GitHub. Prefer GPT-6.2 Sol with high reasoning, when available.
- `hardware` owns electrical design, wiring and BOM in `docs/HARDWARE.md`.
- `usb_firmware` owns SDK, platform, USB host/device, status LED and diagnostics.
- `midi` owns the transport-independent routing engine and packet semantics.
- `documentation` owns README and helps maintain the current-state snapshot.
- `testing` owns native tests, USB mocks and physical acceptance procedures.

Delegate bounded tasks to at most three specialists at once. Prefer GPT-6 Luna
with medium reasoning. Assign disjoint files and bring findings and interface
decisions to the main agent. Ask the owner before adopting physical assumptions
that affect safety or purchasing. Keep documentation to this file, README,
`docs/STATE.md` and `docs/HARDWARE.md` unless another document is necessary.

## Engineering

- Use the pinned Pico SDK toolchain and small C/C++ modules without an RTOS.
- Keep USB transport and routing independent. Treat USB MIDI event packets as
  packets and keep logical port identity explicit. Prevent an event from routing
  back to its origin. Preserve traffic ordering where the protocol requires it.
- Prefer bounded queues with explicit overflow behavior and no realtime allocation.
  Keep CDC diagnostics nonblocking. Define core/IRQ ownership of shared state.
- Verify host and device USB together before claiming dual-interface operation.
  Label evidence as compiled, simulated, or physically tested. Hardware acceptance
  requires the actual board, hub and multiple MIDI devices.
- Use warnings for project code and native tests for routing, backpressure and
  reconnect. Keep `main` buildable and never track build output or credentials.

## Build and verify

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
picotool info build/rpico_bridge.uf2
git diff --check
```

Run these in the dev container. Native test commands will be added with the
routing tests. See `docs/STATE.md` for the current verification and blockers.

## Resume and finish

Read this file and `docs/STATE.md`, then run `git status --short --branch` and
`git log -5 --oneline`. Pick the next useful task from the snapshot and start it.
After meaningful work, run relevant checks and replace outdated state in
`docs/STATE.md`. Commit and push completed, validated changes to the public
`nevynit/rpico-bridge` repository. Preserve shared history.
