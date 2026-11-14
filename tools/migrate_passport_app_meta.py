#!/usr/bin/env python3
"""Convert the Runner 128 KiB app_meta image to the 64 KiB layout."""

import argparse
from pathlib import Path
from struct import Struct

from littlefs import LittleFS, UserContext
from littlefs.errors import LittleFSError


BLOCK_SIZE = 4096
OLD_SIZE = 128 * 1024
NEW_SIZE = 64 * 1024
APP_ALIGNMENT = 64 * 1024
OLD_POOL_SIZE = 92 * APP_ALIGNMENT
MAX_ID = 96
CATALOG_HEADER = Struct("<5I")
CATALOG_ENTRY = Struct("<3I")
METADATA_SIZE = 176
APP_SIZE_OFFSET = 164


def filesystem(context):
    return LittleFS(
        context=context, mount=False, block_size=BLOCK_SIZE,
        block_count=len(context.buffer) // BLOCK_SIZE, read_size=128,
        prog_size=128, cache_size=512, lookahead_size=128,
        block_cycles=512, name_max=64,
    )


def migrate(image):
    if len(image) != OLD_SIZE:
        raise ValueError("expected the 128 KiB app_meta partition image")
    source = UserContext(OLD_SIZE)
    source.buffer[:] = image
    old = filesystem(source)
    old.mount()
    try:
        with old.open("catalog", "rb") as file:
            catalog = file.read()
    except FileNotFoundError:
        catalog = CATALOG_HEADER.pack(0x43415431, 0, 0, 0, 0)

    if len(catalog) < CATALOG_HEADER.size:
        raise ValueError("truncated app catalog")
    magic, count, moving_id, destination, copied = CATALOG_HEADER.unpack_from(catalog)
    entries = catalog[CATALOG_HEADER.size:]
    if magic != 0x43415431 or count > 92 or len(entries) != count * CATALOG_ENTRY.size:
        raise ValueError("invalid app catalog")
    records = {}
    end = 0
    move_valid = not (moving_id or destination or copied)
    for offset, size, app_id in CATALOG_ENTRY.iter_unpack(entries):
        span = (size + APP_ALIGNMENT - 1) // APP_ALIGNMENT * APP_ALIGNMENT
        if (not 1 <= app_id <= MAX_ID or app_id in records or not size
                or offset < end or offset % APP_ALIGNMENT or offset + span > OLD_POOL_SIZE):
            raise ValueError("invalid extent in app catalog")
        if app_id == moving_id:
            move_valid = destination == end < offset and copied <= span and copied % APP_ALIGNMENT == 0
        with old.open(str(app_id), "rb") as file:
            record = file.read()
        if len(record) != METADATA_SIZE or int.from_bytes(
            record[APP_SIZE_OFFSET : APP_SIZE_OFFSET + 4], "little"
        ) != size:
            raise ValueError(f"invalid metadata for app {app_id}")
        records[app_id] = record
        end = offset + span
    if not move_valid:
        raise ValueError("invalid app move journal")

    target = UserContext(NEW_SIZE)
    new = filesystem(target)
    new.format()
    new.mount()
    with new.open("catalog", "wb") as file:
        file.write(catalog)
    if records:
        with new.open("metadata", "wb") as file:
            for app_id, record in sorted(records.items()):
                file.seek((app_id - 1) * METADATA_SIZE)
                file.write(record)
    new.unmount()
    return target.buffer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="128 KiB partition read from 0x7e0000")
    parser.add_argument("output", type=Path, help="new 64 KiB partition image for 0x7f0000")
    args = parser.parse_args()
    try:
        image = migrate(args.source.read_bytes())
        with args.output.open("xb") as file:
            file.write(image)
    except (OSError, ValueError, LittleFSError) as error:
        parser.exit(1, f"ERROR: {error}\n")


if __name__ == "__main__":
    main()
