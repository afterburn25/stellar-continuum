import copy
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

from native_settlement_runtime import (author_settlement_fixture,
                                       validate_native_settlement_export)


def base_payload():
    systems = [{"Id": i, "Name": f"System {i}", "X": float(i), "Y": 0.0}
               for i in range(500)]
    bodies = [{"Id": i, "SystemId": i, "Name": f"Body {i}"}
              for i in range(500)]
    return {"FormatVersion": 17, "GameVersion": "test", "SavedAtUtc": "base",
            "SimulationDays": 0.0, "SimulationSeconds": 0.0,
            "Galaxy": {"PlayerCivilizationId": 0, "Systems": systems,
                       "PlanetaryBodies": bodies,
                       "Civilizations": [{"Id": 0, "Name": "Player",
                                             "HomeSystemId": 0, "SpeciesId": "terran_baseline",
                                             "IsPlayer": True}],
                       "Fleets": [],
                       "Colonies": [{"Id": 0, "CivilizationId": 0,
                                       "SystemId": 0, "PlanetaryBodyId": 0}],
                       "Economies": [{"CivilizationId": 0, "Credits": 500.0}],
                       "Technologies": [{"CivilizationId": 0, "NodeId": "unchanged"}],
                       "Knowledge": [{"CivilizationId": 0, "KnownSystemIds": [0],
                                      "SystemSurveys": [{"SystemId": 0, "Level": 3,
                                                         "Progress": 1.0}]}]}}


def bmp(width, height):
    stride = (width * 3 + 3) & ~3
    length = stride * height
    pixels = (bytes(range(251)) * (length // 251 + 1))[:length]
    size = 54 + length
    return (b"BM" + struct.pack("<IHHI", size, 0, 0, 54) +
            struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0,
                        length, 2835, 2835, 0, 0) + pixels)


class NativeSettlementRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir()
            calls = []

            def run(args, *, cwd, env, **unused):
                self.assertNotEqual(Path(cwd), package)
                self.assertNotIn(str(package), env.get("PATH", ""))
                if fault == "failed":
                    return subprocess.CompletedProcess(args, 9, "caught-output", "terminal-error")
                save = Path(args[args.index("--save-path") + 1])
                if "--smoke" in args:
                    current = base_payload()
                    if fault == "fresh_format": current["FormatVersion"] = 16
                    save.write_text(json.dumps(current), encoding="utf-8")
                    capture = Path(args[args.index("--smoke") + 1])
                    if fault != "capture":
                        width = int(args[args.index("--width") + 1])
                        height = int(args[args.index("--height") + 1])
                        image = bmp(width - 1 if fault == "geometry" else width,
                                    height)
                        if fault == "truncated":
                            image = bytearray(image[:-64])
                            struct.pack_into("<I", image, 2, len(image))
                            image = bytes(image)
                        capture.write_bytes(image)
                    renderer = "software" if fault == "renderer" else "vulkan"
                    uploads = 0 if fault == "uploads" else 9
                    stdout = (f"gpu_driver={renderer} systems=500 image_uploads={uploads} "
                              "save=ok base=generated")
                    calls.append(args)
                    return subprocess.CompletedProcess(args, 0, stdout, "")
                current = json.loads(save.read_text())
                fleet = next(x for x in current["Galaxy"]["Fleets"]
                             if x["CivilizationId"] == 0 and x["Role"] == 2)
                kind = "outpost" if fleet["DesignId"] == "resource_outpost_ship" else "colony"
                reload = "--settlement-reload-smoke" in args
                label = "paused_reload" if reload else "ordered"
                cost = 90.0 if kind == "outpost" else 120.0
                if not reload:
                    current["SimulationDays"] = 0.5
                    current["SavedAtUtc"] = f"ordered-{kind}"
                    current["Galaxy"]["Economies"][0]["Credits"] -= cost
                    fleet["DestinationSystemId"] = 1
                    fleet["DestinationPlanetaryBodyId"] = 1
                    fleet["MissionOrderRevision"] = 1
                    fleet["SettlementDaysCompleted"] = 0.25
                    fleet["SettlementBodyId"] = 1
                    if fault == "arrived":
                        fleet["DestinationSystemId"] = None
                        fleet["CurrentSystemId"] = 1
                    if fault == "owner": fleet["CivilizationId"] = 1
                    if fault == "revision_unchanged": fleet["MissionOrderRevision"] = 0
                    if fault == "passengers": fleet["EmbarkedPopulationMillions"] += 1
                    if fault == "systems_after": current["Galaxy"]["Systems"].pop()
                    if fault == "knowledge": current["Galaxy"]["Knowledge"][0]["KnownSystemIds"].remove(1)
                    if fault == "survey":
                        next(x for x in current["Galaxy"]["Knowledge"][0]["SystemSurveys"]
                             if x["SystemId"] == 1)["Level"] = 2
                    if fault == "instant":
                        current["Galaxy"]["Colonies"].append(
                            {"Id": 2, "CivilizationId": 0, "SystemId": 1,
                             "PlanetaryBodyId": 1})
                else:
                    current["SavedAtUtc"] = f"reload-{kind}"
                    if fault == "payload": current["Galaxy"]["ReloadMutation"] = True
                save.write_text(json.dumps(current), encoding="utf-8")
                state = {"mode": label, "kind": kind, "fleet_id": fleet["Id"],
                         "system_id": 1, "body_id": 1,
                         "mission_revision": fleet["MissionOrderRevision"],
                         "before_days": 0.0 if not reload else 0.5,
                         "saved_days": 0.5,
                         "settlement_days": fleet["SettlementDaysCompleted"],
                         "authorization": cost if not reload else 0.0,
                         "treasury_before": 500.0,
                         "treasury_after": 500.0 - cost,
                         "requires_authorization": not reload,
                         "selected": True, "previewed": not reload,
                         "cancelled": not reload, "cancel_no_charge": not reload,
                         "accepted": not reload, "no_instant_colony": True,
                         "paused": True}
                if fault in state: state[fault] = False if isinstance(state[fault], bool) else -1
                if fault == "wrong_kind": state["kind"] = "outpost" if kind == "colony" else "colony"
                if fault == "charge" and not reload: state["authorization"] += 1
                if fault == "treasury" and not reload: state["treasury_after"] += 1
                if fault == "clock" and not reload: state["saved_days"] = state["before_days"]
                if fault == "complete":
                    state["settlement_days"] = 30.0 if kind == "colony" else 20.0
                    fleet["SettlementDaysCompleted"] = state["settlement_days"]
                    save.write_text(json.dumps(current), encoding="utf-8")
                if fault == "destination": state["body_id"] = 2
                if fault == "diagnostic_extra": state["secret_name"] = "Hidden"
                mode = "--settlement-reload-smoke" if reload else "--settlement-smoke"
                capture = Path(args[args.index(mode) + 1])
                if fault != "capture":
                    width = int(args[args.index("--width") + 1])
                    height = int(args[args.index("--height") + 1])
                    image = bmp(width - 1 if fault == "geometry" else width, height)
                    if fault == "truncated":
                        image = bytearray(image[:-64])
                        struct.pack_into("<I", image, 2, len(image))
                        image = bytes(image)
                    capture.write_bytes(image)
                renderer = "software" if fault == "renderer" else "vulkan"
                uploads = 0 if fault == "uploads" else 9
                stdout = (f"gpu_driver={renderer} systems=500 image_uploads={uploads} save=ok "
                          f"settlement={json.dumps(state, separators=(',', ':'))}")
                calls.append(args)
                return subprocess.CompletedProcess(args, 0, stdout, "")

            with mock.patch("native_settlement_runtime.subprocess.run", side_effect=run):
                result = validate_native_settlement_export(package, {})
            self.assertEqual(len(calls), 5)
            self.assertNotIn("--load", calls[0])
            self.assertTrue(all("--load" in call for call in calls[1:]))
            self.assertEqual(result["settlementKinds"], ["colony", "outpost"])
            self.assertEqual(len(result["settlementCaptures"]), 5)
            self.assertTrue(result["nativeSettlementFreshBase"])
            self.assertTrue(result["nativeSettlementPlayerInput"])
            self.assertTrue(result["nativeSettlementPausedReload"])

    def test_two_kinds_order_and_reload(self): self.exercise()
    def test_arrived_fleet_system_fallback(self): self.exercise("arrived")
    def test_selection_required(self):
        with self.assertRaises(RuntimeError): self.exercise("selected")
    def test_preview_required(self):
        with self.assertRaises(RuntimeError): self.exercise("previewed")
    def test_cancel_required(self):
        with self.assertRaises(RuntimeError): self.exercise("cancelled")
    def test_cancel_no_charge_required(self):
        with self.assertRaises(RuntimeError): self.exercise("cancel_no_charge")
    def test_accept_required(self):
        with self.assertRaises(RuntimeError): self.exercise("accepted")
    def test_pause_required(self):
        with self.assertRaises(RuntimeError): self.exercise("paused")
    def test_wrong_kind_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("wrong_kind")
    def test_wrong_quote_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("charge")
    def test_wrong_charge_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("treasury")
    def test_no_time_progress_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("clock")
    def test_completed_mission_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("complete")
    def test_foreign_vessel_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("owner")
    def test_unchanged_mission_revision_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("revision_unchanged")
    def test_changed_passengers_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("passengers")
    def test_changed_system_count_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("systems_after")
    def test_unknown_target_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("knowledge")
    def test_unsurveyed_target_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("survey")
    def test_destination_mismatch_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("destination")
    def test_instant_colony_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("instant")
    def test_reload_mutation_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("payload")
    def test_renderer_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("renderer")
    def test_image_upload_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("uploads")
    def test_missing_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("capture")
    def test_capture_geometry_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("geometry")
    def test_truncated_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("truncated")
    def test_failed_launch_reports_both_streams(self):
        with self.assertRaisesRegex(RuntimeError, "(?s)caught-output.*terminal-error"):
            self.exercise("failed")
    def test_invalid_fresh_base_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("fresh_format")

    def test_fixture_authoring_preserves_technology_and_uses_two_designs(self):
        base = base_payload()
        colony = author_settlement_fixture(base, "colony")
        outpost = author_settlement_fixture(base, "outpost")
        self.assertEqual(colony["Galaxy"]["Technologies"], base["Galaxy"]["Technologies"])
        self.assertEqual(outpost["Galaxy"]["Technologies"], base["Galaxy"]["Technologies"])
        self.assertEqual(colony["Galaxy"]["Fleets"][0]["DesignId"], "colony_ship")
        self.assertEqual(outpost["Galaxy"]["Fleets"][0]["DesignId"], "resource_outpost_ship")
        self.assertEqual(len(colony["Galaxy"]["Knowledge"][0]["KnownSystemIds"]), 500)

    def test_fixture_rejects_insufficient_funds(self):
        value = base_payload()
        value["Galaxy"]["Economies"][0]["Credits"] = 80
        with self.assertRaises(RuntimeError): author_settlement_fixture(value, "outpost")

    def test_fixture_rejects_existing_eligible_vessel(self):
        value = author_settlement_fixture(base_payload(), "colony")
        with self.assertRaises(RuntimeError): author_settlement_fixture(value, "outpost")


if __name__ == "__main__":
    unittest.main()
