#!/usr/bin/env python3
"""Inspect and extract an application from a Passport merged binary."""

from __future__ import annotations

import argparse
import csv
import hashlib
from functools import reduce
from operator import xor
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
from archive_firmware import IMAGE_HEADER_SIZE, SEGMENT_HEADER, application_descriptor
from verify_firmware import FLASH_SIZE, PARTITION_TABLE_SIZE, Partition, parse_partition_table, validate_partition_ranges


TABLE_OFFSET = 0x8000
APP_ALIGNMENT = 0x10000
APP_NAME = re.compile(r"Passport-[A-Za-z0-9_-]{1,22}")
NAME_BYTES = 32
# The project name follows magic, secure version, reserved words and version.
NAME_OFFSET = IMAGE_HEADER_SIZE + SEGMENT_HEADER.size + 16 + 32
HASH_BYTES = hashlib.sha256().digest_size
BASE_DATA = {("nvs", 1, 2), ("phy_init", 1, 1)}
MANAGER_TABLE = Path(__file__).resolve().parents[1] / "main/runner/partitions.apps.csv"


def read_merged(path: Path) -> tuple[bytes, list[Partition]]:
    with path.open("rb") as source:
        merged = source.read(FLASH_SIZE + 1)
    if len(merged) > FLASH_SIZE or not merged.startswith(b"\xe9"):
        raise ValueError("expected a raw 0x0 Passport merged .bin, at most 8 MiB")
    table = merged[TABLE_OFFSET : TABLE_OFFSET + PARTITION_TABLE_SIZE]
    partitions, has_md5 = parse_partition_table(table)
    if not has_md5:
        raise ValueError("partition table has no MD5 marker")
    validate_partition_ranges(partitions)
    return merged, partitions


def read_app(data: bytes) -> tuple[bytes, dict[str, str]]:
    descriptor = application_descriptor(data)
    if data[23] != 1:
        raise ValueError("application does not have an ESP-IDF SHA-256 trailer")
    cursor = IMAGE_HEADER_SIZE
    checksum = 0xEF
    view = memoryview(data)
    for index in range(data[1]):
        if cursor + SEGMENT_HEADER.size > len(data):
            raise ValueError(f"segment {index} header is truncated")
        _, size = SEGMENT_HEADER.unpack_from(data, cursor)
        cursor += SEGMENT_HEADER.size
        end = cursor + size
        if size % 4 or end > len(data):
            raise ValueError(f"segment {index} has an invalid length")
        checksum = reduce(xor, view[cursor:end], checksum)
        cursor = end
    trailer = (cursor + 16) & ~15
    image_end = trailer + HASH_BYTES
    if image_end > len(data):
        raise ValueError("application trailer is truncated")
    if data[trailer - 1] != checksum:
        raise ValueError("application checksum does not match")
    if hashlib.sha256(view[:trailer]).digest() != data[trailer:image_end]:
        raise ValueError("application SHA-256 does not match")
    if data[image_end:].strip(b"\xff"):
        raise ValueError("extra data after the application digest, such as a signature or bundled assets")
    return data[:image_end], descriptor


def manager_limit() -> int:
    with MANAGER_TABLE.open(encoding="utf-8") as table:
        for row in csv.reader(line for line in table if not line.lstrip().startswith("#")):
            if row and row[0].strip() == "app_pool":
                return int(row[4].strip(), 0)
    raise ValueError("manager partition table has no app_pool")


def prepare_upload(app: bytes, descriptor: dict[str, str], name: str, partitions: list[Partition]) -> bytes:
    if not APP_NAME.fullmatch(name):
        raise ValueError("use Passport- followed by 1–22 ASCII letters, digits, hyphens or underscores")
    limit = manager_limit()
    if len(app) > limit:
        raise ValueError(f"application is {len(app):,} bytes; the manager allows at most {limit:,}")
    version = descriptor["version"]
    if not 1 <= len(version) <= 31 or any(not " " <= ch <= "~" for ch in version):
        raise ValueError("the manager requires a printable ASCII version of 1–31 characters")
    if sum(p.kind == 0 for p in partitions) != 1:
        raise ValueError("the upload conversion supports single-app bundles")
    extra = [p.label for p in partitions if p.kind != 0 and (p.label, p.kind, p.subtype) not in BASE_DATA]
    if extra:
        raise ValueError("upload conversion cannot carry additional partitions: " + ", ".join(extra))

    image = bytearray(app)
    name_end = NAME_OFFSET + NAME_BYTES
    field = name.encode("ascii").ljust(NAME_BYTES, b"\0")
    checksum_pos = len(image) - HASH_BYTES - 1
    checksum = reduce(xor, image[NAME_OFFSET:name_end], image[checksum_pos])
    image[checksum_pos] = reduce(xor, field, checksum)
    image[NAME_OFFSET:name_end] = field
    image[-HASH_BYTES:] = hashlib.sha256(memoryview(image)[:-HASH_BYTES]).digest()
    return bytes(image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("merged", type=Path, help="community raw .bin written from 0x0")
    parser.add_argument("-o", "--output", type=Path, help="write the app here; omit to inspect only")
    parser.add_argument("--partition", help="app partition label; required if several apps are present")
    parser.add_argument("--name", help="make an upload candidate with this embedded Passport- name")
    args = parser.parse_args()
    if args.name is not None and args.output is None:
        parser.error("--name requires --output")

    try:
        merged, partitions = read_merged(args.merged)
        images = {}
        for part in partitions:
            if part.kind != 0 or (args.partition is not None and part.label != args.partition):
                continue
            data = merged[part.offset:part.end]
            if not data.strip(b"\xff"):
                continue
            try:
                if part.offset % APP_ALIGNMENT:
                    raise ValueError("ESP32-C3 app partition is not aligned to 64 KiB")
                images[part.label] = read_app(data)
            except ValueError as error:
                raise ValueError(f"{part.label} at 0x{part.offset:x}: {error}") from error
            image, description = images[part.label]
            print(f"{part.label} at 0x{part.offset:x}: {description['project_name']!r}, "
                  f"version {description['version']!r}, {len(image):,} bytes")
        if not images:
            raise ValueError("no populated app partition matched")
        if args.output is None:
            return 0
        if len(images) != 1:
            raise ValueError("choose the application with --partition")
        image, description = next(iter(images.values()))
        if args.name is not None:
            image = prepare_upload(image, description, args.name, partitions)
        with args.output.open("xb") as output:
            output.write(image)
        print(f"Wrote {args.output} ({len(image):,} bytes)")
        if args.name is not None:
            print(f"Embedded name: {args.name}. Test runtime compatibility before relying on this app.")
        elif not APP_NAME.fullmatch(description["project_name"]):
            print("The original project name needs --name for Passport app-manager uploads.")
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
