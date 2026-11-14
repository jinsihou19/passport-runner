"""Check the manager's boot layout and retain the matching upload/debug artifacts."""

import argparse
import hashlib
import json
from pathlib import Path
from tempfile import TemporaryDirectory

from archive_firmware import FULL_BIN, application_descriptor
from verify_firmware import parse_flash_args, parse_partition_table


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def package(manager, claw, pomodoro, output):
    table_path = "partition_table/partition-table.bin"
    table = (manager / table_path).read_bytes()
    partitions, _ = parse_partition_table(table)
    parts = {part.label: part for part in partitions}
    app_parts = {(part.label, part.subtype) for part in partitions if part.kind == 0}
    if app_parts != {("system", 0), ("app_pool", 16)} or [
        (part.kind, part.subtype) for part in (parts["otadata"], parts["app_boot"], parts["app_meta"])
    ] != [(1, 0), (1, 0x40), (1, 0x83)]:
        raise ValueError("incorrect app manager partition types")
    if [(parts[label].offset, parts[label].size) for label in ("nvs", "phy_init", "app_boot")] != [
        (0x9000, 0x6000), (0xF000, 0x1000), (0x12000, 0x1000)
    ]:
        raise ValueError("Runner partition table changes the settings or boot request address")

    flash = parse_flash_args((manager / "flash_args").read_text())
    merged = (manager / FULL_BIN).read_bytes()
    if flash["FoloToy-AI-Passport.bin"] != parts["system"].offset:
        raise ValueError("Runner system is not in the factory partition")
    if b"Starting Runner system" not in (manager / "bootloader/bootloader.bin").read_bytes():
        raise ValueError("the Runner build is missing its bootloader")
    if flash["ota_data_initial.bin"] != parts["otadata"].offset or merged[
        parts["otadata"].offset : parts["otadata"].end
    ] != b"\xff" * parts["otadata"].size:
        raise ValueError("the merged image must start with blank OTA boot selection")
    pool = parts["app_pool"]
    store = parts["app_meta"]
    if (pool.offset % 0x10000 or pool.size % 0x10000 or
            parts["system"].end != pool.offset or pool.end != store.offset):
        raise ValueError("Runner requires an aligned and contiguous app pool")
    for part in (parts["app_boot"], pool):
        if merged[part.offset : part.end] != b"\xff" * part.size:
            raise ValueError(f"{part.label} must start blank")
    store_image = (manager / "app_meta.bin").read_bytes()
    if (flash["app_meta.bin"] != store.offset or len(store_image) != store.size
            or merged[store.offset : store.end] != store_image):
        raise ValueError("the merged image must initialize the app catalog")

    for build, project in (
        (claw, "Passport-ESP-Claw"),
        (pomodoro, "Passport-Pomodoro"),
    ):
        if (build / table_path).read_bytes() != table:
            raise ValueError(f"{project} was built for a different partition layout")
        files = {ext: (build / f"{project}.{ext}").read_bytes() for ext in ("bin", "elf", "map")}
        app = files["bin"]
        descriptor = application_descriptor(app)
        if len(app) > pool.size or descriptor["project_name"] != project:
            raise ValueError(f"incorrect {project} identity or size")
        if descriptor["embedded_elf_sha256"] != sha256(files["elf"]):
            raise ValueError(f"{project} ELF does not match the application image")
        output.mkdir(parents=True, exist_ok=True)
        destination = output / sha256(app)
        if not destination.exists():
            with TemporaryDirectory(dir=output) as temporary:
                stage = Path(temporary) / "app"
                stage.mkdir()
                for ext, content in files.items():
                    (stage / f"{project}.{ext}").write_bytes(content)
                manifest = {"app": descriptor, "files": {
                    f"{project}.{ext}": {"size": len(content), "sha256": sha256(content)}
                    for ext, content in files.items()
                }}
                (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
                stage.rename(destination)
        manifest = json.loads((destination / "manifest.json").read_text())
        for ext, content in files.items():
            name = f"{project}.{ext}"
            recorded = (destination / name).read_bytes()
            if manifest["files"][name] != {"size": len(recorded), "sha256": sha256(recorded)} or (
                ext != "map" and recorded != content
            ):
                raise ValueError(f"archived {name} does not match the build")
        print(f"Passport app verified: {destination / (project + '.bin')} ({len(app)} bytes)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("manager", "claw", "pomodoro", "output"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    try:
        package(args.manager, args.claw, args.pomodoro, args.output)
    except (KeyError, OSError, ValueError) as error:
        parser.exit(1, f"ERROR: {error}\n")
