RPICO-BRIDGE CONFIGURATOR

Open CONFIG.HTM in a current Chromium-based browser such as Chrome or Edge.
The page runs locally from this drive and does not need a server or internet.

1. Connect the RP2040-Zero to the computer with its native USB-C cable.
2. In the page, choose Connect board and select the rpico-bridge CDC port.
3. Review remembered MIDI devices, routing rules and Status LED settings.
4. Use Enable MIDI for the traffic monitor and test keyboard. The keyboard
   sends only to the selected MIDI output.
5. Save route or LED changes to the board. A save may restart the board; if CDC
   disconnects, choose Connect board again.

The board works as a MIDI bridge without this page once firmware is flashed and
the board has power. Its CDC serial interface also provides a command line;
connect at 115200 baud, assert DTR and enter help for commands. Use help rule
for routing syntax and help led for the LED colors and timing controls.

See HARDWARE.MD for board connections and bench guidance. LICENSE.TXT contains
the project license. CONFIG.HTM includes licenses for its bundled libraries.
