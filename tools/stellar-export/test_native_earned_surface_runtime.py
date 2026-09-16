import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import native_earned_surface_runtime as runtime
from test_native_bmp import bmp


def source():
    return {"FormatVersion": 17, "SavedAtUtc": "source", "SimulationDays": 6762.4134387,
            "Galaxy": {"Seed": 115501, "PlayerCivilizationId": 0,
                       "Economies": [{"CivilizationId": 0, "Credits": 3873.798, "Industry": 154.2}],
                       "Colonies": [{"Id": 0, "CivilizationId": 0, "SystemId": 0, "PlanetaryBodyId": 3, "SurfaceBuildings": []},
                                    {"Id": 9, "CivilizationId": 0, "SystemId": 8, "PlanetaryBodyId": 8004,
                                     "PopulationMillions": 250.0, "SurfaceBuildings": []}],
                       "Fleets": [], "ConstructionStates": []}}


def proof(mode, before, after):
    paused = mode == "paused"
    return {"mode": mode, "player_id": 0, "system_id": 8, "body_id": 8004, "colony_id": 9,
            "building_id": 12, "type_id": "fabricator", "x": 7.0, "z": 35.0, "rotation": 0.0,
            "before_days": before["SimulationDays"], "after_days": after["SimulationDays"],
            "steps": 0 if paused else 64, "step_days": 1 / 64, "authorization": 0 if paused else 50,
            "treasury_before": 3823.798 if paused else 3873.798,
            "treasury_after": 3823.798, "industry_cost": 450.0, "industry_progress": 450.0,
            "complete": True, "powered": True, "staffed": True, "enabled": True, "efficiency": 1.0,
            "industry_before": 1.0, "industry_after": 1.0 if paused else 2.0,
            "cancel_unchanged": True, "opened_surface": True, "roundtrip": True}


class EarnedSurfaceRuntimeTests(unittest.TestCase):
    def completed(self):
        before = source(); after = copy.deepcopy(before); after["SimulationDays"] += 1
        after["SavedAtUtc"] = "completed"; after["Galaxy"]["Economies"][0]["Credits"] -= 50
        after["Galaxy"]["Colonies"][1]["SurfaceBuildings"] = [{"Id": 12, "TypeId": "fabricator", "X": 7.0, "Z": 35.0,
            "RotationDegrees": 0.0, "IndustryProgress": 450.0, "IsComplete": True, "IsEnabled": True}]
        return before, after

    def test_resume_proof_and_saved_site_bind(self):
        before, after = self.completed(); report = proof("resume", before, after)
        runtime._proof("earned_surface=" + json.dumps(report), "resume")
        runtime._bind_resume_source(report, runtime._source_state(before))
        runtime._saved_site(after, report, before)

    def test_rejects_forged_source_time_or_treasury(self):
        before, after = self.completed(); report = proof("resume", before, after)
        state = runtime._source_state(before)
        for key, value in (("before_days", report["before_days"] + 1),
                           ("treasury_before", report["treasury_before"] - 1)):
            forged = copy.deepcopy(report); forged[key] = value
            with self.assertRaises(RuntimeError): runtime._bind_resume_source(forged, state)

    def test_rejects_bad_cost_order_and_delta(self):
        before, after = self.completed()
        for change in (lambda p: p.update(industry_cost=449),
                       lambda p: p.update(authorization=49),
                       lambda p: p.update(treasury_after=3824.798),
                       lambda p: p.update(steps=63)):
            value = proof("resume", before, after); change(value)
            with self.assertRaises(RuntimeError): runtime._proof("earned_surface=" + json.dumps(value), "resume")

    def test_rejects_wrong_body_incomplete_and_foreign_site(self):
        before, after = self.completed(); report = proof("resume", before, after)
        bad = copy.deepcopy(report); bad["body_id"] = 8005
        with self.assertRaises(RuntimeError): runtime._proof("earned_surface=" + json.dumps(bad), "resume")
        incomplete = copy.deepcopy(after); incomplete["Galaxy"]["Colonies"][1]["SurfaceBuildings"][0]["IsComplete"] = False
        with self.assertRaises(RuntimeError): runtime._saved_site(incomplete, report, before)
        foreign = copy.deepcopy(after); foreign["Galaxy"]["Colonies"][1]["SurfaceBuildings"][0]["Id"] = 99
        with self.assertRaises(RuntimeError): runtime._saved_site(foreign, report, before)

    def test_rejects_same_count_persistent_entity_identity_swap(self):
        before, after = self.completed()
        before["Galaxy"]["Fleets"] = [{"Id": 1, "CivilizationId": 0}, {"Id": 2, "CivilizationId": 1}]
        after["Galaxy"]["Fleets"] = [{"Id": 2, "CivilizationId": 0}, {"Id": 1, "CivilizationId": 1}]
        with self.assertRaises(RuntimeError): runtime._saved_site(after, proof("resume", before, after), before)

    def test_paused_requires_actual_persisted_output_and_no_mutation(self):
        before, after = self.completed(); paused = copy.deepcopy(after); paused["SavedAtUtc"] = "paused"
        report = proof("paused", after, paused)
        report["industry_before"] = report["industry_after"] = 2.0
        runtime._proof("earned_surface=" + json.dumps(report), "paused")
        runtime._bind_paused(report, proof("resume", before, after), paused)
        report["industry_after"] = 1.0
        with self.assertRaises(RuntimeError): runtime._proof("earned_surface=" + json.dumps(report), "paused")

    def test_rejects_null_boolean_and_duplicate_proof_fields(self):
        before, after = self.completed(); value = proof("resume", before, after); value["x"] = None
        with self.assertRaises(RuntimeError): runtime._proof("earned_surface=" + json.dumps(value), "resume")
        raw = json.dumps(proof("resume", before, after), separators=(",", ":")).replace('"mode":', '"mode":"resume","mode":', 1)
        with self.assertRaises(RuntimeError): runtime._proof("earned_surface=" + raw, "resume")

    def test_source_rejects_wrong_player_and_modules(self):
        value = source(); value["Galaxy"]["PlayerCivilizationId"] = 1
        with self.assertRaises(RuntimeError): runtime._source_state(value)
        value = source(); value["Galaxy"]["Colonies"][1]["SurfaceBuildings"] = [{}]
        with self.assertRaises(RuntimeError): runtime._source_state(value)

    def test_public_api_copies_source_runs_resume_then_immutable_paused_reload(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); package = root / "package"; package.mkdir()
            (package / "stellar-continuum-native.exe").write_text("placeholder")
            path = (root / "source.player17.json").resolve(); initial = source()
            path.write_text(json.dumps(initial), encoding="utf-8"); calls = []
            def launch(args, **unused):
                save = Path(args[args.index("--save-path") + 1]); before = json.loads(save.read_text())
                flag = "--earned-surface-smoke" if "--earned-surface-smoke" in args else "--earned-surface-paused-smoke"
                if flag == "--earned-surface-smoke":
                    after = copy.deepcopy(before); after["SimulationDays"] += 1; after["SavedAtUtc"] = "completed"
                    after["Galaxy"]["Economies"][0]["Credits"] -= 50
                    after["Galaxy"]["Colonies"][1]["SurfaceBuildings"] = [{"Id": 12, "TypeId": "fabricator", "X": 7., "Z": 35., "RotationDegrees": 0., "IndustryProgress": 450., "IsComplete": True, "IsEnabled": True}]
                    report = proof("resume", before, after)
                    save.write_text(json.dumps(after))
                else:
                    after = copy.deepcopy(before); after["SavedAtUtc"] = "paused"; report = proof("paused", before, after); report["industry_before"] = report["industry_after"] = 2.
                    save.write_text(json.dumps(after))
                capture = Path(args[-1]); width = int(args[args.index("--width") + 1]); height = int(args[args.index("--height") + 1]); capture.write_bytes(bmp(width, height))
                if flag == "--earned-surface-smoke":
                    capture.with_name(capture.stem + "-colony.bmp").write_bytes(bmp(width, height))
                    capture.with_name(capture.stem + "-review.bmp").write_bytes(bmp(width, height)); capture.with_name(capture.stem + "-construction.bmp").write_bytes(bmp(width, height))
                calls.append(flag); return subprocess.CompletedProcess(args, 0, "gpu_driver=vulkan systems=500 save=ok \nearned_surface=" + json.dumps(report), "")
            with mock.patch.object(runtime.subprocess, "run", side_effect=launch):
                result = runtime.validate_native_earned_surface_export(package, {}, path)
            self.assertEqual(calls, ["--earned-surface-smoke", "--earned-surface-paused-smoke"])
            self.assertTrue(result["nativeEarnedSurface"]); self.assertEqual(len(result["earnedSurfaceCaptures"]), 5)
            self.assertEqual(path.read_text(encoding="utf-8"), json.dumps(initial))


if __name__ == "__main__":
    unittest.main()
