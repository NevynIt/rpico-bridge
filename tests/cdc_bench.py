#!/usr/bin/env python3
"""Two-way native MIDI <-> CDC peer check for RPICO_CDC_BENCH firmware.

Requires Linux libusb-1.0. Safe by default: USB traffic is only sent with --run.
The runner only opens the exact 1209:0001 device and verifies its product string.
"""

import argparse
import ctypes
import ctypes.util
import sys
import time


VID, PID = 0x1209, 0x0001
PRODUCT = b"USB MIDI 1.0 Bridge"
CDC_OUT, CDC_IN = 0x02, 0x82
MIDI_OUT, MIDI_IN = 0x03, 0x83
INTERFACES = (0, 1, 3)


class DeviceDescriptor(ctypes.Structure):
    _fields_ = [("bLength", ctypes.c_uint8), ("bDescriptorType", ctypes.c_uint8),
                ("bcdUSB", ctypes.c_uint16), ("bDeviceClass", ctypes.c_uint8),
                ("bDeviceSubClass", ctypes.c_uint8), ("bDeviceProtocol", ctypes.c_uint8),
                ("bMaxPacketSize0", ctypes.c_uint8), ("idVendor", ctypes.c_uint16),
                ("idProduct", ctypes.c_uint16), ("bcdDevice", ctypes.c_uint16),
                ("iManufacturer", ctypes.c_uint8), ("iProduct", ctypes.c_uint8),
                ("iSerialNumber", ctypes.c_uint8), ("bNumConfigurations", ctypes.c_uint8)]


def load_libusb():
    name = ctypes.util.find_library("usb-1.0")
    if not name:
        raise RuntimeError("libusb-1.0 was not found")
    lib = ctypes.CDLL(name)
    lib.libusb_init.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    lib.libusb_init.restype = ctypes.c_int
    lib.libusb_get_device_list.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))]
    lib.libusb_get_device_list.restype = ctypes.c_ssize_t
    lib.libusb_free_device_list.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_int]
    lib.libusb_get_device_descriptor.argtypes = [ctypes.c_void_p, ctypes.POINTER(DeviceDescriptor)]
    lib.libusb_get_device_descriptor.restype = ctypes.c_int
    for fn in ("libusb_get_bus_number", "libusb_get_device_address"):
        getattr(lib, fn).argtypes = [ctypes.c_void_p]
        getattr(lib, fn).restype = ctypes.c_uint8
    lib.libusb_open.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    lib.libusb_open.restype = ctypes.c_int
    lib.libusb_get_string_descriptor_ascii.argtypes = [ctypes.c_void_p, ctypes.c_uint8,
                                                         ctypes.POINTER(ctypes.c_uint8), ctypes.c_int]
    lib.libusb_get_string_descriptor_ascii.restype = ctypes.c_int
    for fn in ("libusb_set_auto_detach_kernel_driver", "libusb_claim_interface",
               "libusb_release_interface"):
        getattr(lib, fn).argtypes = [ctypes.c_void_p, ctypes.c_int] if fn != "libusb_set_auto_detach_kernel_driver" else [ctypes.c_void_p, ctypes.c_int]
        getattr(lib, fn).restype = ctypes.c_int
    lib.libusb_control_transfer.argtypes = [ctypes.c_void_p, ctypes.c_uint8, ctypes.c_uint8,
                                             ctypes.c_uint16, ctypes.c_uint16, ctypes.c_void_p,
                                             ctypes.c_uint16, ctypes.c_uint]
    lib.libusb_control_transfer.restype = ctypes.c_int
    for fn in ("libusb_bulk_transfer",):
        getattr(lib, fn).argtypes = [ctypes.c_void_p, ctypes.c_ubyte,
                                      ctypes.POINTER(ctypes.c_ubyte), ctypes.c_int,
                                      ctypes.POINTER(ctypes.c_int), ctypes.c_uint]
        getattr(lib, fn).restype = ctypes.c_int
    lib.libusb_close.argtypes = [ctypes.c_void_p]
    lib.libusb_exit.argtypes = [ctypes.c_void_p]
    return lib


class BenchDevice:
    def __init__(self, lib, timeout_ms, bus, address):
        self.lib, self.timeout = lib, timeout_ms
        self.ctx, self.handle = ctypes.c_void_p(), ctypes.c_void_p()
        self.claimed = []
        self.dtr_set = False
        if lib.libusb_init(ctypes.byref(self.ctx)) != 0:
            raise RuntimeError("libusb_init failed")
        devices = ctypes.POINTER(ctypes.c_void_p)()
        count = lib.libusb_get_device_list(self.ctx, ctypes.byref(devices))
        if count < 0:
            raise RuntimeError("libusb_get_device_list failed")
        matches = []
        try:
            for i in range(count):
                dev = devices[i]
                desc = DeviceDescriptor()
                if lib.libusb_get_device_descriptor(dev, ctypes.byref(desc)) != 0:
                    continue
                if (desc.idVendor, desc.idProduct) != (VID, PID):
                    continue
                dev_bus = lib.libusb_get_bus_number(dev)
                dev_address = lib.libusb_get_device_address(dev)
                if bus is not None and (dev_bus, dev_address) != (bus, address):
                    continue
                handle = ctypes.c_void_p()
                if lib.libusb_open(dev, ctypes.byref(handle)) != 0:
                    continue
                buf = (ctypes.c_uint8 * 128)()
                n = lib.libusb_get_string_descriptor_ascii(handle, desc.iProduct, buf, len(buf))
                if bytes(buf[:max(n, 0)]) != PRODUCT:
                    lib.libusb_close(handle)
                    continue
                matches.append((dev_bus, dev_address, handle))
        finally:
            lib.libusb_free_device_list(devices, 1)
        if len(matches) != 1:
            for _, _, handle in matches:
                lib.libusb_close(handle)
            self.close()
            if matches:
                raise RuntimeError("multiple matching bench devices; refusing to choose one")
            raise RuntimeError("specified 1209:0001 bench device / USB MIDI 1.0 Bridge not found or accessible")
        self.bus, self.address, self.handle = matches[0]

    def control(self, request_type, request, value, index):
        result = self.lib.libusb_control_transfer(self.handle, request_type, request,
                                                  value, index, None, 0, self.timeout)
        if result < 0:
            raise RuntimeError(f"CDC control request failed ({result})")

    def bulk(self, endpoint, payload=b"", size=64, timeout=None):
        data = (ctypes.c_ubyte * max(len(payload), size))()
        for i, byte in enumerate(payload):
            data[i] = byte
        transferred = ctypes.c_int()
        rc = self.lib.libusb_bulk_transfer(self.handle, endpoint, data,
                                            len(payload) if payload else size,
                                            ctypes.byref(transferred), timeout or self.timeout)
        if rc == -7 and not payload:  # LIBUSB_ERROR_TIMEOUT: normal while polling CDC.
            return b""
        if rc < 0:
            raise RuntimeError(f"bulk endpoint 0x{endpoint:02x} failed ({rc})")
        if payload and transferred.value != len(payload):
            raise RuntimeError(f"short write on endpoint 0x{endpoint:02x}: {transferred.value}/{len(payload)} bytes")
        return bytes(data[:transferred.value])

    def open_interfaces(self):
        rc = self.lib.libusb_set_auto_detach_kernel_driver(self.handle, 1)
        if rc != 0:
            raise RuntimeError(f"libusb auto-detach kernel driver failed ({rc})")
        for interface in INTERFACES:
            rc = self.lib.libusb_claim_interface(self.handle, interface)
            if rc != 0:
                raise RuntimeError(f"could not claim USB interface {interface} ({rc})")
            self.claimed.append(interface)
        # CDC ACM SET_CONTROL_LINE_STATE: DTR=1. Line coding is irrelevant.
        self.control(0x21, 0x22, 1, 0)
        self.dtr_set = True

    def close(self):
        if self.handle:
            if self.dtr_set:
                try:
                    self.control(0x21, 0x22, 0, 0)
                except Exception:
                    pass
                self.dtr_set = False
            for interface in reversed(self.claimed):
                self.lib.libusb_release_interface(self.handle, interface)
            self.lib.libusb_close(self.handle)
            self.handle = ctypes.c_void_p()
        if self.ctx:
            self.lib.libusb_exit(self.ctx)
            self.ctx = ctypes.c_void_p()


def read_cdc_packet(dev, expected, deadline):
    linebuf = bytearray()
    wanted = b"M " + expected.hex().upper().encode("ascii")
    while time.monotonic() < deadline:
        chunk = dev.bulk(CDC_IN, size=64, timeout=250)
        for byte in chunk:
            if byte in (10, 13):
                line = bytes(linebuf).strip()
                linebuf.clear()
                if line == wanted:
                    return
            else:
                linebuf.append(byte)
                if len(linebuf) > 128:
                    linebuf.clear()
    raise RuntimeError(f"timed out waiting for CDC packet {wanted.decode()}")


def run(dev, timeout):
    dev.open_interfaces()
    print(f"Opened exact bench device at bus {dev.bus}, address {dev.address}; DTR set.")
    # Cable 0 note-on/off preserve channel and payload; SysEx and realtime exercise CINs.
    packets = [bytes.fromhex(x) for x in (
        "09 90 3C 40", "08 80 3C 00", "04 F0 7D 01", "07 02 03 F7", "0F F8 00 00")]
    try:
        for packet in packets:
            command = b"M " + packet.hex().upper().encode("ascii") + b"\n"
            dev.bulk(CDC_OUT, command)
            received = dev.bulk(MIDI_IN, size=4)
            if received != packet:
                raise RuntimeError(f"CDC->MIDI mismatch: sent {packet.hex()} got {received.hex()}")
            # The CDC-originated event must not return to its own logical port.
            try:
                echoed = read_cdc_packet(dev, packet, time.monotonic() + 0.15)
                raise RuntimeError(f"CDC self-echo observed: {echoed}")
            except RuntimeError as exc:
                if "timed out" not in str(exc):
                    raise
            print(f"PASS CDC->MIDI, no self echo: {packet.hex(' ').upper()}")

            dev.bulk(MIDI_OUT, packet)
            read_cdc_packet(dev, packet, time.monotonic() + timeout)
            print(f"PASS MIDI->CDC: {packet.hex(' ').upper()}")
            unexpected = dev.bulk(MIDI_IN, size=4, timeout=150)
            if unexpected:
                raise RuntimeError(f"native MIDI self-echo observed after MIDI->CDC: {unexpected.hex()}")
            print("PASS MIDI source excluded from native MIDI IN")
    finally:
        dev.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="store_true", help="send test packets; without this, only identify the exact target")
    parser.add_argument("--timeout", type=int, default=1500, help="bulk response timeout in milliseconds")
    parser.add_argument("--bus", type=int, help="USB bus number supplied by the operator")
    parser.add_argument("--address", type=int, help="USB device address supplied by the operator")
    args = parser.parse_args()
    if args.run and (args.bus is None or args.address is None):
        parser.error("--run requires --bus and --address for the exact flashed board")
    if (args.bus is None) != (args.address is None):
        parser.error("--bus and --address must be supplied together")
    lib = load_libusb()
    dev = BenchDevice(lib, args.timeout, args.bus, args.address)
    try:
        print(f"Matched 1209:0001 USB MIDI 1.0 Bridge at bus {dev.bus}, address {dev.address}.")
        if args.run:
            run(dev, args.timeout)
        else:
            print("No interfaces claimed and no MIDI traffic sent. Re-run with --run after bench UF2 is confirmed flashed.")
    finally:
        dev.close()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
