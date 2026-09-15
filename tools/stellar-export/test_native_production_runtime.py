import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from native_production_runtime import (
    validate_native_construction_export,
    validate_native_shipyard_export,
)


def source_campaign():
    core = {"Capabilities": []}
    return {
        "FormatVersion": 17,
        "SavedAtUtc": "source",
        "SimulationDays": 42.25,
        "Galaxy": {
            "PlayerCivilizationId": 7,
            "Systems": [{"Id": index} for index in range(20)],
            "Economies": [{"CivilizationId": 7, "Credits": 100,
                            "Industry": 100}],
            "ConstructionStates": [{
                "CivilizationId": 7,
                "CompletedProjectIds": [],
                "ActiveProjectId": None,
                "ActiveProjectProgress": 0,
                "ActiveProjectAuthorizationCredits": 0,
                "QueuedProjects": [],
            }],
            "ShipyardStates": [{
                "CivilizationId": 7,
                "NextOrderSequence": 1,
                "ActiveDesignId": None,
                "ActiveOrderId": None,
                "ActiveBuildProgress": 0,
                "ActiveAuthorizationCredits": 0,
                "ReservedPopulationMillions": 0,
                "ReservedPopulationSpeciesId": None,
                "ReservedPopulationSourceColonyId": None,
                "QueuedBuilds": [],
            }],
        },
        "AdaptiveResearch": {"Civilizations": [{
            "CivilizationId": 7,
            "Research": {"Research": {"Research": {"Research": {
                "Core": core,
            }}}},
        }]},
    }


def fixture(path: Path):
    source = source_campaign()
    path.write_text(json.dumps({"Rows": [{
        "Name": "valid-current17",
        "InputJson": json.dumps(source),
    }]}), encoding="utf-8")


class NativeProductionExportTests(unittest.TestCase):
    def exercise_shipyard(self, *, fresh_unlocked=False,
                          start_not_running=False, no_pause=False,
                          no_order=False, mutate_reload=False,
                          skipped_save=False, no_debit=False,
                          authorization=70, design_id="warp_scout",
                          nonfinite_treasury=False):
        with tempfile.TemporaryDirectory(prefix="stellar-shipyard-export-test-") as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir()
            source_fixture = root / "player17.json"
            fixture(source_fixture)
            authored_runs = 0

            def launch(args, *, cwd, env, **unused):
                nonlocal authored_runs
                self.assertNotEqual(cwd, package)
                self.assertIn("windows", env["PATH"].lower())
                save = Path(args[args.index("--save-path") + 1])
                capture = Path(args[args.index("--shipyard-smoke") + 1])
                capture.write_bytes(b"BM" + bytes(52))
                if "fresh-locked" in save.name:
                    payload = source_campaign()
                    payload["Galaxy"]["Systems"] = [
                        {"Id": index} for index in range(500)]
                    payload["SavedAtUtc"] = "fresh"
                    marker = ("orders:1:1:unexpected:start-not-run:clock-unproven"
                              if fresh_unlocked else
                              "locked:1:0:start-not-run:clock-unproven")
                else:
                    payload = json.loads(save.read_text(encoding="utf-8"))
                    state = payload["Galaxy"]["ShipyardStates"][0]
                    if authored_runs == 0:
                        payload["SimulationDays"] = 42.5
                        payload["SavedAtUtc"] = "started"
                        if not no_order:
                            state.update({"NextOrderSequence": 2,
                                          "ActiveDesignId": design_id,
                                          "ActiveOrderId": "shipyard-7-1",
                                          "ActiveBuildProgress": 0,
                                          "ActiveAuthorizationCredits": authorization})
                            if not no_debit:
                                payload["Galaxy"]["Economies"][0]["Credits"] = (
                                    float("nan") if nonfinite_treasury else 0)
                        marker = ("started:2:1:shipyard-7-1:" +
                                  ("start-not-run" if start_not_running else
                                   "start-running") + ":" +
                                  ("clock-unproven" if no_pause else
                                   "running-to-paused"))
                    else:
                        payload["SavedAtUtc"] = "loaded"
                        if mutate_reload:
                            payload["Galaxy"]["Economies"][0]["Industry"] += 1
                        marker = ("orders:2:1:shipyard-7-1:start-not-run:"
                                  "running-to-paused")
                    authored_runs += 1
                save.write_text(json.dumps(payload), encoding="utf-8")
                save_marker = ("preserved" if skipped_save and
                               authored_runs == 2 else "ok")
                systems = len(payload["Galaxy"]["Systems"])
                return subprocess.CompletedProcess(
                    args, 0, f"gpu_driver=vulkan systems={systems} "
                    f"save={save_marker} shipyard={marker}", "")

            with mock.patch("native_production_runtime.subprocess.run",
                            side_effect=launch):
                result = validate_native_shipyard_export(
                    package, {}, source_fixture)
            self.assertTrue(result["nativeShipyardPlayerInput"])
            self.assertTrue(result["nativeShipyardCancellationQuotePrepared"])
            self.assertTrue(result["nativeShipyardOrderReload"])
            self.assertTrue(result["nativeShipyardFreshLocked"])
            self.assertTrue(result["shipyardScenarioAuthoredForValidation"])
            self.assertEqual(len(result["shipyardCaptures"]), 3)

    def exercise_construction(self, *, start_not_running=False,
                              no_pause=False, no_order=False,
                              mutate_reload=False, skipped_save=False,
                              frozen_time=False, no_debit=False,
                              authorization=350, nonfinite_treasury=False,
                              unexpected_reload_input=False):
        with tempfile.TemporaryDirectory(prefix="stellar-construction-export-test-") as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir()
            source_fixture = root / "player17.json"
            fixture(source_fixture)
            runs = 0

            def launch(args, *, cwd, env, **unused):
                nonlocal runs
                self.assertNotEqual(cwd, package)
                self.assertIn("windows", env["PATH"].lower())
                save = Path(args[args.index("--save-path") + 1])
                capture = Path(args[args.index("--construction-smoke") + 1])
                payload = json.loads(save.read_text(encoding="utf-8"))
                capture.write_bytes(b"BM" + bytes(52))
                state = payload["Galaxy"]["ConstructionStates"][0]
                if runs == 0:
                    payload["SimulationDays"] = 42.25 if frozen_time else 42.5
                    payload["SavedAtUtc"] = "started"
                    if not no_order:
                        state.update({"ActiveProjectId": "orbital_shipyard",
                                      "ActiveProjectProgress": 0,
                                      "ActiveProjectAuthorizationCredits": authorization})
                        if not no_debit:
                            payload["Galaxy"]["Economies"][0]["Credits"] = (
                                float("nan") if nonfinite_treasury else 0)
                    marker = ("started:2:orbital_shipyard:" +
                              ("start-not-run" if start_not_running else
                               "start-running") + ":" +
                              ("quote-unproven" if no_pause else
                               "quote-paused"))
                else:
                    payload["SavedAtUtc"] = "loaded"
                    if mutate_reload:
                        payload["Galaxy"]["Economies"][0]["Industry"] += 1
                    marker = ("started:2:another:start-running:quote-paused"
                              if unexpected_reload_input else
                              "locked:2:none:start-not-run:quote-unproven")
                runs += 1
                save.write_text(json.dumps(payload), encoding="utf-8")
                save_marker = "preserved" if skipped_save and runs == 2 else "ok"
                return subprocess.CompletedProcess(
                    args, 0, "gpu_driver=vulkan systems=20 "
                    f"save={save_marker} construction={marker}", "")

            with mock.patch("native_production_runtime.subprocess.run",
                            side_effect=launch):
                result = validate_native_construction_export(
                    package, {}, source_fixture)
            self.assertTrue(result["nativeConstructionPlayerInput"])
            self.assertTrue(
                result["nativeConstructionCancellationQuotePrepared"])
            self.assertTrue(result["nativeConstructionOrderReload"])
            self.assertTrue(result["constructionScenarioAuthoredForValidation"])
            self.assertEqual(len(result["constructionCaptures"]), 2)

    def test_shipyard_ui_start_quote_lock_and_reload(self):
        self.exercise_shipyard()

    def test_fresh_shipyard_must_remain_locked(self):
        with self.assertRaisesRegex(RuntimeError, "retain its locked shipyard"):
            self.exercise_shipyard(fresh_unlocked=True)

    def test_shipyard_start_must_be_while_running(self):
        with self.assertRaisesRegex(RuntimeError, "start while running"):
            self.exercise_shipyard(start_not_running=True)

    def test_shipyard_must_prepare_pause_quote(self):
        with self.assertRaisesRegex(RuntimeError, "prepare cancellation"):
            self.exercise_shipyard(no_pause=True)

    def test_shipyard_diagnostic_without_saved_order_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "identify one saved order"):
            self.exercise_shipyard(no_order=True)

    def test_shipyard_order_without_authorization_debit_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "debit its canonical"):
            self.exercise_shipyard(no_debit=True)

    def test_shipyard_malformed_authorization_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "canonical build order"):
            self.exercise_shipyard(authorization="seventy")

    def test_shipyard_nonfinite_authorization_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "canonical build order"):
            self.exercise_shipyard(authorization=float("nan"))

    def test_shipyard_wrong_authorization_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "canonical build order"):
            self.exercise_shipyard(authorization=69)

    def test_shipyard_wrong_design_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "canonical build order"):
            self.exercise_shipyard(design_id="patrol_corvette")

    def test_shipyard_nonfinite_treasury_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "debit its canonical"):
            self.exercise_shipyard(nonfinite_treasury=True)

    def test_shipyard_reload_mutation_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "changed during paused reload"):
            self.exercise_shipyard(mutate_reload=True)

    def test_shipyard_skipped_save_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "actual manual save"):
            self.exercise_shipyard(skipped_save=True)

    def test_construction_ui_start_quote_and_reload(self):
        self.exercise_construction()

    def test_construction_start_must_be_while_running(self):
        with self.assertRaisesRegex(RuntimeError, "start while running"):
            self.exercise_construction(start_not_running=True)

    def test_construction_must_prepare_pause_quote(self):
        with self.assertRaisesRegex(RuntimeError, "prepare cancellation"):
            self.exercise_construction(no_pause=True)

    def test_construction_diagnostic_without_saved_order_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "identify one saved order"):
            self.exercise_construction(no_order=True)

    def test_construction_order_without_authorization_debit_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "debit its canonical"):
            self.exercise_construction(no_debit=True)

    def test_construction_malformed_authorization_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "canonical order"):
            self.exercise_construction(authorization="three hundred fifty")

    def test_construction_nonfinite_authorization_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "canonical order"):
            self.exercise_construction(authorization=float("inf"))

    def test_construction_wrong_authorization_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "canonical order"):
            self.exercise_construction(authorization=349)

    def test_construction_nonfinite_treasury_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "debit its canonical"):
            self.exercise_construction(nonfinite_treasury=True)

    def test_construction_reload_mutation_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "changed during paused reload"):
            self.exercise_construction(mutate_reload=True)

    def test_construction_skipped_save_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "actual manual save"):
            self.exercise_construction(skipped_save=True)

    def test_construction_requires_running_time_to_advance(self):
        with self.assertRaisesRegex(RuntimeError, "advance finite simulation time"):
            self.exercise_construction(frozen_time=True)

    def test_construction_reload_cannot_accept_new_input(self):
        with self.assertRaisesRegex(RuntimeError, "unexpected new input"):
            self.exercise_construction(unexpected_reload_input=True)


if __name__ == "__main__":
    unittest.main()
