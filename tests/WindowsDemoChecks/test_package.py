"""Distributable completeness checks; fixtures never execute binaries."""
import importlib.util
import json
import re
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

SCRIPT = Path(__file__).resolve().parents[2] / "scripts/package_windows_demo.py"
SPEC = importlib.util.spec_from_file_location("package_windows_demo", SCRIPT)
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


def fixture(root):
    root.mkdir()
    header = bytearray(64)
    header[:2] = b"MZ"
    struct.pack_into("<I", header, 60, 64)
    (root / PACKAGE.EXE).write_bytes(header + b"PE\0\0\x64\x86")
    (root / PACKAGE.PCK).write_bytes(b"GDPCfixture")
    payload = root / PACKAGE.PAYLOAD
    payload.mkdir()
    for name in PACKAGE.REQUIRED_MANAGED:
        (payload / name).write_bytes(b"fixture")
    data = root / "data/research/v1"
    data.mkdir(parents=True)
    (data / "index.json").write_text("{}")


class WindowsDemoChecks(unittest.TestCase):
    def test_package_and_in_game_versions_match(self):
        version = (PACKAGE.ROOT / "VERSION").read_text(encoding="utf-8").strip()
        source = (PACKAGE.ROOT / "src/Game/GameVersion.cs").read_text(encoding="utf-8")
        self.assertEqual(version, re.search(r'Current = "([^"]+)"', source).group(1))
        number, _, stage = version.partition("-")
        expected_label = number + (" " + stage.capitalize() if stage else "")
        self.assertEqual(expected_label, re.search(r'Display = "([^"]+)"', source).group(1))

    def test_complete_package_records_exact_revision_and_payload(self):
        with tempfile.TemporaryDirectory() as temp:
            export = Path(temp) / "windows"
            fixture(export)
            archive = PACKAGE.package(export, Path(temp) / "dist", "a" * 40,
                                      "https://github.com/afterburn25/stellar-continuum/actions/runs/1")
            self.assertTrue(archive.with_suffix(".zip.sha256").is_file())
            with zipfile.ZipFile(archive) as contents:
                manifest = json.loads(contents.read(next(n for n in contents.namelist() if n.endswith("/BUILD.json"))))
                self.assertEqual("a" * 40, manifest["git_commit"])
                self.assertIn(PACKAGE.PAYLOAD + "/coreclr.dll", manifest["files"])
                self.assertIn("not performed", manifest["validation"]["windows_binary_execution"])
                self.assertEqual(len(manifest["files"]) + 1, len(contents.namelist()))

    def test_missing_managed_runtime_is_rejected(self):
        for name in PACKAGE.REQUIRED_MANAGED:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                export = Path(temp) / "windows"
                fixture(export)
                (export / PACKAGE.PAYLOAD / name).unlink()
                with self.assertRaisesRegex(ValueError, "incomplete"):
                    PACKAGE.validate_export(export)

    def test_wrong_architecture_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            export = Path(temp) / "windows"
            fixture(export)
            (export / PACKAGE.EXE).write_bytes((export / PACKAGE.EXE).read_bytes()[:-2] + b"\x4c\x01")
            with self.assertRaisesRegex(ValueError, "x86_64"):
                PACKAGE.validate_export(export)

    def test_missing_public_data_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            export = Path(temp) / "windows"
            fixture(export)
            (export / "data/research/v1/index.json").unlink()
            with self.assertRaisesRegex(ValueError, "catalog"):
                PACKAGE.validate_export(export)

    def test_invalid_pck_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            export = Path(temp) / "windows"
            fixture(export)
            (export / PACKAGE.PCK).write_bytes(b"bad pack")
            with self.assertRaisesRegex(ValueError, "resource pack"):
                PACKAGE.validate_export(export)


if __name__ == "__main__":
    unittest.main()
