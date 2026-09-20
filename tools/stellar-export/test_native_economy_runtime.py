import json
import unittest

from native_economy_runtime import _CHECKS, check_economy_payload, parse_economy_check


def payload(player_priority=2, foreign_priority=None):
    return {"FormatVersion": 17, "SimulationDays": 0, "Galaxy": {
        "PlayerCivilizationId": 7, "Systems": [{}] * 500,
        "Economies": [{"CivilizationId": 7, "IndustryPriority": player_priority},
                      {"CivilizationId": 9, "IndustryPriority": foreign_priority}]}}


class EconomyEvidenceTests(unittest.TestCase):
    def test_complete(self):
        state = dict.fromkeys(_CHECKS, True)
        self.assertEqual(state, parse_economy_check("economy_inspection=" + json.dumps(state)))

    def test_missing_extra_false_and_non_boolean(self):
        for bad in (False, 1, "true", None):
            state = dict.fromkeys(_CHECKS, True)
            state["priority_saved"] = bad
            with self.assertRaises(RuntimeError):
                parse_economy_check("economy_inspection=" + json.dumps(state))
        for delta in ("remove", "extra"):
            state = dict.fromkeys(_CHECKS, True)
            if delta == "remove":
                state.pop("canonical_totals")
            else:
                state["unexpected"] = True
            with self.assertRaises(RuntimeError):
                parse_economy_check("economy_inspection=" + json.dumps(state))

    def test_reject_ambiguous_or_malformed_proof(self):
        valid = "economy_inspection=" + json.dumps(dict.fromkeys(_CHECKS, True))
        for bad in ("", valid + "\n" + valid, "economy_inspection=[]",
                    "economy_inspection={", valid[:-1] + ',"navigation":true}'):
            with self.assertRaises(RuntimeError):
                parse_economy_check(bad)

    def test_player_priority_and_paused_payload(self):
        check_economy_payload(payload())
        check_economy_payload(payload(2, 2))
        paused = payload(); paused["SimulationDays"] = 1
        with self.assertRaises(RuntimeError):
            check_economy_payload(paused)
        with self.assertRaises(RuntimeError):
            check_economy_payload(payload(2.0))

    def test_foreign_priority_does_not_pass(self):
        with self.assertRaises(RuntimeError):
            check_economy_payload(payload(None, 2))


if __name__ == "__main__":
    unittest.main()
