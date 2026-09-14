"""Dependency rejection checks; no GPU or installed SDL is needed."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

import stellar as exporter
from native_client_runtime import copy_native_client_runtime, validate_native_client_export
from native_research_runtime import validate_native_research_export


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
        self.font = self.root / "assets/visual/fonts/Rajdhani-SemiBold.ttf"
        self.font.parent.mkdir(parents=True)
        self.font.write_bytes(b"test-only font content")
        self.font_license = self.font.with_name("OFL-Rajdhani.txt")
        self.font_license.write_text("test-only font license")
        self.ui_declaration = self.root / "export/native-ui-assets.json"
        self.ui_declaration.parent.mkdir(parents=True)
        self.ui_declaration.write_text(json.dumps({"schemaVersion": 1,
            "font": {"source": self.font.relative_to(self.root).as_posix(),
                     "runtimePath": self.font.relative_to(self.root).as_posix(),
                     "sha256": hashlib.sha256(self.font.read_bytes()).hexdigest()},
            "license": {"source": self.font_license.relative_to(self.root).as_posix(),
                        "runtimePath": "Licenses/OFL-Rajdhani.txt",
                        "sha256": hashlib.sha256(self.font_license.read_bytes()).hexdigest()}}))

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

    def test_missing_font_blocks_package(self):
        self.font.unlink()
        with self.assertRaisesRegex(RuntimeError, "Missing native UI font"):
            self.copy()

    def test_missing_font_license_blocks_package(self):
        self.font_license.unlink()
        with self.assertRaisesRegex(RuntimeError, "Missing native UI license"):
            self.copy()

    def test_tampered_font_blocks_package(self):
        self.font.write_bytes(b"changed")
        with self.assertRaisesRegex(RuntimeError, "font differs from reviewed content"):
            self.copy()

    def test_tampered_font_license_blocks_package(self):
        self.font_license.write_text("changed")
        with self.assertRaisesRegex(RuntimeError, "license differs from reviewed content"):
            self.copy()

    def test_unreviewed_font_path_cannot_escape_package(self):
        declaration = json.loads(self.ui_declaration.read_text())
        declaration["font"]["runtimePath"] = "../outside.ttf"
        self.ui_declaration.write_text(json.dumps(declaration))
        with self.assertRaisesRegex(RuntimeError, "Unreviewed native UI font path"):
            self.copy()

    def test_unreviewed_font_source_is_rejected(self):
        declaration = json.loads(self.ui_declaration.read_text())
        declaration["font"]["source"] = "../../outside.ttf"
        self.ui_declaration.write_text(json.dumps(declaration))
        with self.assertRaisesRegex(RuntimeError, "Unreviewed native UI font path"):
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


class NativeSessionExportTests(unittest.TestCase):
    def exercise(self, mutate_load=False):
        with tempfile.TemporaryDirectory(prefix="stellar-session-export-test-") as temporary:
            package = Path(temporary) / "package"
            package.mkdir()
            calls = []
            def launch(args, *, cwd, env, **unused):
                save = Path(args[args.index("--save-path") + 1])
                capture = Path(args[args.index("--smoke") + 1])
                self.assertEqual(save.parent, cwd)
                self.assertNotEqual(cwd, package)
                self.assertEqual(capture.parent, cwd)
                calls.append(args)
                if "--load" in args:
                    self.assertTrue(save.is_file())
                    payload = json.loads(save.read_text())
                    payload["SavedAtUtc"] = "later"
                    if mutate_load:
                        payload["SimulationDays"] += 1
                else:
                    payload = {"FormatVersion": 17, "SavedAtUtc": "earlier", "SimulationDays": 42.25,
                               "Galaxy": {"Systems": list(range(500))}}
                save.write_text(json.dumps(payload))
                capture.write_bytes(b"BM" + bytes(54))
                return subprocess.CompletedProcess(args, 0, "gpu_driver=vulkan systems=500 ", "")
            with mock.patch("native_client_runtime.subprocess.run", side_effect=launch):
                result = validate_native_client_export(package, {})
            self.assertEqual(len(calls), 2)
            self.assertTrue(result["nativeClientPlayer17Reload"])
            self.assertTrue(result["nativeClientIsolatedManualSave"])

    def test_preview_uses_isolated_save_and_reloads_it(self):
        self.exercise()

    def test_load_cannot_silently_change_the_saved_world(self):
        with self.assertRaisesRegex(RuntimeError, "changed during paused load"):
            self.exercise(mutate_load=True)


class NativeResearchExportTests(unittest.TestCase):
    def exercise(self, *, mutate_load=False, funded=True, progressed=True, skipped_save=False):
        with tempfile.TemporaryDirectory(prefix="stellar-research-export-test-") as temporary:
            package = Path(temporary) / "package"
            package.mkdir()
            calls = []

            def launch(args, *, cwd, env, **unused):
                self.assertNotEqual(cwd, package)
                save = Path(args[args.index("--save-path") + 1])
                capture = Path(args[args.index("--research-smoke") + 1])
                self.assertEqual(save.parent, cwd)
                self.assertEqual(capture.parent, cwd)
                calls.append(args)
                if "--load" in args:
                    payload = json.loads(save.read_text())
                    payload["SavedAtUtc"] = "later"
                    if mutate_load:
                        payload["Galaxy"]["Economies"][0]["Credits"] += 1
                else:
                    research = {"Core": {"ActiveProjects": [{"NodeId": "known",
                                "Paused": False, "TotalResearchPoints": 1 if progressed else 0}]}}
                    for _ in range(3):
                        research = {"Research": research}
                    payload = {"FormatVersion": 17, "SavedAtUtc": "earlier",
                        "Galaxy": {"Systems": list(range(500)), "PlayerCivilizationId": 7,
                                   "Economies": [{"CivilizationId": 7, "Credits": 100,
                                                  "LastResearchSpendingPerDay": 2 if funded else 0}]},
                        "AdaptiveResearch": {"Civilizations": [{"CivilizationId": 7,
                                                                  "Research": research}]}}
                save.write_text(json.dumps(payload))
                capture.write_bytes(b"BM" + bytes(54))
                saved = "preserved" if skipped_save and "--load" in args else "ok"
                return subprocess.CompletedProcess(args, 0, "gpu_driver=vulkan systems=500 save=" + saved + " research=known:active:0.1", "")

            with mock.patch("native_research_runtime.subprocess.run", side_effect=launch):
                result = validate_native_research_export(package, {})
            self.assertEqual(len(calls), 2)
            self.assertTrue(result["nativeResearchPlayerInput"])
            self.assertTrue(result["nativeResearchProgressReload"])
            self.assertTrue(all(Path(path).is_file() for path in result["researchCaptures"]))

    def test_research_input_progress_survives_isolated_reload(self):
        self.exercise()

    def test_research_reload_cannot_change_treasury(self):
        with self.assertRaisesRegex(RuntimeError, "changed during paused load"):
            self.exercise(mutate_load=True)

    def test_unfunded_research_does_not_count_as_success(self):
        with self.assertRaisesRegex(RuntimeError, "funded, advancing research"):
            self.exercise(funded=False)

    def test_zero_progress_does_not_count_as_success(self):
        with self.assertRaisesRegex(RuntimeError, "funded, advancing research"):
            self.exercise(progressed=False)

    def test_skipping_loaded_save_is_not_a_roundtrip(self):
        with self.assertRaisesRegex(RuntimeError, "actual manual save"):
            self.exercise(skipped_save=True)


if __name__ == "__main__":
    unittest.main()
