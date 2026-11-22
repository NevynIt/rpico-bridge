# rpico-bridge

Build a small USB MIDI 1.0 bridge for the Waveshare RP2040-Zero. Use native
USB-C for one bidirectional MIDI port and CDC diagnostics. Use PIO USB as host
for multiple class-compliant MIDI devices on a hub. Discover devices and route
each event among logical MIDI ports. Preserve MIDI channels, SysEx, realtime
and other valid traffic. Routing rules may select a destination USB cable
independently. USB power must suffice when a computer is absent. Use the onboard
RGB LED for status and hardware reset for recovery.
Routing policy is configurable, but v1 has no channel rewriting, filters, extra
controls or peripherals.

## Responsibilities

- Main agent owns milestones, architecture, task boundaries, review, integration,
  final verification and publishing to GitHub. Prefer GPT-6.2 Sol with high
  reasoning, when available.
- Specialists own detailed implementation, debugging, technical research, test
  authoring and detailed verification within their assigned scope. The main agent
  assigns bounded tasks, reviews the evidence and changes, then integrates them.
- `hardware` owns electrical design, wiring and BOM in `docs/HARDWARE.md`.
- `usb_firmware` owns SDK, platform, USB host/device, status LED and diagnostics.
- `midi` owns the transport-independent routing engine and packet semantics.
- `documentation` owns README and helps maintain the current-state snapshot.
- `testing` owns native tests, USB mocks and physical acceptance procedures.
- The routing policy uses an ordered list of match rules and a fallback action.
  Preserve MIDI channel values. Rules can select destination cables. Do not add
  channel rewriting or filtering.

Use specialist delegation for detailed work. Assign a clear deliverable and
scope, avoid overlapping file ownership, and keep no more than three specialists
active at once. Prefer GPT-6 Luna with medium reasoning. Specialists report
evidence and interface decisions to the main agent, who owns integration and
publication. Ask the hardware owner before adopting physical assumptions that
affect safety or purchasing. Keep documentation to this file, README,
`docs/STATE.md` and `docs/HARDWARE.md` unless another document is necessary.

## Engineering

- Use the pinned Pico SDK toolchain and small C/C++ modules without an RTOS.
- Keep USB transport and routing independent. Treat USB MIDI event packets as
  packets and keep logical port identity explicit. Prevent an event from routing
  back to its origin. Preserve traffic ordering where the protocol requires it.
- Prefer bounded queues with explicit overflow behavior and no realtime allocation.
  Keep CDC diagnostics nonblocking. Define core/IRQ ownership of shared state.
- Before SDK flash writes for policy or catalog updates, pause MIDI routing and
  stop the PIO host timer. Mask the native CDC IRQ during the flash operation.
  A successful save with an attached PIO host is expected to reboot and
  re-enumerate the host-side devices. Acknowledge explicit commits only after
  durable flash verification.
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
cmake -S tests -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
git diff --check
```

Run the firmware build in the dev container. The native tests also run on a
Linux host without a Pico SDK. See `docs/STATE.md` for verification and blockers.

## Resume and finish

Read this file and `docs/STATE.md`, then run `git status --short --branch` and
`git log -5 --oneline`. Pick the next useful task from the snapshot and start it.
After meaningful work, run relevant checks and replace outdated state in
`docs/STATE.md`. Commit and push completed, validated changes to the public
`nevynit/rpico-bridge` repository. Preserve shared history.
