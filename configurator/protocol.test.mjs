import test from 'node:test';
import assert from 'node:assert/strict';
import {
  decodeHexName, parseRecord, parseSnapshot, parseMidiPacket, formatMidiBytes,
  normalizeRule, DEFAULT_LED_SETTINGS, parseLedSettings, serializeLedSettings,
  validateLedSettings, SerialBridge,
} from './src/protocol.mjs';

test('parses device identity and all route fields', () => {
  assert.deepEqual(parseRecord('R DEVICE 3 1 2 1 4B6579626F617264'), {
    kind: 'device', id: 3, online: true, rx_cables: 2, tx_cables: 1, name: 'Keyboard',
  });
  assert.deepEqual(parseRecord('R RULE 3 255 16 2 1 0'), {
    kind: 'rule', source_id: 3, source_cable: 255, channel: 16, action: 2, destination_id: 1, destination_cable: 0,
  });
});

test('accepts unsolicited welcome and activity while reading a bounded snapshot', () => {
  assert.deepEqual(parseSnapshot(['Welcome to rpico-bridge', 'R CONFIG 0', 'M 09 3C 64', 'R END']), { default: 0, devices: [], rules: [] });
});

test('rejects malformed records and incomplete snapshots', () => {
  assert.equal(parseRecord('R DEVICE 2 1'), null);
  assert.equal(decodeHexName('not hex'), '');
  assert.throws(() => parseSnapshot(['R CONFIG 0']), /unexpectedly/);
  assert.throws(() => parseSnapshot(['R END']), /without a configuration/);
});

test('formats channel voice and realtime events for the monitor', () => {
  assert.match(parseMidiPacket([0x93, 60, 90]), /Note on · ch 4/);
  assert.equal(parseMidiPacket([0xf8]), 'Clock');
});

test('caps displayed raw MIDI bytes without mutating or shortening the packet', () => {
  const packet = Uint8Array.from({ length: 300 }, (_, i) => i & 0xff);
  const display = formatMidiBytes(packet);
  assert.equal(display.split(' … ')[0].split(' ').length, 256);
  assert.ok(display.endsWith('… +44 bytes'));
  assert.equal(packet.length, 300);
  assert.equal(packet[299], 43);
});

test('normalizes cable exclusion to the canonical firmware row', () => {
  const rule = normalizeRule({
    source_id: 3, source_cable: 2, channel: 7, action: 3,
    destination_id: 4, destination_cable: 5,
  }, [{ id: 3, name: 'Keyboard' }]);
  assert.deepEqual(rule, {
    source_id: 3, source_cable: 2, channel: 255, action: 3,
    destination_id: 0, destination_cable: 255,
  });
  assert.throws(() => normalizeRule({ action: 3, source_id: 0, source_cable: 0 }, [{ id: 3 }]), /remembered device/);
  assert.throws(() => normalizeRule({ action: 3, source_id: 3, source_cable: 255 }, [{ id: 3 }]), /exact USB cable/);
});

test('parses, validates, and serializes the LED palette contract', () => {
  const line = 'R LED 8 250 1500 0C0C0C 000812 000300 001212 120A00 120012 0C0000';
  assert.deepEqual(parseLedSettings(line), DEFAULT_LED_SETTINGS);
  assert.equal(serializeLedSettings(DEFAULT_LED_SETTINGS), '8 250 1500 0C0C0C 000812 000300 001212 120A00 120012 0C0000');
  assert.throws(() => parseLedSettings('R LED 8 250 1500 0C0C0C'), /malformed/);
  assert.throws(() => parseLedSettings(line.replace('000812', '#000812')), /six hexadecimal/);
  assert.throws(() => validateLedSettings({ ...DEFAULT_LED_SETTINGS, hold_ms: 249 }), /at least the phase/);
  assert.throws(() => validateLedSettings({ ...DEFAULT_LED_SETTINGS, pulse_ms: 101 }), /2–100/);
  const zeros = { pulse_ms: 2, phase_ms: 10, hold_ms: 10, ready: '000000', hub: '000000', midi: '000000', mouse: '000000', traffic: '000000', bootsel: '000000', error: '000000' };
  assert.equal(serializeLedSettings(zeros), '2 10 10 000000 000000 000000 000000 000000 000000 000000');
  const maximum = { pulse_ms: 100, phase_ms: 1000, hold_ms: 5000, ready: 'FFFFFF', hub: 'FFFFFF', midi: 'FFFFFF', mouse: 'FFFFFF', traffic: 'FFFFFF', bootsel: 'FFFFFF', error: 'FFFFFF' };
  assert.equal(serializeLedSettings(maximum), '100 1000 5000 FFFFFF FFFFFF FFFFFF FFFFFF FFFFFF FFFFFF FFFFFF');
});

test('asserts DTR, reads unsolicited welcome text, and transacts ordered rules', async () => {
  const commands = [];
  let controller;
  const encoder = new TextEncoder();
  const port = {
    async open() {},
    async setSignals(value) {
      assert.deepEqual(value, { dataTerminalReady: true });
      controller.enqueue(encoder.encode('Welcome to bridge\n'));
    },
    async close() {},
    readable: new ReadableStream({ start(c) { controller = c; } }),
    writable: new WritableStream({
      write(data) {
        const command = new TextDecoder().decode(data).trim();
        commands.push(command);
        const response = command === 'R GET'
          ? 'R CONFIG 0\nR END\n'
          : 'R OK\n';
        controller.enqueue(encoder.encode(response));
      },
    }),
  };
  const seen = [];
  const bridge = new SerialBridge(port, { onLine: line => seen.push(line) });
  await bridge.open();
  const snapshot = await bridge.getSnapshot();
  assert.equal(snapshot.default, 0);
  assert.ok(seen.includes('Welcome to bridge'));
  await bridge.applySnapshot({ default: 1, rules: [
    { source_id: 3, source_cable: 255, channel: 255, action: 2, destination_id: 1, destination_cable: 255 },
    { source_id: 1, source_cable: 0, channel: 0, action: 1, destination_id: 0, destination_cable: 255 },
    { source_id: 3, source_cable: 1, channel: 8, action: 3, destination_id: 2, destination_cable: 4 },
  ], devices: [{ id: 3, name: 'Keyboard' }] });
  assert.deepEqual(commands, [
    'R GET', 'R BEGIN 1', 'R RULE 3 255 255 2 1 255',
    'R RULE 1 0 0 1 0 255', 'R RULE 3 1 255 3 0 255', 'R COMMIT',
  ]);
  await bridge.close();
});

test('reads and durably sets LED settings with the exact CDC command fields', async () => {
  const commands = [];
  let controller;
  const encoder = new TextEncoder();
  const port = {
    async open() {}, async setSignals() {}, async close() {},
    readable: new ReadableStream({ start(c) { controller = c; } }),
    writable: new WritableStream({
      write(data) {
        const command = new TextDecoder().decode(data).trim();
        commands.push(command);
        if (command === 'R LED GET') {
          controller.enqueue(encoder.encode('R LED 8 250 1500 0C0C0C 000812 000300 001212 120A00 120012 0C0000\n'));
        } else if (command.startsWith('R LED SET ')) {
          controller.enqueue(encoder.encode('R OK\n'));
        } else if (command === 'R GET') {
          controller.enqueue(encoder.encode('R CONFIG 0\nR END\n'));
        }
      },
    }),
  };
  const bridge = new SerialBridge(port);
  await bridge.open();
  const loaded = await bridge.getLedSettings();
  assert.deepEqual(loaded, DEFAULT_LED_SETTINGS);
  await bridge.setLedSettings({ ...loaded, mouse: 'AABBCC' });
  assert.deepEqual(commands, [
    'R LED GET',
    'R LED SET 8 250 1500 0C0C0C 000812 000300 AABBCC 120A00 120012 0C0000',
  ]);
  await bridge.close();
});

test('an older firmware LED error or timeout leaves later route requests usable', async () => {
  let controller;
  let ledQueries = 0;
  const encoder = new TextEncoder();
  const port = {
    async open() {}, async setSignals() {}, async close() {},
    readable: new ReadableStream({ start(c) { controller = c; } }),
    writable: new WritableStream({
      write(data) {
        const command = new TextDecoder().decode(data).trim();
        if (command === 'R LED GET' && ledQueries++ === 0) controller.enqueue(encoder.encode('R ERR command\n'));
        if (command === 'R GET') controller.enqueue(encoder.encode('R CONFIG 0\nR END\n'));
      },
    }),
  };
  const bridge = new SerialBridge(port);
  await bridge.open();
  await assert.rejects(bridge.getLedSettings(), /R ERR command/);
  assert.equal((await bridge.getSnapshot()).default, 0);
  await assert.rejects(bridge.commandUntil('R LED GET', line => line.startsWith('R LED '), 5), /Timed out/);
  assert.equal((await bridge.getSnapshot()).default, 0);
  await bridge.close();
});
