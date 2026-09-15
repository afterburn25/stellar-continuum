import copy
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

from native_new_game_runtime import validate_native_new_game_export


def fixture(path):
    source = {"FormatVersion": 17, "SavedAtUtc": "fixture", "SimulationDays": 2,
              "Galaxy": {"Systems": [{"Id": 0}]}}
    path.write_text(json.dumps({"Rows": [{"Name": "valid-current17",
                                          "InputJson": json.dumps(source)}]}),
                    encoding="utf-8")


def payload():
    return {"FormatVersion": 17, "SavedAtUtc": "fresh", "SimulationDays": .25,
            "Galaxy": {"Seed": 143250,
                       "GenerationMetadata": {"EnteredSeed": "143250",
                           "InternalSeed": 143250, "SystemCount": 250,
                           "PlayerSpeciesId": "pelagic_high_pressure"},
                       "Systems": [{"Id": i} for i in range(250)],
                       "PlayerCivilizationId": 7,
                       "Civilizations": [{"Id": 7, "IsPlayer": True,
                           "SpeciesId": "pelagic_high_pressure"}]}}


def bmp(width, height):
    stride = (width * 3 + 3) & ~3
    length = stride * height
    pixels = (bytes(range(251)) * (length // 251 + 1))[:length]
    size = 54 + length
    return (b"BM" + struct.pack("<IHHI", size, 0, 0, 54) +
            struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0,
                        length, 2835, 2835, 0, 0) + pixels)


class NativeNewGameRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None, *, audio_check=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); package = root / "package"; package.mkdir()
            source = root / "fixture.json"; fixture(source)
            calls = []; fresh_payload = payload()

            def run(args, *, cwd, env, **unused):
                self.assertNotEqual(Path(cwd), package)
                self.assertNotIn(str(package), env.get("PATH", ""))
                calls.append(args)
                save = Path(args[args.index("--save-path") + 1])
                width = int(args[args.index("--width") + 1])
                height = int(args[args.index("--height") + 1])
                if fault == "failed":
                    return subprocess.CompletedProcess(args, 9, "startup-output", "terminal-error")
                if "--new-game-smoke" in args:
                    capture = Path(args[args.index("--new-game-smoke") + 1])
                    setup = capture.with_name(capture.stem + "-setup.bmp")
                    loading = capture.with_name(capture.stem + "-loading.bmp")
                    generated = save.with_name(save.name.removesuffix(".player17.json") +
                                               "-native-1.player17.json")
                    state = {"mode": "fresh", "entry_opened": True,
                             "setup_opened": True, "species_selected": True,
                             "size_selected": True, "seed_entered": True,
                             "create_requested": True, "indeterminate_observed": True,
                             "activated": True, "saved": True,
                             "species_id": "pelagic_high_pressure",
                             "system_count": 250, "seed": "143250",
                             "requested_save_path": str(save),
                             "generated_save_path": str(generated), "unique_slot": True,
                             "setup_screenshot": str(setup),
                             "loading_screenshot": str(loading),
                             "statuses": ["Generating the campaign"]}
                    data = copy.deepcopy(fresh_payload)
                    if fault in state: state[fault] = False
                    if fault == "wrong_seed": data["Galaxy"]["Seed"] = 2
                    if fault == "wrong_species": data["Galaxy"]["Civilizations"][0]["SpeciesId"] = "terran_baseline"
                    if fault == "wrong_count": data["Galaxy"]["Systems"].pop()
                    if fault == "metadata": data["Galaxy"]["GenerationMetadata"]["SystemCount"] = 500
                    if fault == "nonfinite": data["SimulationDays"] = float("nan")
                    if fault == "format": data["FormatVersion"] = 16
                    if fault == "extra": state["hidden"] = "value"
                    if fault == "statuses": state["statuses"] = []
                    if fault == "path_escape": state["generated_save_path"] = str(package / "escaped.player17.json")
                    if fault == "setup_path": state["setup_screenshot"] = str(capture)
                    if fault != "missing_save": generated.write_text(json.dumps(data), encoding="utf-8")
                    if fault == "overwrite": save.write_text("changed", encoding="utf-8")
                    for name, image_path in (("setup", setup), ("loading", loading), ("final", capture)):
                        if fault == "missing_capture" and name == "final": continue
                        image = bmp(width - 1 if fault == "geometry" and name == "final" else width, height)
                        if fault == "blank" and name == "setup": image = image[:54] + bytes(len(image) - 54)
                        if fault == "truncated" and name == "loading":
                            image = bytearray(image[:-32]); struct.pack_into("<I", image, 2, len(image)); image = bytes(image)
                        image_path.write_bytes(image)
                    stdout = ((("audio_check=" + json.dumps({"assets_loaded": True, "music_starts": 1,
                                "confirm_count": 2, "queued_music_bytes": 2048,
                                "boot_services": 3, "stopped": True}, separators=(",", ":")) + "\n") if audio_check else "") +
                              "gpu_driver=vulkan systems=250 image_uploads=4 save=ok new_game=" + json.dumps(state, separators=(",", ":")))
                else:
                    generated = save
                    data = json.loads(generated.read_text())
                    data["SavedAtUtc"] = "reload"
                    if fault == "reload_change": data["Galaxy"]["Mutation"] = True
                    if fault == "reload_missing": generated.unlink()
                    else: generated.write_text(json.dumps(data), encoding="utf-8")
                    capture = Path(args[args.index("--smoke") + 1])
                    image = bmp(width - 1 if fault == "reload_geometry" else width, height)
                    capture.write_bytes(image)
                    stdout = (("audio_check=" + json.dumps({"assets_loaded": True, "music_starts": 1,
                               "confirm_count": 0, "queued_music_bytes": 2048,
                               "boot_services": 0, "stopped": True}, separators=(",", ":")) + "\n"
                              if audio_check else "") +
                              "gpu_driver=vulkan systems=250 image_uploads=4 save=ok")
                if fault == "renderer": stdout = stdout.replace("gpu_driver=vulkan", "gpu_driver=software")
                if fault == "diagnostic_count": stdout = stdout.replace("systems=250", "systems=500")
                return subprocess.CompletedProcess(args, 0, stdout, "")

            with mock.patch("native_new_game_runtime.subprocess.run", side_effect=run):
                result = validate_native_new_game_export(package, {}, source, audio_check=audio_check)
            self.assertEqual(len(calls), 2)
            self.assertIn("--new-game-smoke", calls[0]); self.assertNotIn("--load", calls[0])
            self.assertIn("--load", calls[1]); self.assertIn("--smoke", calls[1])
            self.assertTrue(result["nativeNewGamePlayerInput"])
            self.assertTrue(result["nativeNewGameIndependentSave"])
            self.assertTrue(result["nativeNewGamePausedReload"])
            self.assertEqual(len(result["newGameCaptures"]), 4)
            if audio_check:
                self.assertIn("--audio-check", calls[0])
                self.assertIn("--audio-check", calls[1])
                self.assertTrue(result["nativeNewGameAudioCheck"])
                self.assertEqual(result["newGameAudioChecks"]["reload"]["confirm_count"], 0)
            else:
                self.assertNotIn("nativeNewGameAudioCheck", result)
                self.assertNotIn("--audio-check", calls[0])
                self.assertNotIn("--audio-check", calls[1])

    def test_valid(self): self.exercise()
    def test_optional_audio_check_runs_fresh_and_reload(self): self.exercise(audio_check=True)
    def test_entry_required(self):
        with self.assertRaises(RuntimeError): self.exercise("entry_opened")
    def test_setup_required(self):
        with self.assertRaises(RuntimeError): self.exercise("setup_opened")
    def test_species_input_required(self):
        with self.assertRaises(RuntimeError): self.exercise("species_selected")
    def test_size_input_required(self):
        with self.assertRaises(RuntimeError): self.exercise("size_selected")
    def test_seed_input_required(self):
        with self.assertRaises(RuntimeError): self.exercise("seed_entered")
    def test_create_required(self):
        with self.assertRaises(RuntimeError): self.exercise("create_requested")
    def test_indeterminate_required(self):
        with self.assertRaises(RuntimeError): self.exercise("indeterminate_observed")
    def test_activation_required(self):
        with self.assertRaises(RuntimeError): self.exercise("activated")
    def test_save_required(self):
        with self.assertRaises(RuntimeError): self.exercise("saved")
    def test_unique_slot_required(self):
        with self.assertRaises(RuntimeError): self.exercise("unique_slot")
    def test_wrong_seed(self):
        with self.assertRaises(RuntimeError): self.exercise("wrong_seed")
    def test_wrong_species(self):
        with self.assertRaises(RuntimeError): self.exercise("wrong_species")
    def test_wrong_count(self):
        with self.assertRaises(RuntimeError): self.exercise("wrong_count")
    def test_wrong_metadata(self):
        with self.assertRaises(RuntimeError): self.exercise("metadata")
    def test_nonfinite(self):
        with self.assertRaises(RuntimeError): self.exercise("nonfinite")
    def test_overwrite(self):
        with self.assertRaises(RuntimeError): self.exercise("overwrite")
    def test_escape(self):
        with self.assertRaises(RuntimeError): self.exercise("path_escape")
    def test_missing_save(self):
        with self.assertRaises(RuntimeError): self.exercise("missing_save")
    def test_changed_reload(self):
        with self.assertRaises(RuntimeError): self.exercise("reload_change")
    def test_missing_reload(self):
        with self.assertRaises(RuntimeError): self.exercise("reload_missing")
    def test_missing_capture(self):
        with self.assertRaises(RuntimeError): self.exercise("missing_capture")
    def test_bad_geometry(self):
        with self.assertRaises(RuntimeError): self.exercise("geometry")
    def test_truncated(self):
        with self.assertRaises(RuntimeError): self.exercise("truncated")
    def test_blank(self):
        with self.assertRaises(RuntimeError): self.exercise("blank")
    def test_reload_geometry(self):
        with self.assertRaises(RuntimeError): self.exercise("reload_geometry")
    def test_missing_vulkan(self):
        with self.assertRaises(RuntimeError): self.exercise("renderer")
    def test_wrong_diagnostic_count(self):
        with self.assertRaises(RuntimeError): self.exercise("diagnostic_count")
    def test_extra_schema(self):
        with self.assertRaises(RuntimeError): self.exercise("extra")
    def test_status_required(self):
        with self.assertRaises(RuntimeError): self.exercise("statuses")
    def test_arbitrary_setup_path(self):
        with self.assertRaises(RuntimeError): self.exercise("setup_path")
    def test_launch_error_includes_streams(self):
        with self.assertRaises(RuntimeError) as caught:
            self.exercise("failed")
        self.assertIn("startup-output", str(caught.exception))
        self.assertIn("terminal-error", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
