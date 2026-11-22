#!/usr/bin/env python3
"""Build a deterministic 512 KiB FAT12 documentation drive image."""

import argparse
import hashlib
import math
import os
from pathlib import Path
import struct
import tempfile


SECTOR_SIZE = 512
TOTAL_SECTORS = 1024
SECTORS_PER_CLUSTER = 1
RESERVED_SECTORS = 1
FAT_COUNT = 2
ROOT_ENTRIES = 224
SECTORS_PER_FAT = 3
ROOT_SECTORS = math.ceil(ROOT_ENTRIES * 32 / SECTOR_SIZE)
ROOT_START = RESERVED_SECTORS + FAT_COUNT * SECTORS_PER_FAT
DATA_START = ROOT_START + ROOT_SECTORS
DATA_CLUSTERS = TOTAL_SECTORS - DATA_START
FAT12_MAX_CLUSTERS = 4084
VOLUME_LABEL = b"RPICOBRIDGE"
FIXED_DATE = 0x0021  # 1980-01-01


def _name_83(filename):
    parts = filename.upper().split(".")
    if len(parts) != 2:
        raise ValueError(f"not an 8.3 filename: {filename!r}")
    stem, extension = parts
    allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$~!#%&-{}()@'`"
    if not stem or len(stem) > 8 or len(extension) > 3:
        raise ValueError(f"not an 8.3 filename: {filename!r}")
    if any(character not in allowed for character in stem + extension):
        raise ValueError(f"invalid character in 8.3 filename: {filename!r}")
    return stem.ljust(8).encode("ascii") + extension.ljust(3).encode("ascii")


def _set_fat12_entry(fat, cluster, value):
    offset = cluster + cluster // 2
    if cluster & 1:
        fat[offset] = (fat[offset] & 0x0F) | ((value << 4) & 0xF0)
        fat[offset + 1] = (value >> 4) & 0xFF
    else:
        fat[offset] = value & 0xFF
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F)


def _get_fat12_entry(fat, cluster):
    offset = cluster + cluster // 2
    if cluster & 1:
        return ((fat[offset] >> 4) | (fat[offset + 1] << 4)) & 0xFFF
    return fat[offset] | ((fat[offset + 1] & 0x0F) << 8)


def _boot_sector(file_entries):
    sector = bytearray(SECTOR_SIZE)
    sector[0:3] = b"\xEB\x3C\x90"
    sector[3:11] = b"RPICOBRD"
    struct.pack_into(
        "<HBHBHHBHHHII",
        sector,
        11,
        SECTOR_SIZE,
        SECTORS_PER_CLUSTER,
        RESERVED_SECTORS,
        FAT_COUNT,
        ROOT_ENTRIES,
        TOTAL_SECTORS,
        0xF8,
        SECTORS_PER_FAT,
        32,  # sectors per track
        1,   # heads
        0,   # hidden sectors
        0,   # total sectors (use the 16-bit field above)
    )
    sector[36] = 0x80  # fixed disk
    sector[38] = 0x29   # extended BPB signature
    digest = hashlib.sha256()
    for filename, content in file_entries:
        digest.update(_name_83(filename))
        digest.update(struct.pack("<I", len(content)))
        digest.update(content)
    serial = int.from_bytes(digest.digest()[:4], "little")
    struct.pack_into("<I", sector, 39, serial)
    sector[43:54] = VOLUME_LABEL
    sector[54:62] = b"FAT12   "
    sector[510:512] = b"\x55\xAA"
    return sector


def _directory_entry(name, first_cluster, size, attributes):
    entry = bytearray(32)
    entry[:11] = name
    entry[11] = attributes
    struct.pack_into("<H", entry, 14, 0)  # creation time
    struct.pack_into("<H", entry, 16, FIXED_DATE)
    struct.pack_into("<H", entry, 18, FIXED_DATE)  # access date
    struct.pack_into("<H", entry, 22, 0)  # modification time
    struct.pack_into("<H", entry, 24, FIXED_DATE)
    struct.pack_into("<H", entry, 26, first_cluster)
    struct.pack_into("<I", entry, 28, size)
    return entry


def build_image(file_entries):
    """Return FAT12 image bytes and fail if any content cannot fit."""
    if len(VOLUME_LABEL) != 11:
        raise AssertionError("FAT volume labels must be exactly 11 bytes")
    if DATA_CLUSTERS > FAT12_MAX_CLUSTERS:
        raise AssertionError("selected geometry is outside the FAT12 cluster range")
    names = [_name_83(filename) for filename, _ in file_entries]
    if len(set(names)) != len(names):
        raise ValueError("duplicate 8.3 filenames")
    if len(file_entries) + 1 > ROOT_ENTRIES:
        raise ValueError("root directory is full")

    clusters_needed = [math.ceil(len(content) / SECTOR_SIZE) for _, content in file_entries]
    if any(count == 0 for count in clusters_needed):
        raise ValueError("zero-length files are not supported")
    if sum(clusters_needed) > DATA_CLUSTERS:
        raise ValueError("files exceed the 512 KiB FAT12 data area")

    image = bytearray(TOTAL_SECTORS * SECTOR_SIZE)
    image[:SECTOR_SIZE] = _boot_sector(file_entries)
    fat = bytearray(SECTORS_PER_FAT * SECTOR_SIZE)
    fat[:3] = bytes((0xF8, 0xFF, 0xFF))

    root = bytearray(ROOT_SECTORS * SECTOR_SIZE)
    root[:32] = _directory_entry(VOLUME_LABEL, 0, 0, 0x08)
    root_cursor = 32
    next_cluster = 2
    for (filename, content), name, count in zip(file_entries, names, clusters_needed):
        clusters = list(range(next_cluster, next_cluster + count))
        next_cluster += count
        for index, cluster in enumerate(clusters):
            following = clusters[index + 1] if index + 1 < count else 0xFFF
            _set_fat12_entry(fat, cluster, following)
            start = (DATA_START + cluster - 2) * SECTOR_SIZE
            chunk = content[index * SECTOR_SIZE:(index + 1) * SECTOR_SIZE]
            image[start:start + len(chunk)] = chunk
        root[root_cursor:root_cursor + 32] = _directory_entry(
            name, clusters[0], len(content), 0x21  # read-only + archive
        )
        root_cursor += 32

    fat_start = RESERVED_SECTORS * SECTOR_SIZE
    fat_size = len(fat)
    image[fat_start:fat_start + fat_size] = fat
    second_fat = fat_start + fat_size
    image[second_fat:second_fat + fat_size] = fat
    root_start = ROOT_START * SECTOR_SIZE
    image[root_start:root_start + len(root)] = root

    verify_image(bytes(image), file_entries)
    return bytes(image)


def extract_files(image):
    """Read root files from an image for build-time integrity verification."""
    if len(image) != TOTAL_SECTORS * SECTOR_SIZE:
        raise ValueError("image has the wrong size")
    boot = image[:SECTOR_SIZE]
    if boot[510:512] != b"\x55\xAA":
        raise ValueError("missing FAT boot signature")
    bps, spc, reserved, fat_count, root_entries, total, media, spf = struct.unpack_from(
        "<HBHBHHBH", boot, 11
    )
    if (bps, spc, reserved, fat_count, root_entries, total, media, spf) != (
        SECTOR_SIZE, SECTORS_PER_CLUSTER, RESERVED_SECTORS, FAT_COUNT,
        ROOT_ENTRIES, TOTAL_SECTORS, 0xF8, SECTORS_PER_FAT,
    ):
        raise ValueError("unexpected FAT12 BPB geometry")
    fat_offset = RESERVED_SECTORS * SECTOR_SIZE
    fat_size = SECTORS_PER_FAT * SECTOR_SIZE
    fat = image[fat_offset:fat_offset + fat_size]
    root_offset = ROOT_START * SECTOR_SIZE
    root = image[root_offset:root_offset + ROOT_SECTORS * SECTOR_SIZE]
    result = {}
    for offset in range(0, len(root), 32):
        entry = root[offset:offset + 32]
        if entry[0] == 0:
            break
        if entry[11] & 0x08:
            continue
        stem = entry[:8].decode("ascii").rstrip()
        extension = entry[8:11].decode("ascii").rstrip()
        filename = f"{stem}.{extension}" if extension else stem
        cluster = struct.unpack_from("<H", entry, 26)[0]
        size = struct.unpack_from("<I", entry, 28)[0]
        chunks = bytearray()
        visited = set()
        while cluster < 0xFF8:
            if cluster < 2 or cluster in visited or len(visited) >= DATA_CLUSTERS:
                raise ValueError(f"invalid or cyclic FAT chain for {filename}")
            visited.add(cluster)
            start = (DATA_START + cluster - 2) * SECTOR_SIZE
            chunks.extend(image[start:start + SECTOR_SIZE])
            cluster = _get_fat12_entry(fat, cluster)
        result[filename] = bytes(chunks[:size])
    return result


def verify_image(image, expected_files):
    extracted = extract_files(image)
    expected = {filename.upper(): content for filename, content in expected_files}
    if extracted != expected:
        raise ValueError("FAT image extraction did not match source files")


def project_files(app_path):
    script_dir = Path(__file__).resolve().parent
    return [
        ("CONFIG.HTM", Path(app_path).read_bytes()),
        ("README.TXT", (script_dir / "README.txt").read_bytes()),
        ("LICENSE.TXT", (script_dir.parent / "LICENSE").read_bytes()),
    ]


def main():
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", default=script_dir / "dist" / "index.html",
                        help="standalone HTML bundle (default: configurator/dist/index.html)")
    parser.add_argument("--out", default=script_dir / "dist" / "rpico-bridge.img",
                        help="output image (default: configurator/dist/rpico-bridge.img)")
    args = parser.parse_args()
    entries = project_files(args.app)
    image = build_image(entries)
    output = Path(args.out)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, prefix=output.name + ".",
                                     suffix=".tmp", delete=False) as temporary:
        temporary.write(image)
        temp_path = Path(temporary.name)
    os.replace(temp_path, output)
    print(f"Created {output.resolve()} ({len(image)} bytes)")
    for filename, content in entries:
        print(f"  {filename}: {len(content)} bytes sha256={hashlib.sha256(content).hexdigest()}")
    print(f"  image sha256={hashlib.sha256(image).hexdigest()}")


if __name__ == "__main__":
    main()
