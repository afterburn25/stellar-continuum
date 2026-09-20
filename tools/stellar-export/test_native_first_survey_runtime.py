import copy
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

import native_first_survey_runtime as runtime
from test_native_fresh_progression_runtime import payload as fresh_payload


def bmp(width, height):
    stride = (width * 3 + 3) & ~3
    pixels = (bytes(range(251)) * ((stride * height // 251) + 1))[:stride * height]
    return (b"BM" + struct.pack("<IHHI", 54 + len(pixels), 0, 0, 54) +
            struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0,
                        len(pixels), 2835, 2835, 0, 0) + pixels)


def source_payload():
    value = fresh_payload(100.0)
    galaxy = value["Galaxy"]
    galaxy["Civilizations"][0]["HomeSystemId"] = 0
    scout, science = galaxy["Fleets"]
    scout.update(CurrentSystemId=1, DestinationSystemId=None, TransitPhase=0,
                 TransitProgress=0.0, MissionOrderRevision=4,
                 PlannedRouteSystemIds=[], TransitOriginSystemId=None,
                 TransitTargetSystemId=None, FuelRemainingLightYears=9.0,
                 FuelCapacityLightYears=10.0, ReconnaissanceSystemId=1,
                 ReconnaissanceDaysCompleted=2.0)
    science.update(CurrentSystemId=0, DestinationSystemId=None, TransitPhase=0,
                  TransitProgress=0.0, MissionOrderRevision=3,
                  PlannedRouteSystemIds=[], TransitOriginSystemId=None,
                  TransitTargetSystemId=None, FuelRemainingLightYears=10.0,
                  FuelCapacityLightYears=10.0, HoldRequested=False,
                  ReturnToBaseRequested=False)
    galaxy["Knowledge"] = [{"CivilizationId": 0, "SystemSurveys": [
        {"SystemId": 1, "Level": 2, "Progress": .35}]}]
    galaxy["PlanetaryBodies"] = [{"Id": 30, "SystemId": 1, "ParentBodyId": None}]
    return value


def science(value):
    return next(row for row in value["Galaxy"]["Fleets"]
                if row["DesignId"] == "science_vessel")


def survey(value):
    row = value["Galaxy"]["Knowledge"][0]["SystemSurveys"][0]
    return row["Level"], row["Progress"]


def proof(mode, before, after, phases):
    previous, current = science(before), science(after)
    before_level, before_progress = survey(before)
    after_level, after_progress = survey(after)
    return {
        "mode": mode, "seed": 115501, "player_id": 0, "fleet_id": 8,
        "scout_id": 7, "body_id": 30, "origin_id": 0, "target_id": 1,
        "before_days": before["SimulationDays"], "after_days": after["SimulationDays"],
        "input_orders": 1 if mode == "depart" else 0,
        "steps": round((after["SimulationDays"] - before["SimulationDays"]) * 64),
        "step_days": 1 / 64, "revision_before": previous["MissionOrderRevision"],
        "revision_after": current["MissionOrderRevision"],
        "phase_before": previous["TransitPhase"], "phase_after": current["TransitPhase"],
        "survey_before": before_level, "survey_after": after_level,
        "survey_progress_before": before_progress, "survey_progress_after": after_progress,
        "transit_progress_before": previous["TransitProgress"],
        "transit_progress_after": current["TransitProgress"], "seen_phases": phases,
        "selected": True, "selection_read_only": True,
        "preview_read_only": mode == "depart", "lane_connected": True,
        "inspection_read_only": True, "facts_visible": mode == "resume" or (mode == "paused" and after_level == 3),
        "paused": True, "save_roundtrip": True,
    }


class NativeFirstSurveyRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir()
            source = root / "recon.json"
            initial = source_payload()
            if fault == "missing_prereq":
                initial["Galaxy"]["ConstructionStates"][0]["CompletedProjectIds"].pop()
            if fault == "missing_recon":
                initial["Galaxy"]["Fleets"][0]["ReconnaissanceDaysCompleted"] = 1.0
            source.write_text(json.dumps(initial), encoding="utf-8")
            calls = []

            def launch(args, **unused):
                save = Path(args[args.index("--save-path") + 1])
                before = json.loads(save.read_text())
                after = copy.deepcopy(before)
                flag = next(value for value in (
                    "--first-survey-smoke", "--first-survey-paused-smoke",
                    "--first-survey-resume-smoke") if value in args)
                mode = {"--first-survey-smoke": "depart",
                        "--first-survey-paused-smoke": "paused",
                        "--first-survey-resume-smoke": "resume"}[flag]
                vessel = science(after)
                if mode == "depart":
                    vessel.update(CurrentSystemId=1, DestinationSystemId=None, TransitPhase=0,
                                   TransitProgress=0.0, MissionOrderRevision=4,
                                   PlannedRouteSystemIds=[], FuelRemainingLightYears=9.0)
                    after["Galaxy"]["Knowledge"][0]["SystemSurveys"][0].update(Level=2, Progress=.5)
                    after["SimulationDays"] += 32 / 64
                elif mode == "resume":
                    vessel.update(CurrentSystemId=1, DestinationSystemId=None, TransitPhase=0,
                                   TransitProgress=0.0, PlannedRouteSystemIds=[], FuelRemainingLightYears=9.0)
                    after["Galaxy"]["Knowledge"][0]["SystemSurveys"][0].update(Level=3, Progress=1.0)
                    after["SimulationDays"] += 64 / 64
                after["SavedAtUtc"] = f"saved-{len(calls)}"
                report = proof(mode, before, after, [0, 1, 2, 3] if mode == "depart" else [0])
                if fault == "repeated_orders" and mode == "resume":
                    report["input_orders"] = 1
                if fault == "fake_completion" and mode == "resume":
                    after["Galaxy"]["Knowledge"][0]["SystemSurveys"][0]["Level"] = 2
                if fault == "mismatched_proof" and mode == "depart":
                    report["revision_after"] += 1
                if fault == "wrong_target" and mode == "depart":
                    report["target_id"] = 2
                if fault == "wrong_body" and mode == "depart":
                    report["body_id"] = 99
                if fault == "boolean_id" and mode == "depart":
                    report["body_id"] = True
                if fault == "nonfinite" and mode == "depart":
                    report["after_days"] = float("nan")
                if fault == "unhashable_phase" and mode == "depart":
                    report["seen_phases"] = [0, [1], 2, 3]
                if fault == "hidden_facts" and mode == "depart":
                    report["facts_visible"] = True
                if fault == "wrong_resume_source" and mode == "paused" and calls:
                    vessel["CurrentSystemId"] = 0
                if fault == "resume_fuel_loss" and mode == "resume":
                    vessel["FuelRemainingLightYears"] = 8.0
                if fault == "resume_fuel_gain" and mode == "resume":
                    vessel["FuelRemainingLightYears"] = 9.5
                if fault == "scout_changed" and mode == "resume":
                    after["Galaxy"]["Fleets"][0]["ReconnaissanceDaysCompleted"] = 3.0
                if fault == "paused_mutation" and mode == "paused" and calls:
                    after["Galaxy"]["Economies"][0]["Credits"] = 2
                save.write_text(json.dumps(after))
                capture = Path(args[-1])
                capture.write_bytes(bmp(int(args[args.index("--width") + 1]), int(args[args.index("--height") + 1])))
                if mode == "depart" and fault != "missing_image": capture.with_name(capture.stem + "-departure.bmp").write_bytes(capture.read_bytes())
                if mode == "resume": capture.with_name(capture.stem + "-inspection.bmp").write_bytes(capture.read_bytes())
                calls.append(args)
                return subprocess.CompletedProcess(args, 0, "gpu_driver=vulkan systems=500 save=ok \nfirst_survey=" + json.dumps(report, separators=(",", ":")), "")

            with mock.patch.object(runtime.subprocess, "run", side_effect=launch):
                if fault:
                    with self.assertRaises(RuntimeError): runtime.validate_native_first_survey_export(package, {}, source)
                    return
                result = runtime.validate_native_first_survey_export(package, {}, source)
            self.assertTrue(result["nativeFirstSurvey"])
            self.assertEqual(len(calls), 4)
            self.assertEqual(len(result["firstSurveyCaptures"]), 6)
            self.assertEqual(len(result["firstSurveySaveCaptures"]), 4)

    def test_complete_serial_survey(self): self.exercise()
    def test_rejects_mismatched_proof_save(self): self.exercise("mismatched_proof")
    def test_rejects_wrong_target(self): self.exercise("wrong_target")
    def test_rejects_wrong_body(self): self.exercise("wrong_body")
    def test_rejects_boolean_identity(self): self.exercise("boolean_id")
    def test_rejects_nonfinite_clock(self): self.exercise("nonfinite")
    def test_rejects_unhashable_phase(self): self.exercise("unhashable_phase")
    def test_rejects_missing_prerequisites(self): self.exercise("missing_prereq")
    def test_rejects_incomplete_reconnaissance_source(self): self.exercise("missing_recon")
    def test_rejects_repeated_orders(self): self.exercise("repeated_orders")
    def test_rejects_fake_completion(self): self.exercise("fake_completion")
    def test_rejects_hidden_facts_claimed_early(self): self.exercise("hidden_facts")
    def test_rejects_wrong_resume_source(self): self.exercise("wrong_resume_source")
    def test_rejects_resume_fuel_loss(self): self.exercise("resume_fuel_loss")
    def test_rejects_resume_fuel_gain(self): self.exercise("resume_fuel_gain")
    def test_rejects_missing_image(self): self.exercise("missing_image")
    def test_rejects_scout_mutation(self): self.exercise("scout_changed")
    def test_rejects_paused_mutation(self): self.exercise("paused_mutation")

    def test_launch_failure_includes_diagnostics(self):
        failed = subprocess.CompletedProcess([], 9, "stdout-tail", "stderr-tail")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with mock.patch.object(runtime.subprocess, "run", return_value=failed):
                with self.assertRaisesRegex(RuntimeError, "(?s)stdout-tail.*stderr-tail"):
                    runtime._launch(root, root, {}, root / "save.json", root / "capture.bmp",
                                    "--first-survey-smoke", 1280, 720)

    def test_rejects_ambiguous_saved_identities(self):
        for fault in ("boolean_survey", "duplicate_survey", "boolean_body_system",
                      "duplicate_system", "boolean_fleet"):
            with self.subTest(fault=fault):
                value = source_payload()
                report = proof("paused", value, value, [0])
                galaxy = value["Galaxy"]
                surveys = galaxy["Knowledge"][0]["SystemSurveys"]
                if fault == "boolean_survey":
                    surveys[0]["SystemId"] = True
                elif fault == "duplicate_survey":
                    surveys.append(copy.deepcopy(surveys[0]))
                elif fault == "boolean_body_system":
                    galaxy["PlanetaryBodies"][0]["SystemId"] = True
                elif fault == "duplicate_system":
                    galaxy["Systems"].append(copy.deepcopy(galaxy["Systems"][0]))
                else:
                    galaxy["Fleets"][0]["Id"] = True
                with self.assertRaises(RuntimeError):
                    runtime._state(value, report)


if __name__ == "__main__":
    unittest.main()
