import copy
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

import native_first_exploration_runtime as runtime
from test_native_fresh_progression_runtime import payload as fresh_payload


def bmp(width, height):
    stride = (width * 3 + 3) & ~3
    pixels = bytes(range(251)) * ((stride * height // 251) + 1)
    pixels = pixels[:stride * height]
    return (b"BM" + struct.pack("<IHHI", 54 + len(pixels), 0, 0, 54) +
            struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0,
                        len(pixels), 2835, 2835, 0, 0) + pixels)


def payload():
    result = fresh_payload(100.0)
    player = result["Galaxy"]["Civilizations"][0]
    player["HomeSystemId"] = 0
    scout, science = result["Galaxy"]["Fleets"]
    scout.update({
        "CurrentSystemId": 0,
        "DestinationSystemId": None,
        "TransitPhase": 0,
        "TransitProgress": 0.0,
        "MissionOrderRevision": 4,
        "PlannedRouteSystemIds": [],
        "TransitOriginSystemId": None,
        "TransitTargetSystemId": None,
        "FuelRemainingLightYears": 10.0,
        "FuelCapacityLightYears": 10.0,
        "ReconnaissanceSystemId": None,
        "ReconnaissanceDaysCompleted": 0.0,
    })
    science.update({
        "CurrentSystemId": 0,
        "DestinationSystemId": None,
        "TransitPhase": 0,
        "TransitProgress": 0.0,
        "MissionOrderRevision": 0,
        "PlannedRouteSystemIds": [],
        "TransitOriginSystemId": None,
        "TransitTargetSystemId": None,
        "FuelRemainingLightYears": 10.0,
        "FuelCapacityLightYears": 10.0,
        "ReconnaissanceSystemId": None,
        "ReconnaissanceDaysCompleted": 0.0,
    })
    result["Galaxy"]["Knowledge"] = [{
        "CivilizationId": 0,
        "SystemSurveys": [{"SystemId": 1, "Level": 1, "Progress": 0.0}],
    }]
    return result


def survey(payload_value):
    rows = payload_value["Galaxy"]["Knowledge"][0]["SystemSurveys"]
    row = next(value for value in rows if value["SystemId"] == 1)
    return row["Level"], row["Progress"]


def proof(mode, before, after, phases):
    before_scout = next(row for row in before["Galaxy"]["Fleets"]
                        if row.get("CivilizationId") == 0 and
                        row.get("DesignId") == "warp_scout")
    after_scout = next(row for row in after["Galaxy"]["Fleets"]
                       if row.get("CivilizationId") == 0 and
                       row.get("DesignId") == "warp_scout")
    before_level, before_progress = survey(before)
    after_level, after_progress = survey(after)
    return {
        "mode": mode,
        "seed": 115501,
        "player_id": 0,
        "fleet_id": 7,
        "origin_id": 0,
        "target_id": 1,
        "before_days": before["SimulationDays"],
        "after_days": after["SimulationDays"],
        "input_orders": 1 if mode == "depart" else 0,
        "steps": round((after["SimulationDays"] -
                        before["SimulationDays"]) * 64),
        "step_days": 1 / 64,
        "revision_before": before_scout["MissionOrderRevision"],
        "revision_after": after_scout["MissionOrderRevision"],
        "phase_before": before_scout["TransitPhase"],
        "phase_after": after_scout["TransitPhase"],
        "survey_before": before_level,
        "survey_after": after_level,
        "survey_progress_before": before_progress,
        "survey_progress_after": after_progress,
        "transit_progress_before": before_scout["TransitProgress"],
        "transit_progress_after": after_scout["TransitProgress"],
        "seen_phases": phases,
        "selected": True,
        "selection_read_only": True,
        "preview_read_only": mode == "depart",
        "lane_connected": True,
        "paused": True,
        "save_roundtrip": True,
    }


class NativeFirstExplorationRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir()
            source = root / "earned.json"
            initial = payload()
            if fault == "foreign_scout":
                foreign = copy.deepcopy(initial["Galaxy"]["Fleets"][0])
                foreign.update(Id=1, CivilizationId=9)
                initial["Galaxy"]["Fleets"].insert(0, foreign)
            if fault == "extra_owned":
                extra = copy.deepcopy(initial["Galaxy"]["Fleets"][0])
                extra["Id"] = 9
                initial["Galaxy"]["Fleets"].append(extra)
            if fault == "bad_fresh_contract":
                initial["Galaxy"]["ConstructionStates"][0][
                    "CompletedProjectIds"].pop()
            if fault == "serialized_direct_lane":
                initial["Galaxy"]["Lanes"] = [
                    {"FirstSystemId": 0, "SecondSystemId": 1}]
            if fault == "serialized_wrong_lane":
                initial["Galaxy"]["Lanes"] = [
                    {"FirstSystemId": 0, "SecondSystemId": 2}]
            source.write_text(json.dumps(initial), encoding="utf-8")
            calls = []

            def launch(args, **unused):
                save = Path(args[args.index("--save-path") + 1])
                before = json.loads(save.read_text(encoding="utf-8"))
                after = copy.deepcopy(before)
                flag = next(value for value in (
                    "--first-exploration-smoke",
                    "--first-exploration-paused-smoke",
                    "--first-exploration-resume-smoke") if value in args)
                mode = {
                    "--first-exploration-smoke": "depart",
                    "--first-exploration-paused-smoke": "paused",
                    "--first-exploration-resume-smoke": "resume",
                }[flag]
                scout = next(row for row in after["Galaxy"]["Fleets"]
                             if row.get("CivilizationId") == 0 and
                             row.get("DesignId") == "warp_scout")
                if mode == "depart":
                    scout.update({
                        "CurrentSystemId": None,
                        "DestinationSystemId": 1,
                        "TransitOriginSystemId": 0,
                        "TransitTargetSystemId": 1,
                        "TransitPhase": 2,
                        "TransitProgress": 0.048,
                        "PlannedRouteSystemIds": [1],
                        "FuelRemainingLightYears": 9.0,
                        "MissionOrderRevision": 5,
                    })
                    after["SimulationDays"] += 54 / 64
                elif mode == "resume":
                    scout.update({
                        "CurrentSystemId": 1,
                        "DestinationSystemId": None,
                        "TransitOriginSystemId": None,
                        "TransitTargetSystemId": None,
                        "TransitPhase": 0,
                        "TransitProgress": 0.0,
                        "PlannedRouteSystemIds": [],
                        "ReconnaissanceSystemId": 1,
                        "ReconnaissanceDaysCompleted": 2.0,
                    })
                    target = after["Galaxy"]["Knowledge"][0]["SystemSurveys"][0]
                    target.update(Level=2, Progress=0.35)
                    after["SimulationDays"] += 193 / 64
                after["SavedAtUtc"] = f"saved-{len(calls)}"
                report = proof(
                    mode, before, after,
                    [0, 1, 2] if mode == "depart" else
                    [2, 3, 0] if mode == "resume" else
                    [scout["TransitPhase"]])

                if fault == "zero_resume" and mode == "resume":
                    report["steps"] = 0
                    report["after_days"] = report["before_days"]
                if fault == "unhashable_phase" and mode == "depart":
                    report["seen_phases"] = [1, [2]]
                if fault == "boolean_id" and mode == "depart":
                    report["target_id"] = True
                if fault == "changed_identity" and len(calls) == 1:
                    report["fleet_id"] = 8
                if fault == "bad_route" and mode == "depart":
                    scout["PlannedRouteSystemIds"] = [2]
                if fault == "bad_fuel" and mode == "depart":
                    scout["FuelRemainingLightYears"] = 10.0
                if fault == "science_changed" and mode == "resume":
                    science = next(row for row in after["Galaxy"]["Fleets"]
                                   if row.get("CivilizationId") == 0 and
                                   row.get("DesignId") == "science_vessel")
                    science["CurrentSystemId"] = 1

                save_text = json.dumps(after, separators=(",", ":"))
                if fault == "duplicate_save_key" and mode == "depart":
                    save_text = save_text.replace(
                        '"SavedAtUtc":', '"SavedAtUtc":"duplicate","SavedAtUtc":', 1)
                save.write_text(save_text, encoding="utf-8")
                capture = Path(args[-1])
                width = int(args[args.index("--width") + 1])
                height = int(args[args.index("--height") + 1])
                capture.write_bytes(bmp(width, height))
                if mode == "depart" and fault != "missing_sidecar":
                    capture.with_name(capture.stem + "-departure.bmp").write_bytes(
                        capture.read_bytes())
                if mode == "resume":
                    capture.with_name(capture.stem + "-arrival.bmp").write_bytes(
                        capture.read_bytes())
                report_text = json.dumps(report, separators=(",", ":"))
                if fault == "duplicate_proof_key" and mode == "depart":
                    report_text = report_text.replace(
                        '"seed":', '"seed":115501,"seed":', 1)
                stdout = (
                    "gpu_driver=vulkan systems=500 save=ok \n"
                    "first_exploration=" + report_text)
                calls.append(args)
                return subprocess.CompletedProcess(args, 0, stdout, "")

            with mock.patch.object(runtime.subprocess, "run", side_effect=launch):
                if fault in {
                        "extra_owned", "bad_fresh_contract", "serialized_wrong_lane",
                        "zero_resume", "unhashable_phase", "boolean_id",
                        "changed_identity", "bad_route", "bad_fuel",
                        "science_changed", "duplicate_save_key",
                        "duplicate_proof_key", "missing_sidecar"}:
                    with self.assertRaises(RuntimeError):
                        runtime.validate_native_first_exploration_export(
                            package, {}, source)
                    return None, calls
                result = runtime.validate_native_first_exploration_export(
                    package, {}, source)
            return result, calls

    def test_complete_serial_journey_uses_real_fresh_contract(self):
        result, calls = self.exercise()
        self.assertEqual(len(calls), 4)
        self.assertTrue(result["nativeFirstExploration"])
        self.assertTrue(result["nativeFirstExplorationReload"])
        self.assertEqual(len(result["firstExplorationCaptures"]), 6)
        self.assertEqual(len(result["firstExplorationSaveCaptures"]), 4)
        self.assertEqual(len(set(result["firstExplorationSaveCaptures"])), 4)
        capture_names = [Path(path).name
                         for path in result["firstExplorationCaptures"]]
        self.assertTrue(any(name.endswith("-departure.bmp")
                            for name in capture_names))
        self.assertTrue(any(name.endswith("-arrival.bmp")
                            for name in capture_names))

    def test_foreign_scout_is_not_selected_as_source(self):
        result, _ = self.exercise("foreign_scout")
        self.assertTrue(result["nativeFirstExploration"])

    def test_serialized_direct_lane_is_accepted(self):
        result, _ = self.exercise("serialized_direct_lane")
        self.assertTrue(result["nativeFirstExploration"])

    def test_rejects_extra_owned_fleet(self): self.exercise("extra_owned")
    def test_rejects_invalid_fresh_source(self): self.exercise("bad_fresh_contract")
    def test_rejects_wrong_serialized_lane(self): self.exercise("serialized_wrong_lane")
    def test_rejects_zero_day_resume(self): self.exercise("zero_resume")
    def test_rejects_unhashable_phase_as_validation_error(self):
        self.exercise("unhashable_phase")
    def test_rejects_boolean_identity(self): self.exercise("boolean_id")
    def test_rejects_route_identity_change(self): self.exercise("changed_identity")
    def test_rejects_wrong_persisted_route(self): self.exercise("bad_route")
    def test_rejects_forged_fuel_evidence(self): self.exercise("bad_fuel")
    def test_rejects_science_mutation(self): self.exercise("science_changed")
    def test_rejects_duplicate_save_key(self): self.exercise("duplicate_save_key")
    def test_rejects_duplicate_proof_key(self): self.exercise("duplicate_proof_key")
    def test_rejects_missing_departure_sidecar(self): self.exercise("missing_sidecar")


if __name__ == "__main__":
    unittest.main()
