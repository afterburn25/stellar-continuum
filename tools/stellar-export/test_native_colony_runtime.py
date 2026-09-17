import copy
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

from native_colony_runtime import validate_native_colony_export


def bmp(width, height):
    stride = (width * 3 + 3) & ~3
    length = stride * height
    pattern = bytes(range(251))
    pixels = (pattern * (length // len(pattern) + 1))[:length]
    size = 54 + len(pixels)
    return (b"BM" + struct.pack("<IHHI", size, 0, 0, 54) +
            struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0,
                        len(pixels), 2835, 2835, 0, 0) + pixels)


def payload(saved="one"):
    systems = [{"Id": index} for index in range(500)]
    return {"FormatVersion": 17, "SavedAtUtc": saved, "SimulationDays": 0,
            "Galaxy": {"PlayerCivilizationId": 0, "Systems": systems,
                       "PlanetaryBodies": [{"Id": 3, "SystemId": 0}],
                       "Colonies": [{"Id": 4, "CivilizationId": 0,
                                      "SystemId": 0, "PlanetaryBodyId": 3,
                                      "PopulationMillions": 10,
                                      "StoredFoodPopulationDaysMillions": 300,
                                      "StoredWaterPopulationDaysMillions": 70,
                                      "SurfaceBuildings": [{"Id": 8}]}],
                       "Knowledge": [{"CivilizationId": 0,
                                      "KnownSystemIds": [0],
                                      "SystemSurveys": [{"SystemId": 0,
                                                         "Level": 3,
                                                         "Progress": 1}]}]}}


def state(mode):
    return {"mode": mode, "player_id": 0, "system_id": 0, "body_id": 3,
            "colony_id": 4, "revision": 1, "site_count": 1,
            "population_millions": 10.0, "support_ratio": 1.0,
            "power_supply": 2.0, "power_demand": 1.0,
            "food_reserve_days": 30.0, "water_reserve_days": 7.0,
            **{key: True for key in ("selected", "opened", "back_restored",
                                      "pause_retained", "speed_retained",
                                      "paused", "day_unchanged")}}


def roster():
    return {"player_id": 0, "colony_id": 4, "rows": 1,
            "opened": True, "selected": True, "readonly": True,
            "exclusive": True, "scrolled": True}


class NativeColonyRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary) / "package"
            package.mkdir()
            calls = []

            def run(args, *, cwd, env, **unused):
                self.assertNotEqual(Path(cwd), package)
                self.assertNotIn(str(package), env.get("PATH", ""))
                if fault == "launch":
                    return subprocess.CompletedProcess(args, 7, "caught-output", "terminal-error")
                save = Path(args[args.index("--save-path") + 1])
                reload = "--colony-reload-smoke" in args
                mode = "paused_reload" if reload else "fresh"
                capture = Path(args[args.index("--colony-reload-smoke" if reload else "--colony-smoke") + 1])
                current = json.loads(save.read_text()) if reload else payload()
                current["SavedAtUtc"] = f"saved-{len(calls)}"
                report = state(mode)
                proof = roster()
                if fault in report:
                    report[fault] = False if isinstance(report[fault], bool) else -1
                if fault == "nonfinite": report["support_ratio"] = float("nan")
                if fault == "owner": report["player_id"] = 1
                if fault == "location": report["body_id"] = 9
                if fault == "sites": report["site_count"] = 2
                if fault == "population": report["population_millions"] = 11
                if fault == "forecast": report["food_reserve_days"] = 29
                if fault == "knowledge": current["Galaxy"]["Knowledge"][0]["SystemSurveys"] = []
                if fault == "known_ids": current["Galaxy"]["Knowledge"][0]["KnownSystemIds"] = []
                if fault == "clock": current["SimulationDays"] = 1
                if fault == "reload" and reload:
                    current["Galaxy"]["Colonies"][0]["Infrastructure"] = .5
                save.write_text(json.dumps(current), encoding="utf-8")
                if fault != "capture":
                    image = bmp(1279 if fault == "geometry" else int(args[args.index("--width") + 1]),
                                int(args[args.index("--height") + 1]))
                    if fault == "truncated":
                        image = bytearray(image[:-128])
                        struct.pack_into("<I", image, 2, len(image))
                        image = bytes(image)
                    capture.write_bytes(image)
                roster_capture = capture.with_name(f"{capture.stem}-colony-roster.bmp")
                if fault != "roster_missing_sidecar":
                    roster_capture.write_bytes(bmp(
                        1279 if fault == "roster_geometry" else int(args[args.index("--width") + 1]),
                        int(args[args.index("--height") + 1])))
                if fault == "roster_false": proof["selected"] = False
                if fault == "roster_count": proof["rows"] = 2
                if fault == "roster_identity": proof["colony_id"] = 99
                if fault == "roster_missing":
                    roster_line = ""
                else:
                    roster_line = "colony_roster=" + json.dumps(proof, separators=(",", ":"))
                if fault == "roster_duplicate":
                    roster_line = roster_line + "\n" + roster_line
                uploads = 0 if fault == "uploads" else 4
                driver = "software" if fault == "renderer" else "vulkan"
                dimensions = {"path": str(capture),
                              "width": int(args[args.index("--width") + 1]),
                              "height": int(args[args.index("--height") + 1])}
                roster_dimensions = {"path": str(roster_capture),
                                     "width": int(args[args.index("--width") + 1]),
                                     "height": int(args[args.index("--height") + 1])}
                stdout = (f"gpu_driver={driver} systems=500 image_uploads={uploads} "
                          f"save=ok colony={json.dumps(report, separators=(',', ':'))}\n"
                          f"native_capture={json.dumps(dimensions, separators=(',', ':'))}\n"
                          f"native_capture={json.dumps(roster_dimensions, separators=(',', ':'))}\n"
                          f"{roster_line}")
                calls.append(args)
                return subprocess.CompletedProcess(args, 0, stdout, "")

            with mock.patch("native_colony_runtime.subprocess.run", side_effect=run):
                result = validate_native_colony_export(package, {})
            self.assertEqual(len(calls), 2)
            self.assertNotIn("--load", calls[0])
            self.assertIn("--load", calls[1])
            self.assertTrue(result["nativeColonyPlayerInput"])
            self.assertTrue(result["nativeColonyPausedReload"])
            self.assertTrue(result["nativeColonyRoster"])
            self.assertEqual(len(result["colonyCaptures"]), 2)
            self.assertEqual(len(result["colonyRosterCaptures"]), 2)
            self.assertEqual(len(result["colonyDiagnostics"]), 2)

    def test_two_launch_player_input_and_paused_reload(self): self.exercise()
    def test_body_selection_is_required(self):
        with self.assertRaises(RuntimeError): self.exercise("selected")
    def test_colony_open_is_required(self):
        with self.assertRaises(RuntimeError): self.exercise("opened")
    def test_back_routing_is_required(self):
        with self.assertRaises(RuntimeError): self.exercise("back_restored")
    def test_pause_routing_is_required(self):
        with self.assertRaises(RuntimeError): self.exercise("pause_retained")
    def test_speed_routing_is_required(self):
        with self.assertRaises(RuntimeError): self.exercise("speed_retained")
    def test_final_pause_is_required(self):
        with self.assertRaises(RuntimeError): self.exercise("paused")
    def test_diagnostic_day_stability_is_required(self):
        with self.assertRaises(RuntimeError): self.exercise("day_unchanged")
    def test_nonfinite_telemetry_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("nonfinite")
    def test_saved_owner_must_match(self):
        with self.assertRaises(RuntimeError): self.exercise("owner")
    def test_saved_location_must_match(self):
        with self.assertRaises(RuntimeError): self.exercise("location")
    def test_site_count_must_match(self):
        with self.assertRaises(RuntimeError): self.exercise("sites")
    def test_population_must_match(self):
        with self.assertRaises(RuntimeError): self.exercise("population")
    def test_tomorrow_reserve_forecast_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("forecast")
    def test_unknown_system_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("knowledge")
    def test_survey_without_known_system_identity_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("known_ids")
    def test_time_advance_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("clock")
    def test_reload_payload_change_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("reload")
    def test_missing_capture_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("capture")
    def test_capture_geometry_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("geometry")
    def test_truncated_pixel_rows_are_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("truncated")
    def test_non_vulkan_renderer_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("renderer")
    def test_missing_image_uploads_are_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("uploads")
    def test_missing_roster_proof_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("roster_missing")
    def test_duplicate_roster_proof_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("roster_duplicate")
    def test_false_roster_proof_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("roster_false")
    def test_forged_roster_count_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("roster_count")
    def test_forged_roster_identity_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("roster_identity")
    def test_missing_roster_sidecar_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("roster_missing_sidecar")
    def test_roster_sidecar_geometry_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("roster_geometry")
    def test_failed_launch_reports_stdout_and_stderr(self):
        with self.assertRaisesRegex(RuntimeError, "(?s)caught-output.*terminal-error"):
            self.exercise("launch")


if __name__ == "__main__":
    unittest.main()
