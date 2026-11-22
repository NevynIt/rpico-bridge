import test from 'node:test';
import assert from 'node:assert/strict';
import { decodeHexName, parseRecord, parseSnapshot, parseMidiPacket, formatMidiBytes, normalizeRule, SerialBridge } from './src/protocol.mjs';

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
