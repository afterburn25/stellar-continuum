import copy
import json
from pathlib import Path
import unittest

from native_military_runtime import (_CHECKS, parse_military_check, military_source,
                                     check_military_payload)


class MilitaryEvidenceTests(unittest.TestCase):
    def test_complete(self):
        state = dict.fromkeys(_CHECKS, True)
        self.assertEqual(state, parse_military_check("military_inspection=" + json.dumps(state)))

    def test_missing_extra_false_and_non_boolean(self):
        for bad in (False, 1, "true", None):
            state = dict.fromkeys(_CHECKS, True)
            state["order_saved"] = bad
            with self.assertRaises(RuntimeError):
                parse_military_check("military_inspection=" + json.dumps(state))
        for delta in ("remove", "extra"):
            state = dict.fromkeys(_CHECKS, True)
            if delta == "remove":
                state.pop("only_order_changed")
            else:
                state["unexpected"] = True
            with self.assertRaises(RuntimeError):
                parse_military_check("military_inspection=" + json.dumps(state))

    def test_reject_ambiguous_or_malformed_proof(self):
        valid = "military_inspection=" + json.dumps(dict.fromkeys(_CHECKS, True))
        for bad in ("", valid + "\n" + valid, "military_inspection=[]",
                    "military_inspection={", valid[:-1] + ',"selection":true}'):
            with self.assertRaises(RuntimeError):
                parse_military_check(bad)

    def test_order_must_be_on_the_actual_owned_ship(self):
        root = Path(__file__).resolve().parents[2]
        source, fleet_id = military_source(root / "native-tests/fixtures/player-campaign-json.json")
        valid = copy.deepcopy(source)
        fleet = next(f for f in valid["Galaxy"]["Fleets"] if f["Id"] == fleet_id)
        fleet["Combat"].update(Order=1, DefendSystemId=fleet["CurrentSystemId"])
        check_military_payload(valid, source, fleet_id)
        for mutate in (lambda p, f: p.update(SimulationDays=p["SimulationDays"] + 1),
                       lambda p, f: f.update(CivilizationId=999),
                       lambda p, f: f.update(DestinationSystemId=9),
                       lambda p, f: f.update(MissionOrderRevision=99),
                       lambda p, f: f["Combat"].update(Order=True),
                       lambda p, f: f["Combat"].update(Order=0),
                       lambda p, f: f["Combat"].update(DefendSystemId=999),
                       lambda p, f: f["Combat"].update(TargetFleetId=999)):
            bad = copy.deepcopy(valid)
            target = next(f for f in bad["Galaxy"]["Fleets"] if f["Id"] == fleet_id)
            mutate(bad, target)
            with self.assertRaises(RuntimeError):
                check_military_payload(bad, source, fleet_id)


if __name__ == "__main__":
    unittest.main()
