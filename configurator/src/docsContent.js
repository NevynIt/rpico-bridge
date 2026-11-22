// Plain-text documentation data for the offline Documentation view.
// Keep these values as text; the React view renders every field as text nodes.
export const DOCS_PAGES = [
  {
    id: 'quickstart',
    title: 'Quick start',
    sections: [
      {
        heading: 'Open and connect',
        paragraphs: [
          'Open CONFIG.HTM from the RPICOBRIDGE drive in desktop Chrome or Edge. The configurator is bundled with the firmware and works offline. Allow Web Serial and Web MIDI when the browser asks.',
          'Connect the board to the computer with a USB-C data cable. Choose Connect board and select the rpico-bridge CDC device. Choose Enable MIDI to use the monitor and test keyboard. If a save restarts the board, connect again after CDC returns.',
          'The drive is read-only firmware content. Route and LED settings are saved to the board over CDC and stored in its settings flash.'
        ]
      },
      {
        heading: 'Inspect and route devices',
        paragraphs: [
          'The device list shows remembered MIDI devices, their IDs, online state, and input/output cable counts. Offline devices stay listed so their rules can be edited.',
          'Rules run from top to bottom. The first match selects Route all, Drop or Route to one device and cable. An unmatched message follows the all or drop default. MIDI channels stay unchanged; a destination cable is selected independently.',
          'Exclude cable is a separate global setting. It removes one remembered device cable as both a source and destination before ordered rules, and cannot be overridden by rule order. No per-message filters or channel rewriting are provided.'
        ],
        list: [
          'Choose a source device, cable and channel, then an action. For Route to, choose a destination and cable.',
          'Use the up and down controls to change rule order. Save to board validates and persists the ordered rules.',
          'A bidirectional pair needs one rule in each direction. The default all fallback already routes messages to other ports when no rule matches.',
          'To leave one cable out globally, add an Exclude cable rule for its remembered device and exact cable.'
        ],
        code: [
          'devices\nbegin drop\nrule SOURCE_ID 0 any to DESTINATION_ID 0\nrule DESTINATION_ID 0 any to SOURCE_ID 0\nsave'
        ]
      },
      {
        heading: 'MIDI monitor and test keyboard',
        paragraphs: [
          'Choose the bridge input and output, then enable MIDI. Capture controls the event log. The monitor observes incoming messages and never sends them back. The test keyboard sends only to the selected output.',
          'The on-screen keys send Note On while pressed and Note Off on release. Select the MIDI channel before playing.'
        ],
        list: [
          'Enable computer-keyboard capture to play notes from the keyboard. At octave 4, A/S/D/F/G/H/J/K/L/semicolon play B3/C4/D4/E4/F4/G4/A4/B4/C5/D5. E/R/Y/U/I/P play C-sharp 4, D-sharp 4, F-sharp 4, G-sharp 4, A-sharp 4 and C-sharp 5.',
          'The initial octave is 4, with S at middle C. Press digits 1–8 to select an octave; Z lowers the octave and X raises it.',
          'Shortcuts do not act while a text field or other editable control has focus, or when Ctrl, Alt, Meta or Shift is held. Releasing keys sends Note Off.'
        ]
      },
      {
        heading: 'Status LED',
        paragraphs: [
          'The Status LED panel automatically reads saved colors and MIDI/mouse activity timings when the board connects. Read from board reloads those saved settings later. A successful read replaces pending LED edits in the panel without saving them.',
          'Change colors or timings, then Save LED settings. A successful save is durable and may restart the bridge; reconnect if CDC disconnects.',
          'Default colors are ready gray, hub blue, MIDI-interface dim green (#000300), mouse cyan, MIDI traffic amber, BOOTSEL magenta and initialization error red. MIDI traffic pulses for 8 ms by default; bursts merge. The pulse range is 2–100 ms. Mouse phase and hold default to 250 ms and 1500 ms.',
          'Colors describe firmware states and are configurable. A blue hub state means TinyUSB accepted hub configuration; it does not prove that a MIDI interface mounted. HID reports are observed for status only and are not routed as pointer input.'
        ]
      },
      {
        heading: 'CDC serial console',
        paragraphs: [
          'The Serial console connects to the board’s CDC interface. The configurator asserts DTR when connected. A separate terminal should use 115200 baud and assert DTR. Enter help for the command list, help rule for routing syntax and help led for LED controls.',
          'Show periodic status and Show raw MIDI control diagnostic lines in the console. Both are off by default. Clear removes the visible console history; it does not change board settings.'
        ],
        list: [
          'devices lists remembered device IDs, names, online state and cable counts. rules shows the active fallback and ordered rules.',
          'begin drop stages a drop fallback. rule SOURCE CABLE CHANNEL to DESTINATION CABLE adds a route; use any for an unrestricted cable or channel. save commits changes and abort discards staged policy.',
          'rule DEVICE CABLE any exclude excludes one remembered cable in both directions. bootsel requests bootloader mode. Hardware BOOT/RESET remains available for recovery.',
          'Saves with an active PIO host pause routing and the host timer while flash is written. A successful save may restart the board and re-enumerate attached host devices.'
        ]
      }
    ]
  },
  {
    id: 'hardware',
    title: 'Hardware guide',
    sections: [
      {
        heading: 'Connections',
        paragraphs: [
          'The target is the Waveshare RP2040-Zero. Its native USB-C connector carries the board’s USB MIDI, CDC console and documentation drive. Use a USB-C data cable to connect to a computer.',
          'PIO USB host data uses GPIO14 for D+ and GPIO15 for D−. Connect the host adapter’s ground to board ground. Pico-PIO-USB recommends 22 ohm series resistors in both data lines.'
        ],
        list: [
          'Native USB-C to computer: board power, one bidirectional MIDI port and CDC.',
          'GPIO14/GPIO15 and ground to the hub upstream data connection: PIO host for downstream MIDI devices.',
          'The tested four-wire breakout was labeled V, D−, D+ and G. Its CC handling and operation across both USB-C plug orientations have not been established.'
        ],
        links: [
          { label: 'Waveshare RP2040-Zero schematic', href: 'https://files.waveshare.com/upload/4/4c/RP2040_Zero.pdf' },
          { label: 'Pinned Pico-PIO-USB wiring guidance', href: 'https://github.com/sekigon-gonnoc/Pico-PIO-USB/blob/b186e36bea0f3bcc2ee94ca1fa81e7f71281443a/README.md' }
        ]
      },
      {
        heading: 'Power and VBUS',
        paragraphs: [
          'Native USB-C may power the board during computer-connected tests. Standalone use needs a suitable 5 V source for the board and attached hub/devices. Production host power still needs defined current limiting and reverse-current protection.',
          'Do not connect a direct host VBUS jumper between the breakout and Pico 5 V while the computer is attached to native USB-C. That can feed hub VBUS from the computer through the board. The production host VBUS path and powered-hub behavior are not verified.'
        ],
        list: [
          'A temporary empty-hub check used a wall charger on native USB-C and the laptop disconnected. It did not test loaded power or fault protection.',
          'The board announces 100 mA to its native USB host. Do not treat this as a verified power budget for a hub or multiple peripherals.',
          'Use the hardware reset button if firmware recovery is needed. BOOT plus RESET enters the RP2040 ROM bootloader.'
        ],
        links: [
          { label: 'USB 2.0 specification, power rules', href: 'https://www.usb.org/document-library/usb-20-specification' }
        ]
      },
      {
        heading: 'Bench checks',
        paragraphs: [
          'Start with only the board and a USB-C data cable. Check that the computer enumerates native USB MIDI and CDC. In the console, confirm host=0 and send a MIDI note to verify the RX counter changes.',
          'Then connect the PIO host adapter and a hub using the verified wiring. Confirm expected MIDI interfaces in the device list before testing routing. USB-C orientation, breakout CC behavior, production VBUS, and loaded standalone power still need validation.'
        ],
        list: [
          'Use two MIDI devices and check both routing directions, channels, cable selection and no echo to the source.',
          'Exercise long SysEx alongside MIDI Clock and reconnect a device during traffic.',
          'For standalone tests, disconnect the laptop and use a suitable protected supply. Record the hub, devices and power arrangement.'
        ]
      }
    ]
  }
];
