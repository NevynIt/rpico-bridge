import React, { useEffect, useMemo, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import styles from './style.css';
import {
  SerialBridge, parseSnapshot, parseMidiPacket, formatMidiBytes, midiName,
  LED_COLOR_FIELDS, DEFAULT_LED_SETTINGS, validateLedSettings,
} from './protocol.mjs';
import { DOCS_PAGES } from './docsContent.js';

const h = React.createElement;
const initial = { default: 0, devices: [], rules: [] };
const line = (...children) => h('div', { className: 'row' }, ...children);
const badge = (text, kind = '') => h('span', { className: `badge ${kind}` }, text);
const actionNames = ['Route all', 'Drop', 'Route to', 'Exclude cable'];
const any = 255;
const WHITE_KEYS = [
  ['KeyA', -1], ['KeyS', 0], ['KeyD', 2], ['KeyF', 4], ['KeyG', 5],
  ['KeyH', 7], ['KeyJ', 9], ['KeyK', 11], ['KeyL', 12], ['Semicolon', 14],
];
const BLACK_KEYS = [
  ['KeyE', 1, 20], ['KeyR', 3, 30], ['KeyY', 6, 50],
  ['KeyU', 8, 60], ['KeyI', 10, 70], ['KeyP', 13, 90],
];
const consoleLineKind = value => {
  const text = value.trim();
  if (/^midi host=\d+ rx=\d+ tx=\d+ drops=\d+ invalid=\d+$/.test(text)) return 'status';
  if (/^M [0-9A-Fa-f]{8}$/.test(text)) return 'midi';
  return 'normal';
};

function App() {
  const [serial, setSerial] = useState('Disconnected');
  const [serialNote, setSerialNote] = useState('');
  const [midi, setMidi] = useState('Unavailable');
  const [midiInputs, setMidiInputs] = useState([]);
  const [midiOutputs, setMidiOutputs] = useState([]);
  const [inputId, setInputId] = useState('');
  const [outputId, setOutputId] = useState('');
  const [snapshot, setSnapshot] = useState(initial);
  const [working, setWorking] = useState(false);
  const [events, setEvents] = useState([]);
  const [consoleLines, setConsoleLines] = useState([]);
  const [command, setCommand] = useState('help');
  const [channel, setChannel] = useState(1);
  const [keyboardCapture, setKeyboardCapture] = useState(false);
  const [octave, setOctave] = useState(4);
  const [logs, setLogs] = useState(true);
  const [showStatusLines, setShowStatusLines] = useState(false);
  const [showMidiLines, setShowMidiLines] = useState(false);
  const [page, setPage] = useState('config');
  const [ledSettings, setLedSettings] = useState({ ...DEFAULT_LED_SETTINGS });
  const [ledSaved, setLedSaved] = useState(null);
  const [ledSupported, setLedSupported] = useState(false);
  const [ledBusy, setLedBusy] = useState(false);
  const [ledNote, setLedNote] = useState('Connect the board to read its LED settings.');
  const bridge = useRef(null);
  const midiAccess = useRef(null);
  const outputRef = useRef(null);
  const held = useRef(new Map());
  const octaveRef = useRef(octave);
  const channelRef = useRef(channel);
  const logsRef = useRef(logs);
  const showStatusRef = useRef(showStatusLines);
  const showMidiRef = useRef(showMidiLines);
  octaveRef.current = octave;
  channelRef.current = channel;
  logsRef.current = logs;
  showStatusRef.current = showStatusLines;
  showMidiRef.current = showMidiLines;
  const autoNames = ['USB MIDI 1.0 Bridge', 'rpico-bridge'];
  const ledDirty = ledSaved !== null && JSON.stringify(ledSettings) !== JSON.stringify(ledSaved);

  const addEvent = (origin, bytes) => {
    const detail = parseMidiPacket(bytes);
    const now = new Date().toLocaleTimeString();
    setEvents(old => [{ now, origin, raw: formatMidiBytes(bytes), detail }, ...old].slice(0, 180));
  };
  const addConsole = value => {
    const kind = consoleLineKind(value);
    if ((kind === 'status' && !showStatusRef.current) || (kind === 'midi' && !showMidiRef.current)) return;
    setConsoleLines(old => [...old, value].slice(-80));
  };
  const refreshMidi = access => {
    const inputs = [...access.inputs.values()];
    const outputs = [...access.outputs.values()];
    setMidiInputs(inputs);
    setMidiOutputs(outputs);
    const matches = outputs.filter(p => autoNames.some(n => p.name?.toLowerCase().includes(n.toLowerCase())));
    if (!outputRef.current && matches.length === 1) {
      setOutputId(matches[0].id);
      outputRef.current = matches[0];
    }
    if (!inputId) {
      const inMatches = inputs.filter(p => autoNames.some(n => p.name?.toLowerCase().includes(n.toLowerCase())));
      if (inMatches.length === 1) setInputId(inMatches[0].id);
    }
  };
  useEffect(() => {
    const supported = !!navigator.requestMIDIAccess;
    setMidi(supported ? 'Click to enable' : 'Web MIDI unavailable');
    return () => { releaseAll(); disconnectSerial(false); };
  }, []);
  useEffect(() => {
    outputRef.current = midiOutputs.find(p => p.id === outputId) || null;
    releaseAll();
  }, [outputId, midiOutputs]);
  useEffect(() => {
    const input = midiInputs.find(p => p.id === inputId);
    if (!input) return;
    input.onmidimessage = event => {
      if (!logs) return;
      const bytes = Array.from(event.data);
      addEvent(input.name || 'MIDI input', bytes);
    };
    return () => { input.onmidimessage = null; };
  }, [inputId, midiInputs, logs]);
  useEffect(() => {
    const blur = () => releaseAll();
    const visibility = () => { if (document.visibilityState !== 'visible') releaseAll(); };
    window.addEventListener('blur', blur);
    document.addEventListener('visibilitychange', visibility);
    return () => {
      window.removeEventListener('blur', blur);
      document.removeEventListener('visibilitychange', visibility);
    };
  }, []);

  useEffect(() => {
    if (!keyboardCapture || page !== 'config') return;
    const pitches = new Map([...WHITE_KEYS, ...BLACK_KEYS].map(([code, offset]) => [code, offset]));
    const isEditable = target => {
      if (!(target instanceof Element)) return false;
      const control = target.closest('input, textarea, select, [contenteditable="true"], [role="textbox"]');
      if (!control) return false;
      if (control instanceof HTMLInputElement && ['checkbox', 'radio', 'button', 'submit', 'reset', 'color', 'range'].includes(control.type)) return false;
      return true;
    };
    const down = event => {
      if (event.repeat || event.shiftKey || event.ctrlKey || event.altKey || event.metaKey || isEditable(event.target)) return;
      if (/^Digit[1-8]$/.test(event.code)) {
        setOctave(Number(event.code.slice(5)));
        event.preventDefault();
        return;
      }
      if (event.code === 'KeyZ' || event.code === 'KeyX') {
        setOctave(current => Math.max(1, Math.min(8, current + (event.code === 'KeyZ' ? -1 : 1))));
        event.preventDefault();
        return;
      }
      const offset = pitches.get(event.code);
      if (offset === undefined) return;
      event.preventDefault();
      noteOn(`kbd:${event.code}`, (octaveRef.current + 1) * 12 + offset);
    };
    const up = event => {
      if (!pitches.has(event.code)) return;
      if (!held.current.has(`kbd:${event.code}`)) return;
      event.preventDefault();
      noteOff(`kbd:${event.code}`);
    };
    window.addEventListener('keydown', down);
    window.addEventListener('keyup', up);
    return () => {
      window.removeEventListener('keydown', down);
      window.removeEventListener('keyup', up);
      releaseAll('kbd:');
    };
  }, [keyboardCapture, page]);

  function releaseAll(prefix = '') {
    for (const key of [...held.current.keys()]) {
      if (prefix && !key.startsWith(prefix)) continue;
      noteOff(key);
    }
  }
  async function connectMidi() {
    try {
      if (!navigator.requestMIDIAccess) throw new Error('This browser does not provide Web MIDI. Use a Chromium-based browser on HTTPS or localhost.');
      const access = await navigator.requestMIDIAccess({ sysex: true });
      midiAccess.current = access;
      refreshMidi(access);
      access.onstatechange = () => refreshMidi(access);
      setMidi('Connected');
    } catch (error) { setMidi(error.message || 'MIDI access denied'); }
  }
  async function connectSerial() {
    try {
      if (!('serial' in navigator)) throw new Error('Web Serial is unavailable. Open this file in a supported Chromium browser using HTTPS or localhost.');
      if (!window.isSecureContext) throw new Error('Browser serial access needs a secure context. Use HTTPS or localhost.');
      const port = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x1209, usbProductId: 0x0001 }] });
      const instance = new SerialBridge(port, {
        onLine: text => { addConsole(text); },
        onState: text => setSerial(text),
      });
      bridge.current = instance;
      await instance.open();
      setSerialNote('Choose a command or type help. Device welcome and help text appear here.');
      await refreshRoutes(instance);
      await loadLedSettings(instance);
    } catch (error) {
      setSerial(error.message === 'The user aborted a request.' ? 'Disconnected' : 'Error');
      setSerialNote(error.message || 'Could not connect to the board.');
      setLedBusy(false);
    }
  }
  async function loadLedSettings(target) {
    setLedBusy(true);
    setLedSupported(false);
    setLedSaved(null);
    setLedNote('Reading settings…');
    try {
      const settings = await target.getLedSettings();
      setLedSettings(settings);
      setLedSaved({ ...settings });
      setLedSupported(true);
      setLedNote('Settings loaded. Changes stay local until saved.');
    } catch (error) {
      const unavailable = error.message.startsWith('R ERR ') || error.message.includes('Timed out');
      setLedNote(unavailable
        ? 'LED settings are unavailable on this firmware. Routing controls remain available.'
        : `Could not load LED settings. ${error.message}`);
    } finally { setLedBusy(false); }
  }
  async function saveLedSettings() {
    if (!bridge.current || !ledSupported) return;
    try {
      validateLedSettings(ledSettings);
      setLedBusy(true);
      setLedNote('Saving settings…');
      await bridge.current.setLedSettings(ledSettings);
      setLedSaved({ ...ledSettings });
      setLedNote('Saved on the board. It may restart; reconnect if CDC disconnects.');
    } catch (error) { setLedNote(`Save failed. ${error.message}`); }
    finally { setLedBusy(false); }
  }
  async function disconnectSerial(show = true) {
    const current = bridge.current;
    bridge.current = null;
    if (current) await current.close();
    if (show) setSerial('Disconnected');
  }
  async function refreshRoutes(target = bridge.current) {
    if (!target || working || ledBusy) return;
    setWorking(true);
    try { setSnapshot(await target.getSnapshot()); setSerialNote('Device inventory and saved route rules loaded.'); }
    catch (error) { setSerialNote(error.message); }
    finally { setWorking(false); }
  }
  async function sendCommand(event) {
    event.preventDefault();
    const text = command.trim();
    if (!text || !bridge.current) return;
    addConsole(`> ${text}`);
    setCommand('');
    try { await bridge.current.sendRaw(text); }
    catch (error) { addConsole(`! ${error.message}`); }
  }
  async function commit() {
    if (!bridge.current) { setSerialNote('Connect to the board before saving rules.'); return; }
    if (ledBusy) { setSerialNote('Wait for the LED settings request to finish.'); return; }
    if (snapshot.rules.length > 24) { setSerialNote('The board supports up to 24 ordered rules.'); return; }
    setWorking(true);
    try {
      await bridge.current.applySnapshot(snapshot);
      setSerialNote('Rules saved to the board and active.');
      await refreshRoutesAfterCommit();
    } catch (error) { setSerialNote(`Save failed. ${error.message}`); }
    finally { setWorking(false); }
  }
  async function refreshRoutesAfterCommit() {
    try { setSnapshot(await bridge.current.getSnapshot()); } catch { /* keep edited view and report commit result */ }
  }
  function changeRule(index, patch) {
    setSnapshot(s => ({ ...s, rules: s.rules.map((rule, i) => i === index ? { ...rule, ...patch } : rule) }));
  }
  function changeRuleAction(index, rule, action) {
    if (action !== 3) { changeRule(index, { action }); return; }
    const knownSource = snapshot.devices.some(device => device.id === rule.source_id);
    const exactCable = Number.isInteger(rule.source_cable) && rule.source_cable >= 0 && rule.source_cable <= 15;
    changeRule(index, {
      action,
      source_id: knownSource ? rule.source_id : (snapshot.devices[0]?.id || 0),
      source_cable: exactCable ? rule.source_cable : 0,
      channel: any,
      destination_id: 0,
      destination_cable: any,
    });
  }
  function addRule() {
    setSnapshot(s => ({ ...s, rules: [...s.rules, { source_id: 0, source_cable: any, channel: any, action: 0, destination_id: 1, destination_cable: any }] }));
  }
  function moveRule(index, delta) {
    setSnapshot(s => { const rules = [...s.rules]; const to = index + delta; if (to < 0 || to >= rules.length) return s; [rules[index], rules[to]] = [rules[to], rules[index]]; return { ...s, rules }; });
  }
  function noteOn(key, note) {
    if (held.current.has(key)) return;
    const output = outputRef.current;
    if (!output) { setMidi('Select the bridge MIDI output'); return; }
      const ch = Number(channelRef.current) - 1;
    try {
      const bytes = [0x90 | ch, note, 96];
      const alreadyHeld = [...held.current.values()].some(item => item.output === output && item.channel === ch && item.note === note);
      if (!alreadyHeld) output.send(bytes);
      held.current.set(key, { output, channel: ch, note });
      if (logsRef.current && !alreadyHeld) addEvent('Keyboard', bytes);
    }
    catch (error) { setMidi(`MIDI send failed: ${error.message}`); }
  }
  function noteOff(key) {
    const item = held.current.get(key);
    if (!item) return;
    held.current.delete(key);
    const stillHeld = [...held.current.values()].some(other => other.output === item.output && other.channel === item.channel && other.note === item.note);
    if (stillHeld) return;
    const bytes = [0x80 | item.channel, item.note, 0];
    try { item.output.send(bytes); if (logsRef.current) addEvent('Keyboard', bytes); } catch { /* disconnected */ }
  }
  function openDocs() {
    releaseAll();
    setPage('docs');
  }
  const deviceLabel = id => id === 0 ? 'Any device' : id === 1 ? 'Native USB' : id === 2 ? 'CDC console' : snapshot.devices.find(d => d.id === id)?.name || `Device ${id}`;
  const inputSelect = h('select', { value: inputId, onChange: e => setInputId(e.target.value) }, h('option', { value: '' }, 'No input selected'), ...midiInputs.map(p => h('option', { key: p.id, value: p.id }, p.name || p.id)));
  const outputSelect = h('select', { value: outputId, onChange: e => setOutputId(e.target.value) }, h('option', { value: '' }, 'No output selected'), ...midiOutputs.map(p => h('option', { key: p.id, value: p.id }, p.name || p.id)));
  const visibleConsoleLines = consoleLines.filter(value => {
    const kind = consoleLineKind(value);
    return kind === 'normal' || (kind === 'status' ? showStatusLines : showMidiLines);
  });

  return h('main', { className: 'shell' },
    h('header', { className: 'topbar' },
      h('div', { className: 'brand' }, h('span', { className: 'brand-mark' }, 'r'), h('div', null, h('strong', null, 'rpico-bridge'), h('small', null, 'DEVICE CONFIGURATOR'))),
      h('div', { className: 'topbar-right' },
        line(badge(`CDC · ${serial}`, serial === 'Connected' ? 'good' : ''), badge(`MIDI · ${midi}`, midi === 'Connected' ? 'good' : '')),
        h('nav', { className: 'page-nav', 'aria-label': 'Configurator pages' },
          h('button', { className: page === 'config' ? 'active' : '', onClick: () => setPage('config'), 'data-testid': 'config-tab' }, 'Configure'),
          h('button', { className: page === 'docs' ? 'active' : '', onClick: openDocs, 'data-testid': 'docs-tab' }, 'Documentation')))),
    h('section', { className: 'intro' }, h('div', null, h('h1', null, 'Configure the bridge.'), h('p', { className: 'subhead' }, 'Connect over CDC to inspect devices and save ordered routing rules. MIDI monitoring and the keyboard use the browser’s native MIDI access.')), h('div', { className: 'intro-actions' }, h('button', { className: 'primary', onClick: connectSerial, disabled: serial === 'Connected' }, serial === 'Connected' ? 'Board connected' : 'Connect board'), serial === 'Connected' && h('button', { className: 'quiet', onClick: () => disconnectSerial() }, 'Disconnect'))),
    serialNote && h('div', { className: 'notice' }, serialNote),
    page === 'config' ? h('div', { className: 'grid' },
      h('section', { className: 'panel inventory' },
        h('div', { className: 'panel-head' }, h('div', null, h('h2', null, 'Connected devices')), h('button', { className: 'icon-button', onClick: () => refreshRoutes(), disabled: working || ledBusy || serial !== 'Connected', title: 'Refresh devices' }, '↻')),
        h('p', { className: 'muted' }, 'Saved identities remain listed while devices are offline.'),
        snapshot.devices.length ? h('div', { className: 'device-list' }, ...snapshot.devices.map(d => h('div', { className: 'device', key: d.id }, h('span', { className: `dot ${d.online ? 'online' : ''}` }), h('div', { className: 'device-name' }, h('strong', null, d.name || `Device ${d.id}`), h('small', null, `ID ${d.id} · ${d.rx_cables} in / ${d.tx_cables} out cables`)), badge(d.online ? 'ONLINE' : 'OFFLINE', d.online ? 'good' : '')))) : h('div', { className: 'empty' }, 'Connect the board to discover remembered MIDI devices.'),
        h('div', { className: 'panel-foot' }, `${snapshot.devices.filter(d => d.online).length} online`, h('span', null, `${snapshot.devices.length} remembered`))),
      h('section', { className: 'panel rules' },
        h('div', { className: 'panel-head' }, h('div', null, h('h2', null, 'Routing rules')), badge(`${snapshot.rules.length} / 24`, 'count')),
        h('p', { className: 'muted' }, 'Rules run top to bottom. First matching rule wins.'),
        h('p', { className: 'exclude-note' }, 'Exclude cable blocks both input and output for that remembered device cable before all rules. Rule order cannot override it.'),
        h('div', { className: 'default-row' }, h('label', null, 'If no rule matches'), h('select', { value: snapshot.default, onChange: e => setSnapshot(s => ({ ...s, default: Number(e.target.value) })) }, h('option', { value: 0 }, 'Route to all other ports'), h('option', { value: 1 }, 'Drop'))),
        h('div', { className: 'rule-list' }, snapshot.rules.length ? snapshot.rules.map((rule, i) => h('article', { className: 'rule', key: i },
          h('div', { className: 'rule-order' }, h('span', null, String(i + 1).padStart(2, '0')), h('button', { onClick: () => moveRule(i, -1), disabled: i === 0, title: 'Move up' }, '↑'), h('button', { onClick: () => moveRule(i, 1), disabled: i === snapshot.rules.length - 1, title: 'Move down' }, '↓')),
          h('div', { className: 'rule-fields' },
            line(h('label', null, 'From', h('select', { value: rule.source_id, onChange: e => changeRule(i, { source_id: Number(e.target.value) }) }, rule.action === 3 ? h('option', { value: 0, disabled: true }, 'Choose remembered device') : h('option', { value: 0 }, 'Any device'), ...(rule.action === 3 ? [] : [h('option', { value: 1 }, 'Native USB'), h('option', { value: 2 }, 'CDC console')]), ...snapshot.devices.map(d => h('option', { key: d.id, value: d.id }, d.name || `Device ${d.id}`)))), h('label', null, 'Cable', h('select', { value: rule.source_cable, onChange: e => changeRule(i, { source_cable: Number(e.target.value) }) }, rule.action === 3 ? h('option', { value: any, disabled: true }, 'Choose cable') : h('option', { value: any }, 'Any'), ...Array.from({ length: 16 }, (_, n) => h('option', { key: n, value: n }, String(n))))), rule.action !== 3 && h('label', null, 'Channel', h('select', { value: rule.channel, onChange: e => changeRule(i, { channel: Number(e.target.value) }) }, h('option', { value: any }, 'Any'), ...Array.from({ length: 16 }, (_, n) => h('option', { key: n, value: n }, `Ch ${n + 1}`)), h('option', { value: 16 }, 'System')))),
            line(h('label', null, 'Action', h('select', { value: rule.action, onChange: e => changeRuleAction(i, rule, Number(e.target.value)) }, ...actionNames.map((name, id) => h('option', { key: id, value: id }, name)))), rule.action === 2 && h('label', null, 'Destination', h('select', { value: rule.destination_id, onChange: e => changeRule(i, { destination_id: Number(e.target.value) }) }, h('option', { value: 0 }, 'Choose destination'), h('option', { value: 1 }, 'Native USB'), h('option', { value: 2 }, 'CDC console'), ...snapshot.devices.map(d => h('option', { key: d.id, value: d.id }, d.name || `Device ${d.id}`)))), rule.action === 2 && h('label', null, 'To cable', h('select', { value: rule.destination_cable, onChange: e => changeRule(i, { destination_cable: Number(e.target.value) }) }, h('option', { value: any }, 'Any'), ...Array.from({ length: 16 }, (_, n) => h('option', { key: n, value: n }, String(n))))))),
          h('button', { className: 'remove', onClick: () => setSnapshot(s => ({ ...s, rules: s.rules.filter((_, j) => i !== j) })), title: 'Remove rule' }, '×')))
          : h('div', { className: 'empty compact' }, 'No rules yet. The default action applies to every message.')),
        h('div', { className: 'rule-actions' }, h('button', { className: 'secondary', onClick: addRule, disabled: snapshot.rules.length >= 24 }, '+ Add rule'), h('button', { className: 'primary', onClick: commit, disabled: working || ledBusy || serial !== 'Connected' }, working ? 'Working…' : 'Save to board'))),
      h('section', { className: 'panel monitor' },
        h('div', { className: 'panel-head' }, h('div', null, h('h2', null, 'MIDI monitor')), h('label', { className: 'toggle' }, h('input', { type: 'checkbox', checked: logs, onChange: e => setLogs(e.target.checked) }), 'Capture')),
        h('div', { className: 'midi-selectors' }, h('label', null, 'Input', inputSelect), h('label', null, 'Output / keyboard', outputSelect), h('button', { className: 'secondary', onClick: connectMidi }, midi === 'Connected' ? 'Refresh MIDI' : 'Enable MIDI')),
        h('div', { className: 'log-head' }, h('span', null, 'LIVE EVENTS'), h('button', { className: 'text-button', onClick: () => setEvents([]) }, 'Clear')),
        h('div', { className: 'event-list' }, logs && events.length ? events.map((event, i) => h('div', { className: 'event', key: `${event.now}-${i}` }, h('time', null, event.now), h('span', { className: 'event-source' }, event.origin), h('code', null, event.raw), h('span', { className: 'event-detail' }, event.detail))) : h('div', { className: 'empty compact' }, logs ? 'Select the bridge input to see incoming MIDI.' : 'Capture paused.')),
        h('div', { className: 'keyboard-head' }, h('strong', null, 'Test keyboard'),
          h('label', { className: 'toggle capture-toggle' }, h('input', { type: 'checkbox', checked: keyboardCapture, onChange: e => setKeyboardCapture(e.target.checked), 'data-testid': 'keyboard-capture' }), 'Computer keys'),
          h('label', null, 'Octave', h('select', { value: octave, onChange: e => setOctave(Number(e.target.value)), 'data-testid': 'keyboard-octave' }, ...Array.from({ length: 8 }, (_, i) => h('option', { key: i + 1, value: i + 1 }, `C${i + 1}`)))),
          h('label', null, 'Channel', h('select', { value: channel, onChange: e => setChannel(e.target.value), 'data-testid': 'keyboard-channel' }, ...Array.from({ length: 16 }, (_, i) => h('option', { key: i, value: i + 1 }, String(i + 1)))))),
        h('div', { className: 'keyboard', 'aria-label': 'Playable B-to-D keyboard' },
          ...WHITE_KEYS.map(([code, offset]) => {
            const note = (octave + 1) * 12 + offset;
            return h('button', { key: code, className: 'key white', 'data-testid': `keyboard-white-${code}`, 'data-code': code, 'data-note': note, onPointerDown: e => { e.preventDefault(); try { e.currentTarget.setPointerCapture(e.pointerId); } catch { /* pointer capture is not available in every test harness */ } noteOn(`ptr:${e.pointerId}`, note); }, onPointerUp: e => noteOff(`ptr:${e.pointerId}`), onPointerCancel: e => noteOff(`ptr:${e.pointerId}`), onLostPointerCapture: e => noteOff(`ptr:${e.pointerId}`), onContextMenu: e => e.preventDefault(), 'aria-label': `Play ${midiName(note)} (${code})` }, h('span', null, midiName(note)));
          }),
          ...BLACK_KEYS.map(([code, offset, position]) => {
            const note = (octave + 1) * 12 + offset;
            return h('button', { key: code, className: 'key black', style: { left: `${position}%` }, 'data-testid': `keyboard-black-${code}`, 'data-code': code, 'data-note': note, onPointerDown: e => { e.preventDefault(); try { e.currentTarget.setPointerCapture(e.pointerId); } catch { /* pointer capture is not available in every test harness */ } noteOn(`ptr:${e.pointerId}`, note); }, onPointerUp: e => noteOff(`ptr:${e.pointerId}`), onPointerCancel: e => noteOff(`ptr:${e.pointerId}`), onLostPointerCapture: e => noteOff(`ptr:${e.pointerId}`), onContextMenu: e => e.preventDefault(), 'aria-label': `Play ${midiName(note)} (${code})` });
          })),
        h('p', { className: 'hint' }, 'Computer keys: A S D F G H J K L ; play B C D E F G A B C D; E R Y U I P play sharps. Digits 1–8 set octave; Z/X move down/up.')),
      h('section', { className: 'panel console' },
        h('div', { className: 'panel-head' }, h('div', null, h('h2', null, 'Serial console')), h('button', { className: 'text-button', onClick: () => setConsoleLines([]) }, 'Clear')),
        h('div', { className: 'console-filters' },
          h('label', { className: 'toggle' }, h('input', { type: 'checkbox', checked: showStatusLines, onChange: e => setShowStatusLines(e.target.checked), 'data-testid': 'console-filter-status' }), 'Show periodic status'),
          h('label', { className: 'toggle' }, h('input', { type: 'checkbox', checked: showMidiLines, onChange: e => setShowMidiLines(e.target.checked), 'data-testid': 'console-filter-midi' }), 'Show raw MIDI')),
        h('div', { className: 'console-body' }, visibleConsoleLines.length ? visibleConsoleLines.map((text, i) => h('div', { key: i, className: text.startsWith('>') ? 'sent' : text.startsWith('!') ? 'error' : '' }, text)) : h('span', { className: 'console-placeholder' }, 'Connect over CDC to see the welcome message and device responses.')),
        h('form', { onSubmit: sendCommand, className: 'command-form' }, h('span', null, '›'), h('input', { value: command, onChange: e => setCommand(e.target.value), placeholder: 'Type help or a CDC command', disabled: serial !== 'Connected' || ledBusy, 'aria-label': 'CDC command' }), h('button', { type: 'submit', disabled: serial !== 'Connected' || ledBusy }, 'Send')),
        h('p', { className: 'hint' }, 'Try help, help rule, or help devices.')),
      h('section', { className: 'panel led-panel' },
        h('div', { className: 'panel-head' }, h('div', null, h('h2', null, 'Status LED')), badge(ledBusy ? 'Working' : ledSupported ? (ledDirty ? 'Unsaved changes' : 'Saved on board') : 'Not loaded', ledDirty ? '' : 'count')),
        h('p', { className: 'muted' }, 'Edit the status colors and activity timings, then save once. The board may restart after saving.'),
        h('p', { className: 'led-note', role: 'status', 'aria-live': 'polite' }, ledNote),
        h('div', { className: 'led-timings' },
          h('label', null, 'MIDI pulse (ms)', h('input', { type: 'number', min: 2, max: 100, step: 1, value: ledSettings.pulse_ms, disabled: !ledSupported || ledBusy, onChange: e => setLedSettings(s => ({ ...s, pulse_ms: Number(e.target.value) })) })),
          h('label', null, 'Mouse phase (ms)', h('input', { type: 'number', min: 10, max: 1000, step: 1, value: ledSettings.phase_ms, disabled: !ledSupported || ledBusy, onChange: e => setLedSettings(s => ({ ...s, phase_ms: Number(e.target.value) })) })),
          h('label', null, 'Mouse hold (ms)', h('input', { type: 'number', min: 10, max: 5000, step: 1, value: ledSettings.hold_ms, disabled: !ledSupported || ledBusy, onChange: e => setLedSettings(s => ({ ...s, hold_ms: Number(e.target.value) })) }))),
        h('div', { className: 'led-colors' }, ...LED_COLOR_FIELDS.map(([field, label]) => h('label', { key: field }, label, h('input', { type: 'color', value: `#${ledSettings[field]}`, disabled: !ledSupported || ledBusy, onChange: e => setLedSettings(s => ({ ...s, [field]: e.target.value.slice(1).toUpperCase() })) }), h('code', null, ledSettings[field])))),
        h('div', { className: 'led-actions' }, h('button', { className: 'primary', onClick: saveLedSettings, disabled: serial !== 'Connected' || !ledSupported || ledBusy || !ledDirty }, ledBusy ? 'Working…' : 'Save LED settings'))),
    ) : h('section', { className: 'panel docs-view', 'data-testid': 'docs-view' },
      ...DOCS_PAGES.map(doc => h('article', { className: 'docs-page', key: doc.id },
        h('h1', null, doc.title),
        ...doc.sections.map(section => h('section', { className: 'docs-section', key: section.heading },
          h('h3', null, section.heading),
          ...(section.paragraphs || []).map((paragraph, i) => h('p', { key: `p${i}` }, paragraph)),
          section.list?.length ? h('ul', null, ...section.list.map((item, i) => h('li', { key: i }, item))) : null,
          ...(section.code || []).map((code, i) => h('code', { key: `c${i}` }, code)),
          section.links?.length ? h('ul', null, ...section.links.map(link => h('li', { key: link.href }, h('a', { href: link.href, target: '_blank', rel: 'noreferrer' }, link.label)))) : null))))),
    h('footer', null, h('span', null, 'rpico-bridge · local-only configurator'), h('span', null, 'No server or cloud connection required')));
}

const styleNode = document.createElement('style');
styleNode.textContent = styles;
document.head.append(styleNode);
createRoot(document.getElementById('root')).render(h(App));
