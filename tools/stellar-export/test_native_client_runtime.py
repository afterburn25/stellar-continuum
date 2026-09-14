"""Dependency rejection checks; no GPU or installed SDL is needed."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

import stellar as exporter
from native_client_runtime import copy_native_client_runtime


class NativeClientDependencyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="stellar-dependency-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "build"
        self.output = self.root / "output"
        self.build.mkdir()
        self.output.mkdir()
        image = bytearray(128)
        image[:2] = b"MZ"
        struct.pack_into("<I", image, 0x3c, 64)
        image[64:68] = b"PE\0\0"
        struct.pack_into("<H", image, 68, 0x8664)
        (self.build / "stellar-continuum-native.exe").write_bytes(image)
        self.library = self.build / "SDL3.dll"
        self.library.write_bytes(image)
        self.license = self.build / "_deps/stellar-sdl3/SDL3-test/LICENSE.txt"
        self.license.parent.mkdir(parents=True)
        self.license.write_text("test license")
        lock = {"archiveRoot": "SDL3-test", "version": "test",
                "releaseUrl": "https://example.invalid/test-only",
                "runtime": {"sha256": hashlib.sha256(image).hexdigest()},
                "license": {"path": "LICENSE.txt", "sha256": hashlib.sha256(self.license.read_bytes()).hexdigest()},
                "windowsImports": ["kernel32.dll", "user32.dll"]}
        lock_path = self.root / "third_party/SDL3/runtime-lock.json"
        lock_path.parent.mkdir(parents=True)
        lock_path.write_text(json.dumps(lock))
        self.imports = {"stellar-continuum-native.exe": ["SDL3.dll", "KERNEL32.dll"],
                        "SDL3.dll": ["USER32.dll"]}

    def inspect(self, binary, runtime=(), windows=()):
        text = "\n".join("    " + name for name in self.imports[binary.name])
        with mock.patch.object(exporter, "run", return_value=text):
            return exporter.executable_dependencies(binary, {}, runtime, windows)

    def copy(self):
        return copy_native_client_runtime(self.root, self.build, self.output, self.inspect)

    def test_only_declared_runtime_and_license_are_copied(self):
        metadata = self.copy()
        self.assertEqual(metadata["entryPoint"], "stellar-continuum-native.exe")
        self.assertFalse(metadata["graphicalParity"])
        self.assertEqual(set(metadata["requiredFiles"]), {p.relative_to(self.output).as_posix()
                         for p in self.output.rglob("*") if p.is_file()})

    def test_missing_license_blocks_package(self):
        self.license.unlink()
        with self.assertRaisesRegex(RuntimeError, "Missing native client dependency"):
            self.copy()

    def test_tampered_sdl_blocks_package(self):
        self.library.write_bytes(self.library.read_bytes() + b"changed")
        with self.assertRaisesRegex(RuntimeError, "differs from reviewed SDL"):
            self.copy()

    def test_undeclared_client_import_is_rejected(self):
        self.imports["stellar-continuum-native.exe"].append("unreviewed.dll")
        with self.assertRaisesRegex(RuntimeError, "Unpackaged runtime dependencies"):
            self.copy()

    def test_undeclared_transitive_sdl_import_is_rejected(self):
        self.imports["SDL3.dll"].append("unreviewed.dll")
        with self.assertRaisesRegex(RuntimeError, "Unpackaged runtime dependencies"):
            self.copy()

    def test_headless_dependency_policy_still_rejects_sdl(self):
        with self.assertRaisesRegex(RuntimeError, "Unpackaged runtime dependencies"):
            self.inspect(self.build / "stellar-continuum-native.exe")


if __name__ == "__main__":
    unittest.main()
