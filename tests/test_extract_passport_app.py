"""Host tests for extracting and preparing community applications."""

import hashlib
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from test_verify_firmware import sample_table

ROOT = Path(__file__).resolve().parents[1]
APP_OFFSET = 0x10000


def sample_app(project: bytes = b"community-game") -> bytes:
    desc = bytearray(256)
    struct.pack_into("<I", desc, 0, 0xABCD5432)
    desc[16:22] = b"v1.0.0"
    desc[48:80] = project.ljust(32, b"\0")
    desc[112:118] = b"v5.5.3"
    header = bytearray(24)
    header[:2] = b"\xe9\x02"
    header[12] = 5  # ESP32-C3
    header[23] = 1
    # The second segment and padding are deliberately outside the descriptor.
    payload = b"game"
    image = header + struct.pack("<II", 0x3C000020, 256) + desc
    image += struct.pack("<II", 0x40380000, len(payload)) + payload
    checksum = 0xEF
    for byte in desc + payload:
        checksum ^= byte
    image += b"\0" * (15 - len(image) % 16) + bytes([checksum])
    return bytes(image) + hashlib.sha256(image).digest()


class ExtractionTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.source = root / "community.bin"
        self.output = root / "upload.bin"

    def merge(self, app, entries=None):
        flash = bytearray(b"\xff" * APP_OFFSET)
        flash[0] = 0xE9
        flash[0x8000:0x8C00] = sample_table(entries)
        self.source.write_bytes(flash + app)

    def extract(self, *args, success=True):
        command = [sys.executable, str(ROOT / "tools/extract_passport_app.py"), str(self.source), *args]
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0 if success else 1, result.stdout + result.stderr)
        return result

    def test_extracts_exact_app_and_keeps_source_and_existing_output(self):
        app = sample_app()
        self.merge(app + b"\xff" * 4096)
        source = self.source.read_bytes()
        self.assertIn("factory at 0x10000", self.extract().stdout)
        self.extract("-o", str(self.output))
        self.assertEqual(self.output.read_bytes(), app)
        self.extract("-o", str(self.output), "--name", "Passport-Game", success=False)
        self.assertEqual(self.output.read_bytes(), app)
        self.assertEqual(self.source.read_bytes(), source)

    def test_prepares_upload_and_preserves_every_other_payload_byte(self):
        self.merge(sample_app())
        self.extract("-o", str(self.output), "--name", "Passport-Game")
        self.assertEqual(self.output.read_bytes(), sample_app(b"Passport-Game"))

    def test_rejects_corruption_and_unexpected_tail(self):
        app = sample_app()
        for data, error in (
            (app[:-1], "trailer is truncated"),
            (app[:-1] + bytes([app[-1] ^ 1]), "SHA-256 does not match"),
            (app + b"\xff" * 128 + b"assets", "extra data"),
        ):
            with self.subTest(error=error):
                self.merge(data)
                result = self.extract("-o", str(self.output), success=False)
                self.assertIn(error, result.stderr)
                self.assertFalse(self.output.exists())

    def test_does_not_offer_upload_for_an_app_with_additional_partitions(self):
        entries = ((0, 0, 0x10000, 0x200000, "factory"), (1, 0x82, 0x210000, 0x5F0000, "storage"))
        self.merge(sample_app(), entries)
        result = self.extract("-o", str(self.output), "--name", "Passport-Game", success=False)
        self.assertIn("additional partitions: storage", result.stderr)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
