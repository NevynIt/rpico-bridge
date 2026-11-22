#!/usr/bin/env python3
"""Inspect and update rpico-bridge routing over its CDC serial interface."""

import argparse
import json
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None
    list_ports = None


VID = 0x1209
PID = 0x0001
DEFAULT_TIMEOUT = 2.0


class RouteError(Exception):
    pass


def require_serial():
    if serial is None:
        raise RouteError("pyserial is missing; install tools/requirements.txt")


def ports():
    require_serial()
    return list(list_ports.comports())


def choose_port(override=None):
    available = ports()
    if override:
        return override
    matches = [p for p in available if p.vid == VID and p.pid == PID]
    if len(matches) == 1:
        return matches[0].device
    if not matches:
        raise RouteError("bridge CDC port not found (VID 1209, PID 0001); use --port")
    choices = ", ".join(p.device for p in matches)
    raise RouteError(f"multiple bridge CDC ports found ({choices}); use --port")


def decode_name(hex_text):
    try:
        return bytes.fromhex(hex_text).decode("utf-8", errors="replace")
    except ValueError:
        return ""


def parse_record(line):
    fields = line.strip().split()
    if not fields or fields[0] != "R":
        return None
    if len(fields) >= 2 and fields[1] == "DEVICE" and len(fields) == 7:
        try:
            return {"kind": "device", "id": int(fields[2]),
                    "online": bool(int(fields[3])), "rx_cables": int(fields[4]),
                    "tx_cables": int(fields[5]), "name": decode_name(fields[6])}
        except ValueError:
            return None
    if len(fields) == 3 and fields[1] == "CONFIG":
        return {"kind": "config", "default": int(fields[2])}
    if len(fields) == 8 and fields[1] == "RULE":
        try:
            values = [int(v) for v in fields[2:]]
        except ValueError:
            return None
        return {"kind": "rule", "source_id": values[0],
                "source_cable": values[1], "channel": values[2],
                "action": values[3], "destination_id": values[4],
                "destination_cable": values[5]}
    return None


def open_serial(port, timeout=DEFAULT_TIMEOUT):
    require_serial()
    try:
        return serial.Serial(port, baudrate=115200, timeout=0.1,
                             write_timeout=timeout)
    except (serial.SerialException, OSError) as exc:
        raise RouteError(f"cannot open {port}: {exc}") from exc


def send_line(device, text):
    device.write((text + "\n").encode("ascii"))
    device.flush()


def read_line(device, deadline):
    while time.monotonic() < deadline:
        raw = device.readline()
        if raw:
            try:
                return raw.decode("ascii", errors="replace").strip()
            except UnicodeError:
                continue
    raise RouteError("timed out waiting for bridge response")


def wait_ack(device, timeout=DEFAULT_TIMEOUT):
    deadline = time.monotonic() + timeout
    while True:
        line = read_line(device, deadline)
        if line.startswith("R OK"):
            return
        if line.startswith("R ERR"):
            raise RouteError(line)


def get_config(device):
    send_line(device, "R GET")
    deadline = time.monotonic() + DEFAULT_TIMEOUT
    config = None
    devices = []
    rules = []
    while True:
        line = read_line(device, deadline)
        if line == "R END":
            if config is None:
                raise RouteError("bridge ended response without configuration")
            return config, devices, rules
        record = parse_record(line)
        if not record:
            continue
        if record["kind"] == "config":
            config = record["default"]
        elif record["kind"] == "device":
            devices.append(record)
        elif record["kind"] == "rule":
            rules.append(record)


def label_action(action):
    return {0: "all", 1: "drop", 2: "to", 3: "exclude"}.get(
        action, f"unknown:{action}")


def label_match(value, system=False):
    if value == 255:
        return "any"
    if system and value == 16:
        return "system"
    return value + 1 if system else value


def human_config(default, devices, rules):
    by_id = {device["id"]: {"id": device["id"], "name": device["name"]}
             for device in devices}
    readable_rules = []
    for rule in rules:
        readable = {"source": by_id.get(rule["source_id"], {"id": rule["source_id"]}),
                    "source_cable": label_match(rule["source_cable"]),
                    "channel": label_match(rule["channel"], system=True),
                    "action": label_action(rule["action"])}
        if rule["action"] == 2:
            readable["destination"] = by_id.get(
                rule["destination_id"], {"id": rule["destination_id"]})
            readable["destination_cable"] = label_match(rule["destination_cable"])
        readable_rules.append(readable)
    return {"default": label_action(default), "devices": devices, "rules": readable_rules}


def parse_device(value, devices):
    if isinstance(value, dict):
        value = value.get("id")
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    if isinstance(value, str):
        for device in devices:
            if value.lower() == device["name"].lower():
                return device["id"]
        try:
            return int(value)
        except ValueError:
            pass
    raise RouteError(f"unknown device {value!r}; use an ID from the devices table")


def parse_cable(value, optional=False):
    if value is None and optional:
        return 255
    if isinstance(value, str) and value.lower() in ("any", "*"):
        return 255
    try:
        cable = int(value)
    except (TypeError, ValueError) as exc:
        raise RouteError(f"invalid cable {value!r}; use 0..15 or 'any'") from exc
    if not 0 <= cable <= 15:
        raise RouteError(f"invalid cable {value!r}; use 0..15 or 'any'")
    return cable


def parse_channel(value):
    if isinstance(value, str) and value.lower() in ("any", "*"):
        return 255
    if isinstance(value, str) and value.lower() == "system":
        return 16
    try:
        channel = int(value)
    except (TypeError, ValueError) as exc:
        raise RouteError(f"invalid channel {value!r}; use 1..16, 'system', or 'any'") from exc
    if not 1 <= channel <= 16:
        raise RouteError(f"invalid channel {value!r}; use 1..16, 'system', or 'any'")
    return channel - 1


def make_rule(rule, devices):
    action = rule.get("action")
    if action not in ("all", "drop", "to", "exclude"):
        raise RouteError(f"invalid action {action!r}; use all, drop, to, or exclude")
    source_id = parse_device(rule.get("source"), devices)
    if action == "exclude":
        if not any(device["id"] == source_id for device in devices):
            raise RouteError("exclude requires a remembered device as source")
        source_cable = parse_cable(rule.get("source_cable"))
        if source_cable == 255:
            raise RouteError("exclude requires one exact source cable (0..15)")
        return (source_id, source_cable, 255, 3, 0, 255)

    source_cable = parse_cable(rule.get("source_cable", "any"), optional=True)
    channel = parse_channel(rule.get("channel", "any"))
    action_id = {"all": 0, "drop": 1, "to": 2}[action]
    destination_id = 0
    destination_cable = 255
    if action == "to":
        destination_id = parse_device(rule.get("destination"), devices)
        destination_cable = parse_cable(rule.get("destination_cable", "any"), optional=True)
    return (source_id, source_cable, channel, action_id, destination_id,
            destination_cable)


def apply_config(device, config, known_devices):
    if not isinstance(config, dict) or not isinstance(config.get("rules"), list):
        raise RouteError("configuration must be an object with a rules array")
    default = config.get("default")
    if default not in ("all", "drop"):
        raise RouteError("default must be 'all' or 'drop'")
    rows = [make_rule(rule, known_devices) for rule in config["rules"]]
    send_line(device, f"R BEGIN {0 if default == 'all' else 1}")
    wait_ack(device)
    for row in rows:
        send_line(device, "R RULE " + " ".join(str(value) for value in row))
        wait_ack(device)
    send_line(device, "R COMMIT")
    wait_ack(device)


def command(args):
    if args.command == "ports":
        for port in ports():
            marker = " *" if port.vid == VID and port.pid == PID else ""
            print(f"{port.device}\t{port.description or port.name}{marker}")
        return

    port = choose_port(args.port)
    with open_serial(port) as device:
        if args.command == "devices":
            _, devices, _ = get_config(device)
            print(json.dumps(devices, indent=2))
        elif args.command == "show":
            default, devices, rules = get_config(device)
            print(json.dumps(human_config(default, devices, rules), indent=2))
        elif args.command == "apply":
            try:
                config = json.load(args.file)
            except (OSError, json.JSONDecodeError) as exc:
                raise RouteError(f"cannot read JSON configuration: {exc}") from exc
            _, devices, _ = get_config(device)
            apply_config(device, config, devices)
            print("routing configuration applied")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="CDC serial port (auto-detected when unique)")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("ports", help="list serial ports")
    subparsers.add_parser("devices", help="show bridge device IDs and cable counts")
    subparsers.add_parser("show", help="print the current configuration as editable JSON")
    apply_parser = subparsers.add_parser("apply", help="apply a JSON configuration")
    apply_parser.add_argument("file", type=argparse.FileType("r"))
    args = parser.parse_args()
    try:
        command(args)
    except RouteError as exc:
        print(f"midi_routes: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
