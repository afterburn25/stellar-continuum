import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from native_battle_runtime import (
    _author_battle_source, validate_native_battle_export)


def fixture_payload():
    return {
        "FormatVersion": 17,
        "SimulationDays": 42.25,
        "Galaxy": {
            "Systems": list(range(20)),
            "PlayerCivilizationId": 0,
            "Civilizations": [
                {"Id": index, "Name": f"Civ {index}", "SpeciesId": "x",
                 "HomeSystemId": index} for index in range(4)],
            "Fleets": [
                {"Id": index, "Name": f"Fleet {index}", "CurrentSystemId": 0,
                 "Combat": {}} for index in range(8)],
        },
        "Diplomacy": {"Contacts": [], "Relationships": []},
    }


FIXTURE = {"Rows": [{"Name": "valid-current17",
                     "InputJson": json.dumps(fixture_payload(),
                                             separators=(",", ":"))}]}


def diagnostic():
    return {"formations": 2, "own": 1, "foreign": 1, "redacted": 1,
            "vessels_hidden": 0, "own_inexact": 0, "selected": 1,
            "tokens": 4, "events": 9, "salvos": 0, "tick": 20,
            "order_accepted": 1}


def payload():
    record = fixture_payload()
    record["Galaxy"]["ActiveCombatEncounter"] = {
        "SystemId": 0, "StartedDay": 42.25, "Reconciled": False,
        "Battle": {"BattleId": "x", "Formations": [{}, {}, {}],
                   "Events": [], "ActiveSalvos": []},
        "Vessels": [{"FleetId": 0, "FormationId": 1}],
        "EngagedFormationPairs": [{"FirstFormationId": 1,
                                   "SecondFormationId": 2}]}
    return record


class NativeBattleRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory(prefix="stellar-battle-test-") as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir()
            fixture = root / "fixture.json"
            fixture.write_text(json.dumps(FIXTURE), encoding="utf-8")
            calls = []

            def launch(args, *, cwd, **unused):
                shot = Path(args[args.index("--battle-smoke") + 1])
                save = Path(args[args.index("--save-path") + 1])
                self.assertEqual(cwd, shot.parent)
                self.assertEqual(cwd, save.parent)
                calls.append(args)
                state = diagnostic()
                if fault == "redacted": state["redacted"] = 0
                if fault == "own_inexact": state["own_inexact"] = 1
                if fault == "selected": state["selected"] = 0
                if fault == "order": state["order_accepted"] = 0
                if fault == "tick": state["tick"] = 0
                if fault == "tokens": state["tokens"] = 0
                record = payload()
                if fault == "save": record["Galaxy"]["Systems"] = [1]
                if fault == "encounter":
                    del record["Galaxy"]["ActiveCombatEncounter"]
                if fault == "reconciled":
                    record["Galaxy"]["ActiveCombatEncounter"]["Reconciled"] = True
                if fault == "picket":
                    record["Galaxy"]["ActiveCombatEncounter"]["Battle"]["Formations"] = [{}, {}]
                if fault == "bindings":
                    record["Galaxy"]["ActiveCombatEncounter"]["Vessels"] = []
                save.write_text(json.dumps(record), encoding="utf-8")
                varied = bytes(range(256)) if fault != "blank" else bytes(200)
                if fault == "capture":
                    pass
                else:
                    shot.write_bytes(b"BM" + b"\0" * 52 + varied * 40)
                return subprocess.CompletedProcess(
                    args, 1 if fault == "exit" else 0,
                    f"gpu_driver=vulkan systems=20 image_uploads=5 save=ok "
                    f"battle={json.dumps(state, separators=(',', ':'))}", "")

            with mock.patch("native_battle_runtime.subprocess.run",
                            side_effect=launch):
                result = validate_native_battle_export(package, {}, fixture)
            self.assertEqual(len(calls), 2)
            self.assertIn("--load", calls[0])
            self.assertIn("--load", calls[1])
            self.assertEqual(len(result["battleCaptures"]), 2)
            self.assertTrue(result["nativeBattleWorkspace"])
            self.assertTrue(result["nativeBattleObserverRedaction"])
            self.assertTrue(result["nativeBattleOrder"])
            self.assertTrue(result["nativeBattleReload"])

    def test_complete_actual_contract(self): self.exercise()
    def test_exact_foreign_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("redacted")
    def test_inexact_own_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("own_inexact")
    def test_dropped_selection_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("selected")
    def test_rejected_order_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("order")
    def test_frozen_tactical_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("tick")
    def test_missing_tokens_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("tokens")
    def test_damaged_payload_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("save")
    def test_missing_encounter_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("encounter")
    def test_reconciled_encounter_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("reconciled")
    def test_dropped_picket_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("picket")
    def test_dropped_bindings_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("bindings")
    def test_missing_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("capture")
    def test_blank_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("blank")
    def test_failed_launch_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("exit")


class NativeBattleAuthoringTests(unittest.TestCase):
    def test_authors_engagement_and_picket(self):
        authored = _author_battle_source(fixture_payload())
        galaxy = authored["Galaxy"]
        diplomacy = authored["Diplomacy"]
        self.assertEqual(len(galaxy["Fleets"]), 9)
        self.assertEqual(galaxy["Fleets"][8]["Id"], 8)
        encounter = galaxy["ActiveCombatEncounter"]
        self.assertFalse(encounter["Reconciled"])
        self.assertEqual(len(encounter["Battle"]["Formations"]), 3)
        self.assertEqual(len(encounter["Vessels"]), 5)
        self.assertEqual(len(encounter["EngagedFormationPairs"]), 1)
        owners = {formation["CivilizationId"]
                  for formation in encounter["Battle"]["Formations"]}
        self.assertEqual(owners, {0, 3})
        self.assertEqual(len(diplomacy["Contacts"]), 1)
        self.assertEqual(len(diplomacy["Relationships"]), 1)

    def test_fixture_shape_rejected(self):
        broken = fixture_payload()
        broken["Galaxy"]["Fleets"] = broken["Galaxy"]["Fleets"][:4]
        with self.assertRaises(RuntimeError):
            _author_battle_source(broken)

    def test_missing_hostile_civ_rejected(self):
        broken = fixture_payload()
        broken["Galaxy"]["Civilizations"] = \
            broken["Galaxy"]["Civilizations"][:3]
        with self.assertRaises(RuntimeError):
            _author_battle_source(broken)


if __name__ == "__main__":
    unittest.main()
