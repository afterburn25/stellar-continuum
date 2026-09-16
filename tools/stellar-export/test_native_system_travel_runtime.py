import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from native_system_travel_runtime import validate_native_system_travel_export


class NativeSystemTravelExportTests(unittest.TestCase):
    def exercise(self, fault=None):
        source = {"FormatVersion": 17, "SavedAtUtc": "source", "SimulationDays": 0,
                  "Galaxy": {"PlayerCivilizationId": 0, "Systems": [0, 1, 2],
                             "Knowledge": [{"CivilizationId": 0, "KnownSystemIds": [0],
                                            "SystemSurveys": [{"SystemId": 0, "Level": 3, "Progress": 1}]}],
                             "Fleets": [{"Id": 7, "CivilizationId": 0, "IsActive": True,
                                         "CurrentSystemId": 0, "DestinationSystemId": None,
                                         "MissionOrderRevision": 0, "TransitPhase": 0,
                                         "PlannedRouteSystemIds": [], "LocalTransitPositionX": 0,
                                         "LocalTransitPositionY": 0}], "Economies": [{"Credits": 200}]}}
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary)/"package"
            package.mkdir()
            calls = []

            def run(args, *, cwd, env, **unused):
                self.assertNotEqual(cwd, package)
                self.assertIn("--load", args)
                save = Path(args[args.index("--save-path")+1])
                payload = json.loads(save.read_text())
                fleet = payload["Galaxy"]["Fleets"][0]
                capture = Path(args[-1])
                calls.append(args)
                marker = ""
                if "--fleet-smoke" in args:
                    payload["SimulationDays"] = 1
                    fleet.update(DestinationSystemId=2, MissionOrderRevision=1, TransitPhase=1,
                                 PlannedRouteSystemIds=[1, 2], LocalTransitPositionX=.1)
                    marker = (" fleet=7:2:1:0.1:hover=1:inspect=1:civilian=1"
                              ":locate=1:overview=1:missions=1:2:sites=1:1")
                    if fault == "preparation": fleet["TransitPhase"] = 2
                elif "--system-travel-smoke" in args or "--system-travel-reload-smoke" in args:
                    observer=payload["Galaxy"]["Knowledge"][0]
                    self.assertEqual(observer["KnownSystemIds"], [0, 1])
                    self.assertNotIn(2, observer["KnownSystemIds"])
                    state = {"fleet_id": 7, "system_id": 0, "destination_id": 2, "order_revision": 1,
                             "before_x": .1, "before_y": 0, "after_x": .2, "after_y": 0,
                             "before_days": 1, "after_days": 2,
                             **{key: True for key in ("selected", "canonical_moved", "rendered_moved",
                                                       "paused_stable", "pause_retained", "known_arrow",
                                                       "unknown_denied", "knowledge_unchanged", "lanes_connected")}}
                    payload["SimulationDays"] = 2
                    fleet["LocalTransitPositionX"] = .2
                    if "--system-travel-reload-smoke" in args:
                        state.update(before_x=.2, before_days=2, canonical_moved=False, rendered_moved=False)
                        if fault == "reload": payload["Galaxy"]["Economies"][0]["Credits"] += 1
                    if fault in state: state[fault] = False
                    if fault == "nonfinite": state["after_x"] = float("nan")
                    if fault == "frozen": state["after_x"] = .1
                    if fault == "mismatch": fleet["LocalTransitPositionX"] = .3
                    if fault == "order": fleet["MissionOrderRevision"] = 2
                    if fault == "foreign": fleet["CivilizationId"] = 1
                    if fault == "clock": payload["SimulationDays"] = 3
                    marker = " system_travel=" + json.dumps(state)
                else:
                    if fault == "reload": payload["Galaxy"]["Economies"][0]["Credits"] += 1
                payload["SavedAtUtc"] = "later-" + str(len(calls))
                save.write_text(json.dumps(payload))
                if fault != "capture": capture.write_bytes(b"BM"+bytes(54))
                stdout = "gpu_driver=vulkan systems=3 save=ok screenshot=test.bmp"+marker
                return subprocess.CompletedProcess(args, 0, stdout, "")

            with mock.patch("native_system_travel_runtime._source_row", return_value=copy.deepcopy(source)), \
                 mock.patch("native_system_travel_runtime.subprocess.run", side_effect=run):
                result = validate_native_system_travel_export(package, {}, Path("source"))
            self.assertEqual(len(calls), 3)
            self.assertTrue(result["nativeSystemTravelInput"])
            self.assertTrue(result["nativeSystemLocalTransit"])
            self.assertTrue(result["nativeSystemTravelPausedReload"])

    def test_real_input_motion_and_full_paused_reload(self): self.exercise()
    def test_route_must_be_in_local_departure(self):
        with self.assertRaises(RuntimeError): self.exercise("preparation")
    def test_known_arrow_navigation_is_required(self):
        with self.assertRaises(RuntimeError): self.exercise("known_arrow")
    def test_unknown_arrow_must_be_denied(self):
        with self.assertRaises(RuntimeError): self.exercise("unknown_denied")
    def test_false_lane_membership_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("lanes_connected")
    def test_unproven_pause_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("paused_stable")
    def test_nonfinite_position_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("nonfinite")
    def test_frozen_position_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("frozen")
    def test_diagnostic_must_match_saved_position(self):
        with self.assertRaises(RuntimeError): self.exercise("mismatch")
    def test_navigation_cannot_issue_another_order(self):
        with self.assertRaises(RuntimeError): self.exercise("order")
    def test_foreign_fleet_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("foreign")
    def test_diagnostic_must_match_saved_clock(self):
        with self.assertRaises(RuntimeError): self.exercise("clock")
    def test_reload_cannot_mutate_the_world(self):
        with self.assertRaises(RuntimeError): self.exercise("reload")
    def test_missing_capture_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("capture")


if __name__ == "__main__": unittest.main()
