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
from native_celestial_runtime import NATIVE_CELESTIAL_SOURCES
from native_species_runtime import NATIVE_SPECIES_SOURCES
from native_audio_runtime import NATIVE_AUDIO_SOURCES
from native_voice_runtime import NATIVE_VOICE_SOURCES
from native_startup_art_runtime import NATIVE_STARTUP_ART_SOURCES
from native_galaxy_art_runtime import NATIVE_GALAXY_ART_SOURCES
from native_ship_art_runtime import NATIVE_SHIP_ART_SOURCES
from native_planet_art_runtime import NATIVE_PLANET_ART_SOURCES
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
        celestial_records = {}
        for key, (source, runtime_path) in NATIVE_CELESTIAL_SOURCES.items():
            asset = self.root / source
            asset.parent.mkdir(parents=True, exist_ok=True)
            asset.write_bytes(("test-only asset " + key).encode())
            celestial_records[key] = {"source": source, "runtimePath": runtime_path,
                                      "sha256": hashlib.sha256(asset.read_bytes()).hexdigest()}
        self.celestial_declaration = self.root / "export/native-celestial-assets.json"
        self.celestial_declaration.write_text(json.dumps({"schemaVersion": 1, "assets": celestial_records}))

        species_records = {}
        for key, (source, runtime_path) in NATIVE_SPECIES_SOURCES.items():
            asset = self.root / source
            asset.parent.mkdir(parents=True, exist_ok=True)
            asset.write_bytes(("test-only species " + key).encode())
            species_records[key] = {"source": source, "runtimePath": runtime_path,
                                    "sha256": hashlib.sha256(asset.read_bytes()).hexdigest()}
        self.species_declaration = self.root / "export/native-species-assets.json"
        self.species_declaration.write_text(json.dumps({"schemaVersion": 1, "assets": species_records}))
        startup_art_records = {}
        for key, (source, destination) in NATIVE_STARTUP_ART_SOURCES.items():
            asset = self.root / source
            asset.parent.mkdir(parents=True, exist_ok=True)
            asset.write_bytes(("test-only startup art " + key).encode())
            startup_art_records[key] = {"source": source, "runtimePath": destination,
                                        "sha256": hashlib.sha256(asset.read_bytes()).hexdigest()}
        self.startup_art_declaration = self.root / "export/native-startup-art-assets.json"
        self.startup_art_declaration.write_text(json.dumps({"schemaVersion":1,"assets":startup_art_records}))
        galaxy_art_records = {}
        for key, (source, destination) in NATIVE_GALAXY_ART_SOURCES.items():
            asset = self.root / source
            asset.parent.mkdir(parents=True, exist_ok=True)
            asset.write_bytes(("test-only galaxy art " + key).encode())
            galaxy_art_records[key] = {"source": source, "runtimePath": destination,
                                        "sha256": hashlib.sha256(asset.read_bytes()).hexdigest()}
        self.galaxy_art_declaration = self.root / "export/native-galaxy-art-assets.json"
        self.galaxy_art_declaration.write_text(json.dumps({"schemaVersion":1,"assets":galaxy_art_records}))
        ship_art_records = {}
        for key, (source, destination) in NATIVE_SHIP_ART_SOURCES.items():
            asset = self.root / source
            asset.parent.mkdir(parents=True, exist_ok=True)
            asset.write_bytes(("test-only ship art " + key).encode())
            ship_art_records[key] = {"source": source, "runtimePath": destination,
                                      "sha256": hashlib.sha256(asset.read_bytes()).hexdigest()}
        self.ship_art_declaration = self.root / "export/native-ship-art-assets.json"
        self.ship_art_declaration.write_text(json.dumps({"schemaVersion":1,"assets":ship_art_records}))
        planet_art_records = {}
        for key, (source, destination) in NATIVE_PLANET_ART_SOURCES.items():
            asset = self.root / source
            asset.parent.mkdir(parents=True, exist_ok=True)
            asset.write_bytes(("test-only planet art " + key).encode())
            planet_art_records[key] = {"source": source, "runtimePath": destination,
                                       "sha256": hashlib.sha256(asset.read_bytes()).hexdigest()}
        self.planet_art_declaration = self.root / "export/native-planet-art-assets.json"
        self.planet_art_declaration.write_text(json.dumps({"schemaVersion":1,"assets":planet_art_records}))
        audio_records = {}
        for key, (source, destination) in NATIVE_AUDIO_SOURCES.items():
            asset = self.root / source
            asset.parent.mkdir(parents=True, exist_ok=True)
            asset.write_bytes(("test-only audio " + key).encode())
            audio_records[key] = {"source": source, "runtimePath": destination,
                                  "sha256": hashlib.sha256(asset.read_bytes()).hexdigest()}
        self.audio_declaration = self.root / "export/native-audio-assets.json"
        self.audio_declaration.write_text(json.dumps({"schemaVersion":1,"assets":audio_records}))
        voice_records = {}
        for key, (source, destination) in NATIVE_VOICE_SOURCES.items():
            asset = self.root / source
            asset.parent.mkdir(parents=True, exist_ok=True)
            asset.write_bytes(("test-only voice " + key).encode())
            voice_records[key] = {"source": source, "runtimePath": destination,
                                  "sha256": hashlib.sha256(asset.read_bytes()).hexdigest()}
        self.voice_declaration = self.root / "export/native-voice-assets.json"
        self.voice_declaration.write_text(json.dumps({"schemaVersion":1,"assets":voice_records}))



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

    def test_missing_species_portrait_blocks_package(self):
        (self.root / NATIVE_SPECIES_SOURCES["terran-baseline"][0]).unlink()
        with self.assertRaisesRegex(RuntimeError, "Missing native species"):
            self.copy()

    def test_tampered_species_portrait_blocks_package(self):
        (self.root / NATIVE_SPECIES_SOURCES["terran-baseline"][0]).write_bytes(b"changed")
        with self.assertRaisesRegex(RuntimeError, "differs from reviewed content"):
            self.copy()

    def test_missing_species_credits_blocks_package(self):
        (self.root / NATIVE_SPECIES_SOURCES["credits"][0]).unlink()
        with self.assertRaisesRegex(RuntimeError, "Missing native species credits"):
            self.copy()

    def test_unreviewed_species_source_is_rejected(self):
        declaration = json.loads(self.species_declaration.read_text())
        declaration["assets"]["terran-baseline"]["source"] = "../outside.jpg"
        self.species_declaration.write_text(json.dumps(declaration))
        with self.assertRaisesRegex(RuntimeError, "Unreviewed native species"):
            self.copy()

    def test_unreviewed_species_output_is_rejected(self):
        declaration = json.loads(self.species_declaration.read_text())
        declaration["assets"]["terran-baseline"]["runtimePath"] = "../outside.jpg"
        self.species_declaration.write_text(json.dumps(declaration))
        with self.assertRaisesRegex(RuntimeError, "Unreviewed native species"):
            self.copy()

    def test_unreviewed_species_set_is_rejected(self):
        declaration = json.loads(self.species_declaration.read_text())
        declaration["assets"]["other"] = declaration["assets"]["terran-baseline"]
        self.species_declaration.write_text(json.dumps(declaration))
        with self.assertRaisesRegex(RuntimeError, "set differs from reviewed content"):
            self.copy()

    def test_unsupported_species_schema_is_rejected(self):
        declaration = json.loads(self.species_declaration.read_text())
        declaration["schemaVersion"] = 2
        self.species_declaration.write_text(json.dumps(declaration))
        with self.assertRaisesRegex(RuntimeError, "Unsupported native species"):
            self.copy()

    def test_missing_startup_art_blocks_package(self):
        for source, destination in NATIVE_STARTUP_ART_SOURCES.values():
            with self.subTest(source=source):
                path = self.root / source
                original = path.read_bytes()
                path.unlink()
                with self.assertRaisesRegex(RuntimeError, "Missing native startup art"):
                    self.copy()
                path.write_bytes(original)

    def test_tampered_startup_art_blocks_package(self):
        for source, destination in NATIVE_STARTUP_ART_SOURCES.values():
            with self.subTest(source=source):
                path = self.root / source
                original = path.read_bytes()
                path.write_bytes(b"altered")
                with self.assertRaisesRegex(RuntimeError, "differs from reviewed content"):
                    self.copy()
                path.write_bytes(original)

    def test_startup_art_paths_cannot_expand_package_scope(self):
        original = self.startup_art_declaration.read_text()
        for field in ("source", "runtimePath"):
            with self.subTest(field=field):
                declaration = json.loads(original)
                declaration["assets"]["stellar-loading-splash"][field] = "../outside.png"
                self.startup_art_declaration.write_text(json.dumps(declaration))
                with self.assertRaisesRegex(RuntimeError, "Unreviewed native startup art"):
                    self.copy()
        self.startup_art_declaration.write_text(original)

    def test_startup_art_manifest_schema_and_set_are_strict(self):
        original = self.startup_art_declaration.read_text()
        declaration = json.loads(original)
        declaration["schemaVersion"] = 2
        self.startup_art_declaration.write_text(json.dumps(declaration))
        with self.assertRaisesRegex(RuntimeError, "Unsupported native startup art"):
            self.copy()
        declaration = json.loads(original)
        declaration["assets"]["extra"] = declaration["assets"]["stellar-loading-splash"]
        self.startup_art_declaration.write_text(json.dumps(declaration))
        with self.assertRaisesRegex(RuntimeError, "set differs from reviewed content"):
            self.copy()

    def test_missing_galaxy_art_blocks_package(self):
        for source, destination in NATIVE_GALAXY_ART_SOURCES.values():
            with self.subTest(source=source):
                path = self.root / source
                original = path.read_bytes()
                path.unlink()
                with self.assertRaisesRegex(RuntimeError, "Missing native galaxy art"):
                    self.copy()
                path.write_bytes(original)

    def test_tampered_galaxy_art_blocks_package(self):
        for source, destination in NATIVE_GALAXY_ART_SOURCES.values():
            with self.subTest(source=source):
                path = self.root / source
                original = path.read_bytes()
                path.write_bytes(b"altered")
                with self.assertRaisesRegex(RuntimeError, "differs from reviewed content"):
                    self.copy()
                path.write_bytes(original)

    def test_galaxy_art_paths_cannot_expand_package_scope(self):
        original = self.galaxy_art_declaration.read_text()
        for field in ("source", "runtimePath"):
            with self.subTest(field=field):
                declaration = json.loads(original)
                declaration["assets"]["deep-field-v2"][field] = "../outside.png"
                self.galaxy_art_declaration.write_text(json.dumps(declaration))
                with self.assertRaisesRegex(RuntimeError, "Unreviewed native galaxy art"):
                    self.copy()
        self.galaxy_art_declaration.write_text(original)

    def test_galaxy_art_manifest_schema_and_set_are_strict(self):
        original = self.galaxy_art_declaration.read_text()
        declaration = json.loads(original)
        declaration["schemaVersion"] = 2
        self.galaxy_art_declaration.write_text(json.dumps(declaration))
        with self.assertRaisesRegex(RuntimeError, "Unsupported native galaxy art"):
            self.copy()
        declaration = json.loads(original)
        declaration["assets"]["extra"] = declaration["assets"]["deep-field-v2"]
        self.galaxy_art_declaration.write_text(json.dumps(declaration))
        with self.assertRaisesRegex(RuntimeError, "set differs from reviewed content"):
            self.copy()

    def test_missing_ship_art_blocks_package(self):
        for source, destination in NATIVE_SHIP_ART_SOURCES.values():
            with self.subTest(source=source):
                path = self.root / source
                original = path.read_bytes()
                path.unlink()
                with self.assertRaisesRegex(RuntimeError, "Missing native ship art"):
                    self.copy()
                path.write_bytes(original)

    def test_tampered_ship_art_blocks_package(self):
        for source, destination in NATIVE_SHIP_ART_SOURCES.values():
            with self.subTest(source=source):
                path = self.root / source
                original = path.read_bytes()
                path.write_bytes(b"altered")
                with self.assertRaisesRegex(RuntimeError, "differs from reviewed content"):
                    self.copy()
                path.write_bytes(original)

    def test_ship_art_paths_cannot_expand_package_scope(self):
        original = self.ship_art_declaration.read_text()
        for field in ("source", "runtimePath"):
            with self.subTest(field=field):
                declaration = json.loads(original)
                declaration["assets"]["pathfinder-scout"][field] = "../outside.png"
                self.ship_art_declaration.write_text(json.dumps(declaration))
                with self.assertRaisesRegex(RuntimeError, "Unreviewed native ship art"):
                    self.copy()
        self.ship_art_declaration.write_text(original)

    def test_ship_art_manifest_schema_and_set_are_strict(self):
        original = self.ship_art_declaration.read_text()
        declaration = json.loads(original)
        declaration["schemaVersion"] = 2
        self.ship_art_declaration.write_text(json.dumps(declaration))
        with self.assertRaisesRegex(RuntimeError, "Unsupported native ship art"):
            self.copy()
        declaration = json.loads(original)
        declaration["assets"]["extra"] = declaration["assets"]["pathfinder-scout"]
        self.ship_art_declaration.write_text(json.dumps(declaration))
        with self.assertRaisesRegex(RuntimeError, "set differs from reviewed content"):
            self.copy()

    def test_missing_planet_art_blocks_package(self):
        for source, destination in NATIVE_PLANET_ART_SOURCES.values():
            with self.subTest(source=source):
                path = self.root / source
                original = path.read_bytes()
                path.unlink()
                with self.assertRaisesRegex(RuntimeError, "Missing native planet art"):
                    self.copy()
                path.write_bytes(original)

    def test_tampered_planet_art_blocks_package(self):
        for source, destination in NATIVE_PLANET_ART_SOURCES.values():
            with self.subTest(source=source):
                path = self.root / source
                original = path.read_bytes()
                path.write_bytes(b"altered")
                with self.assertRaisesRegex(RuntimeError, "differs from reviewed content"):
                    self.copy()
                path.write_bytes(original)

    def test_planet_art_paths_cannot_expand_package_scope(self):
        original = self.planet_art_declaration.read_text()
        for field in ("source", "runtimePath"):
            with self.subTest(field=field):
                declaration = json.loads(original)
                declaration["assets"]["gaia-world"][field] = "../outside.png"
                self.planet_art_declaration.write_text(json.dumps(declaration))
                with self.assertRaisesRegex(RuntimeError, "Unreviewed native planet art"):
                    self.copy()
        self.planet_art_declaration.write_text(original)

    def test_planet_art_manifest_schema_and_set_are_strict(self):
        original = self.planet_art_declaration.read_text()
        declaration = json.loads(original)
        declaration["schemaVersion"] = 2
        self.planet_art_declaration.write_text(json.dumps(declaration))
        with self.assertRaisesRegex(RuntimeError, "Unsupported native planet art"):
            self.copy()
        declaration = json.loads(original)
        declaration["assets"]["extra"] = declaration["assets"]["gaia-world"]
        self.planet_art_declaration.write_text(json.dumps(declaration))
        with self.assertRaisesRegex(RuntimeError, "set differs from reviewed content"):
            self.copy()

    def test_missing_license_blocks_package(self):
        self.license.unlink()
        with self.assertRaisesRegex(RuntimeError, "Missing native client dependency"):
            self.copy()

    def test_missing_celestial_credits_blocks_package(self):
        (self.root / "docs/SOL_VISUAL_SOURCES.md").unlink()
        with self.assertRaisesRegex(RuntimeError, "Missing native celestial credits"):
            self.copy()

    def test_tampered_planet_image_blocks_package(self):
        (self.root / "assets/visual/sol/earth.jpg").write_bytes(b"unreviewed replacement")
        with self.assertRaisesRegex(RuntimeError, "celestial earth differs from reviewed content"):
            self.copy()

    def test_unreviewed_celestial_path_cannot_escape_package(self):
        declaration = json.loads(self.celestial_declaration.read_text())
        declaration["assets"]["mars"]["runtimePath"] = "../outside.jpg"
        self.celestial_declaration.write_text(json.dumps(declaration))
        with self.assertRaisesRegex(RuntimeError, "Unreviewed native celestial mars path"):
            self.copy()

    def test_incomplete_celestial_declaration_blocks_package(self):
        declaration = json.loads(self.celestial_declaration.read_text())
        del declaration["assets"]["neptune"]
        self.celestial_declaration.write_text(json.dumps(declaration))
        with self.assertRaisesRegex(RuntimeError, "celestial asset set differs from reviewed content"):
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
    def exercise(self, *, mutate_load=False, funded=True, progressed=True,
                 skipped_save=False, missing_shortcut=False):
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
                shortcut = "" if missing_shortcut or "--load" in args else " shortcut=1"
                return subprocess.CompletedProcess(args, 0, "gpu_driver=vulkan systems=500 save=" + saved + " research=known:active:0.1" + shortcut, "")

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

    def test_missing_candidate_shortcut_evidence_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "candidate shortcuts"):
            self.exercise(missing_shortcut=True)


if __name__ == "__main__":
    unittest.main()
