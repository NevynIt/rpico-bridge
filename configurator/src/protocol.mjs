export function decodeHexName(text) {
  if (!text || !/^(?:[0-9a-f]{2})*$/i.test(text)) return '';
  try { return new TextDecoder().decode(Uint8Array.from(text.match(/../g) || [], h => parseInt(h, 16))); }
  catch { return ''; }
}

export function parseRecord(line) {
  const f = line.trim().split(/\s+/);
  if (f[0] !== 'R') return null;
  if (f[1] === 'CONFIG' && f.length === 3 && /^\d+$/.test(f[2])) return { kind: 'config', default: Number(f[2]) };
  if (f[1] === 'DEVICE' && f.length === 7 && f.slice(2, 6).every(v => /^\d+$/.test(v))) {
    return { kind: 'device', id: +f[2], online: !!+f[3], rx_cables: +f[4], tx_cables: +f[5], name: decodeHexName(f[6]) };
  }
  if (f[1] === 'RULE' && f.length === 8 && f.slice(2).every(v => /^\d+$/.test(v))) {
    const n = f.slice(2).map(Number);
    return { kind: 'rule', source_id: n[0], source_cable: n[1], channel: n[2], action: n[3], destination_id: n[4], destination_cable: n[5] };
  }
  return null;
}

export function parseSnapshot(lines) {
  const snapshot = { default: null, devices: [], rules: [] };
  for (const line of lines) {
    if (line === 'R END') {
      if (snapshot.default === null) throw new Error('Board ended the response without a configuration record.');
      return snapshot;
    }
    const record = parseRecord(line);
    if (!record) continue; // Welcome, help, and MIDI activity are allowed between records.
    if (record.kind === 'config') snapshot.default = record.default;
    else if (record.kind === 'device') snapshot.devices.push(record);
    else snapshot.rules.push(record);
  }
  throw new Error('Board response ended unexpectedly.');
}

export function parseMidiPacket(bytes) {
  if (!bytes?.length) return 'empty';
  const status = bytes[0];
  if (status === 0xf8) return 'Clock';
  if (status === 0xfa) return 'Start';
  if (status === 0xfb) return 'Continue';
  if (status === 0xfc) return 'Stop';
  if (status === 0xfe) return 'Active sensing';
  if (status === 0xff) return 'System reset';
  if (status < 0x80) return 'Data';
  const channel = (status & 0x0f) + 1;
  const kind = status & 0xf0;
  const names = { 0x80: 'Note off', 0x90: bytes[2] === 0 ? 'Note off' : 'Note on', 0xa0: 'Poly pressure', 0xb0: 'Control change', 0xc0: 'Program change', 0xd0: 'Channel pressure', 0xe0: 'Pitch bend' };
  return `${names[kind] || 'System'}${status < 0xf0 ? ` · ch ${channel}` : ''}${bytes.length > 1 ? ` · ${bytes[1]}` : ''}`;
}

export function midiName(note) {
  return `${['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'][note % 12]}${Math.floor(note / 12) - 1}`;
}

export function normalizeRule(rule, devices = []) {
  if (rule.action !== 3) return rule;
  if (!Number.isInteger(rule.source_id) || rule.source_id === 0 ||
      !devices.some(device => device.id === rule.source_id)) {
    throw new Error('Exclude cable requires a remembered device.');
  }
  if (!Number.isInteger(rule.source_cable) || rule.source_cable < 0 || rule.source_cable > 15) {
    throw new Error('Exclude cable requires one exact USB cable (0–15).');
  }
  return { ...rule, channel: 255, destination_id: 0, destination_cable: 255 };
}

export function formatMidiBytes(bytes, limit = 256) {
  const shown = Array.from(bytes).slice(0, limit);
  const hex = shown.map(byte => byte.toString(16).padStart(2, '0')).join(' ').toUpperCase();
  const omitted = bytes.length - shown.length;
  return omitted > 0 ? `${hex} … +${omitted} bytes` : hex;
}

export const LED_COLOR_FIELDS = [
  ['ready', 'Ready'],
  ['hub', 'Hub'],
  ['midi', 'MIDI mounted'],
  ['mouse', 'Mouse'],
  ['traffic', 'MIDI traffic'],
  ['bootsel', 'BOOTSEL'],
  ['error', 'Error'],
];

export const DEFAULT_LED_SETTINGS = {
  pulse_ms: 8,
  phase_ms: 250,
  hold_ms: 1500,
  ready: '0C0C0C',
  hub: '000812',
  midi: '000300',
  mouse: '001212',
  traffic: '120A00',
  bootsel: '120012',
  error: '0C0000',
};

export function validateLedSettings(settings) {
  if (!Number.isInteger(settings.pulse_ms) || settings.pulse_ms < 2 || settings.pulse_ms > 100) {
    throw new Error('MIDI pulse must be 2–100 ms.');
  }
  if (!Number.isInteger(settings.phase_ms) || settings.phase_ms < 10 || settings.phase_ms > 1000) {
    throw new Error('Mouse phase must be 10–1000 ms.');
  }
  if (!Number.isInteger(settings.hold_ms) || settings.hold_ms < settings.phase_ms || settings.hold_ms > 5000) {
    throw new Error('Mouse hold must be at least the phase and no more than 5000 ms.');
  }
  for (const [field, label] of LED_COLOR_FIELDS) {
    if (typeof settings[field] !== 'string' || !/^[0-9a-f]{6}$/i.test(settings[field])) {
      throw new Error(`${label} color must be six hexadecimal digits.`);
    }
  }
}

export function parseLedSettings(line) {
  const fields = line.trim().split(/\s+/);
  if (fields.length !== 12 || fields[0] !== 'R' || fields[1] !== 'LED' ||
      fields.slice(2, 5).some(value => !/^\d+$/.test(value))) {
    throw new Error('Board returned malformed LED settings.');
  }
  const settings = {
    pulse_ms: Number(fields[2]),
    phase_ms: Number(fields[3]),
    hold_ms: Number(fields[4]),
  };
  LED_COLOR_FIELDS.forEach(([field], index) => { settings[field] = fields[5 + index].toUpperCase(); });
  validateLedSettings(settings);
  return settings;
}

export function serializeLedSettings(settings) {
  validateLedSettings(settings);
  const values = [settings.pulse_ms, settings.phase_ms, settings.hold_ms,
    ...LED_COLOR_FIELDS.map(([field]) => settings[field].toUpperCase())];
  return values.join(' ');
}

export class SerialBridge {
  constructor(port, { onLine = () => {}, onState = () => {} } = {}) {
    this.port = port;
    this.onLine = onLine;
    this.onState = onState;
    this.lines = [];
    this.pending = null;
    this.closed = false;
    this.textBuffer = '';
    this.decoder = new TextDecoder();
    this.maxLine = 256;
    this.maxLines = 512;
  }
  async open() {
    await this.port.open({ baudRate: 115200, bufferSize: 512 });
    this.writer = this.port.writable.getWriter();
    this.reader = this.port.readable.getReader();
    await this.port.setSignals({ dataTerminalReady: true });
    this.onState('Connected');
    this.readLoop();
  }
  async readLoop() {
    try {
      while (!this.closed) {
        const { value, done } = await this.reader.read();
        if (done) throw new Error('Serial port disconnected.');
        this.textBuffer += this.decoder.decode(value, { stream: true });
        if (this.textBuffer.length > this.maxLine * 4) this.textBuffer = this.textBuffer.slice(-this.maxLine * 2);
        let nl;
        while ((nl = this.textBuffer.indexOf('\n')) !== -1) {
          const line = this.textBuffer.slice(0, nl).replace(/\r$/, '').slice(0, this.maxLine);
          this.textBuffer = this.textBuffer.slice(nl + 1);
          this.dispatchLine(line);
        }
      }
    } catch (error) {
      if (!this.closed) {
        this.closed = true;
        if (this.pending) { this.pending.reject(error); this.pending = null; }
        this.onLine(`! Serial read failed: ${error.message}`);
        this.onState('Disconnected');
      }
    }
  }
  dispatchLine(line) {
    this.onLine(line);
    if (this.pending) {
      const pending = this.pending;
      pending.lines.push(line);
      if (pending.lines.length > this.maxLines) pending.reject(new Error('Board response exceeded 512 lines.'));
      else if (line.startsWith('R ERR ')) pending.reject(new Error(line));
      else if (typeof pending.terminator === 'function' ? pending.terminator(line) : line === pending.terminator) pending.resolve(line);
      else if (line === 'R END' || line === 'R OK') pending.reject(new Error(`Unexpected response terminator ${line}.`));
    }
  }
  async write(line) {
    if (this.closed || !this.writer) throw new Error('CDC connection is closed.');
    if (line.length > 120 || /[\r\n]/.test(line)) throw new Error('Command is too long or contains a newline.');
    await this.writer.write(new TextEncoder().encode(`${line}\n`));
  }
  async commandUntil(command, terminator, timeout = 4000) {
    if (this.pending) throw new Error('Another CDC command is in progress.');
    let pendingEntry;
    let timer;
    const expected = typeof terminator === 'string' ? terminator : `a response to ${command}`;
    const promise = new Promise((resolve, reject) => {
      pendingEntry = {
        lines: [],
        terminator,
        resolve: value => { clearTimeout(timer); if (this.pending === pendingEntry) this.pending = null; resolve(value); },
        reject: error => { clearTimeout(timer); if (this.pending === pendingEntry) this.pending = null; reject(error); },
      };
      this.pending = pendingEntry;
      timer = setTimeout(() => pendingEntry.reject(new Error(`Timed out waiting for ${expected}.`)), timeout);
    });
    try { await this.write(command); }
    catch (error) { pendingEntry.reject(error); await promise.catch(() => {}); throw error; }
    return promise;
  }
  async getLedSettings() {
    const line = await this.commandUntil('R LED GET', value => /^R LED \d+\s/.test(value), 1500);
    return parseLedSettings(line);
  }
  async setLedSettings(settings) {
    const values = serializeLedSettings(settings);
    await this.commandUntil(`R LED SET ${values}`, 'R OK', 10000);
  }
  async getSnapshot() {
    if (this.pending) throw new Error('Another CDC command is in progress.');
    let pendingEntry;
    let timer;
    const response = new Promise((resolve, reject) => {
      pendingEntry = { lines: [], terminator: 'R END', resolve: () => {
        clearTimeout(timer);
        const lines = pendingEntry.lines;
        if (this.pending === pendingEntry) this.pending = null;
        try { resolve(parseSnapshot(lines)); } catch (error) { reject(error); }
      }, reject: error => { clearTimeout(timer); if (this.pending === pendingEntry) this.pending = null; reject(error); } };
      this.pending = pendingEntry;
      timer = setTimeout(() => pendingEntry.reject(new Error('Timed out waiting for R END.')), 5000);
    });
    try { await this.write('R GET'); }
    catch (error) { pendingEntry.reject(error); await response.catch(() => {}); throw error; }
    return response;
  }
  async sendRaw(command) {
    if (/^R\s+(?:GET|BEGIN|RULE|COMMIT|ABORT|LED)(?:\s|$)/i.test(command)) throw new Error('Managed bridge commands are sent by the configurator.');
    await this.write(command);
  }
  async applySnapshot(snapshot) {
    const rules = snapshot.rules.map(rule => normalizeRule(rule, snapshot.devices || []));
    await this.commandUntil(`R BEGIN ${snapshot.default}`, 'R OK');
    try {
      for (const rule of rules) {
        await this.commandUntil(`R RULE ${rule.source_id} ${rule.source_cable} ${rule.channel} ${rule.action} ${rule.destination_id} ${rule.destination_cable}`, 'R OK');
      }
      await this.commandUntil('R COMMIT', 'R OK');
    } catch (error) {
      try { await this.commandUntil('R ABORT', 'R OK'); } catch { /* timeout/disconnect clears board session */ }
      throw error;
    }
  }
  async close() {
    if (this.closed) return;
    this.closed = true;
    if (this.pending) { this.pending.reject(new Error('CDC disconnected.')); this.pending = null; }
    try { await this.reader?.cancel(); } catch { /* already closed */ }
    try { this.reader?.releaseLock(); } catch { /* already released */ }
    try { await this.writer?.close(); } catch { /* USB detached */ }
    try { this.writer?.releaseLock(); } catch { /* already released */ }
    try { await this.port.close(); } catch { /* USB detached */ }
    this.onState('Disconnected');
  }
}
