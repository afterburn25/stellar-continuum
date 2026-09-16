import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from native_fleet_runtime import validate_native_fleet_export


class NativeFleetExportTests(unittest.TestCase):
    def exercise(self, *, wrong_owner=False, unchanged=False, zero_advance=False,
                 mutate_load=False, skipped_save=False, already_routed=False,
                 frozen_time=False, switched_player=False, no_hover=False):
        with tempfile.TemporaryDirectory(prefix="stellar-fleet-export-test-") as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir()
            source = {"FormatVersion": 17, "SavedAtUtc": "source",
                      "SimulationDays": 42.0,
                      "Galaxy": {"PlayerCivilizationId": 7,
                                 "Systems": [{"Id": index} for index in range(20)],
                                 "Economies": [{"CivilizationId": 7,
                                                "Credits": 100}],
                                 "Fleets": [{"Id": 4, "CivilizationId": 7,
                                             "IsActive": True,
                                             "DestinationSystemId": 9 if already_routed else None,
                                             "PlannedRouteSystemIds": [1, 9] if already_routed else [],
                                             "MissionOrderRevision": 2,
                                             "TransitProgress": 0.0}]}}
            fixture = root / "player17-fixture.json"
            fixture.write_text(json.dumps({"Rows": [{"Name": "valid-current17",
                                                       "InputJson": json.dumps(source)}]}))
            calls = []

            def launch(args, *, cwd, env, **unused):
                self.assertNotEqual(cwd, package)
                self.assertEqual(env["PATH"].lower().count("windows") >= 1, True)
                save = Path(args[args.index("--save-path") + 1])
                capture = Path(args[args.index("--fleet-smoke") + 1])
                self.assertEqual(save.parent, cwd)
                self.assertEqual(capture.parent, cwd)
                self.assertIn("--load", args)
                payload = json.loads(save.read_text())
                replay = len(calls) == 1
                fleet = payload["Galaxy"]["Fleets"][0]
                if not replay:
                    payload["SimulationDays"] = 42.0 if frozen_time else 42.25
                    if switched_player:
                        payload["Galaxy"]["PlayerCivilizationId"] = 8
                    fleet["CivilizationId"] = 8 if wrong_owner else 7
                    if not unchanged:
                        fleet["DestinationSystemId"] = 13
                        fleet["PlannedRouteSystemIds"] = [1, 5, 13]
                        fleet["MissionOrderRevision"] = 3
                        fleet["TransitProgress"] = 0 if zero_advance else .25
                    payload["SavedAtUtc"] = "ordered"
                else:
                    payload["SavedAtUtc"] = "loaded"
                    if mutate_load:
                        payload["Galaxy"]["Economies"][0]["Credits"] += 1
                save.write_text(json.dumps(payload))
                capture.write_bytes(b"BM" + bytes(54))
                calls.append(args)
                marker = "preserved" if skipped_save and replay else "ok"
                hover = (":hover=1:inspect=1:civilian=1:overview=1:missions=1:2:sites=1:1"
                         if not no_hover else "")
                return subprocess.CompletedProcess(
                    args, 0,
                    "gpu_driver=vulkan systems=20 save=" + marker +
                    " fleet=4:13:3:0.250000" + hover, "")

            with mock.patch("native_fleet_runtime.subprocess.run", side_effect=launch):
                result = validate_native_fleet_export(package, {}, fixture)
            self.assertEqual(len(calls), 2)
            self.assertTrue(result["nativeFleetPlayerInput"])
            self.assertTrue(result["nativeFleetTransitAdvanced"])
            self.assertTrue(result["nativeFleetProgressReload"])
            self.assertTrue(all(Path(path).is_file()
                                for path in result["fleetCaptures"]))

    def test_fleet_mouse_order_advances_and_survives_reload(self):
        self.exercise()

    def test_wrong_owner_cannot_count_as_player_order(self):
        with self.assertRaisesRegex(RuntimeError, "unique player-owned fleet"):
            self.exercise(wrong_owner=True)

    def test_unchanged_order_cannot_count_as_player_input(self):
        with self.assertRaisesRegex(RuntimeError, "diagnostic does not match|new canonical route"):
            self.exercise(unchanged=True)

    def test_zero_transit_advancement_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "positive real transit advancement"):
            self.exercise(zero_advance=True)

    def test_missing_hover_preview_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "hover preview"):
            self.exercise(no_hover=True)

    def test_loaded_treasury_mutation_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "changed during paused load"):
            self.exercise(mutate_load=True)

    def test_skipped_loaded_save_is_not_a_roundtrip(self):
        with self.assertRaisesRegex(RuntimeError, "actual manual save"):
            self.exercise(skipped_save=True)

    def test_existing_route_cannot_count_as_first_order(self):
        with self.assertRaisesRegex(RuntimeError, "already routed"):
            self.exercise(already_routed=True)

    def test_frozen_simulation_time_cannot_count_as_advancement(self):
        with self.assertRaisesRegex(RuntimeError, "advance finite simulation time"):
            self.exercise(frozen_time=True)

    def test_switched_player_identity_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "changed the player civilization"):
            self.exercise(switched_player=True)


if __name__ == "__main__":
    unittest.main()
