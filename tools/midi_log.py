#!/usr/bin/env python3
"""List, monitor, and send MIDI 1.0 messages through the host OS MIDI backend."""

from __future__ import annotations

import argparse
import datetime as _datetime
import re
import sys
import time
from pathlib import Path
from typing import Sequence

try:
    import rtmidi
except ImportError:
    rtmidi = None  # type: ignore[assignment]

BRIDGE_MARKERS = ("rpico bridge", "rpico-bridge", "rpico_bridge", "usb midi 1.0 bridge")


def require_rtmidi():
    if rtmidi is None:
        raise RuntimeError(
            "python-rtmidi is not installed; run: python -m pip install -r tools/requirements.txt"
        )
    return rtmidi


def port_names(direction: str) -> list[str]:
    midi = require_rtmidi()
    client = midi.MidiIn() if direction == "input" else midi.MidiOut()
    try:
        return list(client.get_ports())
    finally:
        del client


def list_ports() -> int:
    try:
        inputs, outputs = port_names("input"), port_names("output")
    except Exception as exc:
        print(f"MIDI backend error: {exc}", file=sys.stderr)
        return 2
    for label, names in (("Input", inputs), ("Output", outputs)):
        print(f"{label} ports:")
        if not names:
            print("  (none)")
        for index, name in enumerate(names):
            print(f"  {index}: {name}")
    return 0


def choose_port(names: Sequence[str], direction: str, index: int | None) -> tuple[int, str]:
    if index is not None:
        if index < 0 or index >= len(names):
            raise ValueError(f"{direction} port index {index} is out of range; found {len(names)} port(s)")
        return index, names[index]
    matches = [(i, name) for i, name in enumerate(names)
               if any(marker in name.casefold() for marker in BRIDGE_MARKERS)]
    if not matches:
        raise ValueError(f"no rpico-bridge {direction} port found; use 'ports' and specify --port-index")
    if len(matches) != 1:
        found = ", ".join(f"{i}: {name}" for i, name in matches)
        raise ValueError(f"ambiguous rpico-bridge {direction} ports ({found}); specify --port-index")
    return matches[0]


def message_description(data: Sequence[int]) -> str:
    if not data:
        return "Empty"
    status = data[0]
    if status < 0x80:
        return "Running status/data"
    if status < 0xF0:
        channel = (status & 0x0F) + 1
        names = {0x80: "Note Off", 0x90: "Note On", 0xA0: "Poly Pressure",
                 0xB0: "Control Change", 0xC0: "Program Change", 0xD0: "Channel Pressure",
                 0xE0: "Pitch Bend"}
        return f"{names.get(status & 0xF0, 'Channel message')} ch {channel}"
    names = {0xF0: "SysEx", 0xF1: "MIDI Time Code", 0xF2: "Song Position",
             0xF3: "Song Select", 0xF6: "Tune Request", 0xF7: "SysEx End",
             0xF8: "Clock", 0xF9: "Undefined realtime", 0xFA: "Start",
             0xFB: "Continue", 0xFC: "Stop", 0xFD: "Undefined realtime",
             0xFE: "Active Sensing", 0xFF: "System Reset"}
    return names.get(status, "System message")


def format_record(data: Sequence[int], now: float | None = None) -> str:
    stamp = _datetime.datetime.fromtimestamp(
        time.time() if now is None else now
    ).astimezone().isoformat(timespec="milliseconds")
    readable = message_description(data)
    hex_bytes = " ".join(f"{byte:02X}" for byte in data)
    return f"{stamp}  {readable:<24} {hex_bytes}"


def parse_hex_message(raw: str) -> list[int]:
    tokens = raw.split()
    if not tokens:
        raise ValueError("provide MIDI bytes as space-separated hex, for example: 90 3C 64")
    if any(not re.fullmatch(r"(?:0[xX])?[0-9a-fA-F]{1,2}", token) for token in tokens):
        raise ValueError("invalid hex byte; use space-separated bytes such as '90 3C 64'")
    data = [int(token, 16) for token in tokens]
    validate_midi_message(data)
    return data


def validate_midi_message(data: Sequence[int]) -> None:
    if any(byte < 0 or byte > 255 for byte in data):
        raise ValueError("MIDI bytes must be in range 00..FF")
    if not data:
        raise ValueError("MIDI message cannot be empty")
    status = data[0]
    if status < 0x80:
        raise ValueError("a complete MIDI message must start with a status byte")
    if status == 0xF0:
        if len(data) < 2 or data[-1] != 0xF7:
            raise ValueError("SysEx must start with F0 and end with F7")
        if any(byte >= 0x80 for byte in data[1:-1]):
            raise ValueError("SysEx data bytes must be 00..7F")
        return
    if any(byte >= 0x80 for byte in data[1:]):
        raise ValueError("data bytes after the status must be 00..7F")
    if status < 0xF0:
        size = 2 if status & 0xF0 in (0xC0, 0xD0) else 3
    elif status in (0xF1, 0xF3):
        size = 2
    elif status == 0xF2:
        size = 3
    elif status in (0xF6, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF):
        size = 1
    else:
        raise ValueError(f"unsupported standalone MIDI status byte {status:02X}")
    if len(data) != size:
        raise ValueError(f"status {status:02X} requires exactly {size} byte(s)")


def run_monitor(args: argparse.Namespace) -> int:
    midi = require_rtmidi()
    client = None
    log = None
    try:
        client = midi.MidiIn()
        index, name = choose_port(client.get_ports(), "input", args.port_index)
        # Explicitly retain all traffic by default. Flags let users suppress noisy types.
        client.ignore_types(sysex=args.ignore_sysex, timing=args.ignore_clock,
                            active_sense=args.ignore_sensing)
        log = open(args.log_file, "a", encoding="utf-8", buffering=1) if args.log_file else None
        client.open_port(index, "rpico-bridge monitor")
    except (ValueError, RuntimeError) as exc:
        if client is not None:
            try:
                client.close_port()
            except Exception:
                pass
        if log:
            log.close()
        print(str(exc), file=sys.stderr)
        return 2
    except Exception as exc:
        if client is not None:
            try:
                client.close_port()
            except Exception:
                pass
        if log:
            log.close()
        print(f"MIDI backend error: {exc}", file=sys.stderr)
        return 2
    print(f"Monitoring input {index}: {name} (Ctrl+C to stop)", flush=True)
    try:
        while True:
            event = client.get_message()
            if event is None:
                time.sleep(0.005)
                continue
            message, _delta = event
            line = format_record(message)
            print(line, flush=True)
            if log:
                log.write(line + "\n")
    except KeyboardInterrupt:
        print("\nStopped.", file=sys.stderr)
    except Exception as exc:
        print(f"MIDI backend error: {exc}", file=sys.stderr)
        return 2
    finally:
        if client is not None:
            try:
                client.close_port()
            except Exception:
                pass
        if log:
            log.close()
    return 0


def run_send(args: argparse.Namespace) -> int:
    midi = require_rtmidi()
    try:
        if args.note is not None:
            if not 0 <= args.note <= 127 or not 1 <= args.velocity <= 127 or not 1 <= args.channel <= 16:
                raise ValueError("note must be 0..127; velocity must be 1..127; channel must be 1..16")
            if args.hold_ms < 0:
                raise ValueError("hold time must be zero or greater")
            data_messages = [[0x90 | (args.channel - 1), args.note, args.velocity],
                             [0x80 | (args.channel - 1), args.note, 0]]
        else:
            if not args.hex_message:
                raise ValueError("provide --hex or --note")
            data_messages = [parse_hex_message(args.hex_message)]
        client = midi.MidiOut()
        index, name = choose_port(client.get_ports(), "output", args.port_index)
        client.open_port(index, "rpico-bridge send")
        try:
            if args.note is not None:
                client.send_message(data_messages[0])
                try:
                    time.sleep(args.hold_ms / 1000)
                finally:
                    client.send_message(data_messages[1])
            else:
                client.send_message(data_messages[0])
        finally:
            client.close_port()
    except (ValueError, RuntimeError) as exc:
        print(str(exc), file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"MIDI backend error: {exc}", file=sys.stderr)
        return 2
    if args.note is not None:
        print(f"Sent note {args.note} on channel {args.channel} to output {index}: {name}")
    else:
        print(f"Sent {' '.join(f'{b:02X}' for b in data_messages[0])} to output {index}: {name}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("ports", help="list host MIDI input and output ports")
    monitor = sub.add_parser("monitor", help="log messages from a MIDI input port")
    monitor.add_argument("--port-index", type=int, help="explicit input index from 'ports'; default requires one unique rpico-bridge port")
    monitor.add_argument("--log-file", type=Path, help="append the same timestamped records to this file")
    monitor.add_argument("--ignore-sysex", action="store_true", help="suppress SysEx in the backend")
    monitor.add_argument("--ignore-clock", action="store_true", help="suppress MIDI clock and other timing messages")
    monitor.add_argument("--ignore-sensing", action="store_true", help="suppress active sensing")
    send = sub.add_parser("send", help="send one MIDI message or a note pair")
    send.add_argument("--port-index", type=int, help="explicit output index from 'ports'; default requires one unique rpico-bridge port")
    group = send.add_mutually_exclusive_group(required=True)
    group.add_argument("--hex", dest="hex_message", help="one complete MIDI 1.0 message, e.g. '90 3C 64' or 'F8'")
    group.add_argument("--note", type=int, help="send note-on followed by note-off after the hold time")
    send.add_argument("--channel", type=int, default=1, help="note channel 1..16 (default: 1)")
    send.add_argument("--velocity", type=int, default=80, help="note-on velocity 1..127 (default: 80)")
    send.add_argument("--hold-ms", type=int, default=250, help="note duration in milliseconds (default: 250)")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "ports":
            return list_ports()
        if args.command == "monitor":
            return run_monitor(args)
        return run_send(args)
    except KeyboardInterrupt:
        return 130
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
