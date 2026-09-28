import json
import unittest

from native_supply_runtime import _CHECKS, parse_supply_check


class SupplyEvidenceTests(unittest.TestCase):
    def test_complete(self):
        state = dict.fromkeys(_CHECKS, True)
        self.assertEqual(state, parse_supply_check("supply_inspection=" + json.dumps(state)))

    def test_missing_extra_false_and_non_boolean(self):
        for bad in (False, 1, "true", None):
            state = dict.fromkeys(_CHECKS, True)
            state["campaign_unchanged"] = bad
            with self.assertRaises(RuntimeError):
                parse_supply_check("supply_inspection=" + json.dumps(state))
        for delta in ("remove", "extra"):
            state = dict.fromkeys(_CHECKS, True)
            if delta == "remove":
                state.pop("canonical_totals")
            else:
                state["unexpected"] = True
            with self.assertRaises(RuntimeError):
                parse_supply_check("supply_inspection=" + json.dumps(state))

    def test_reject_ambiguous_or_malformed_proof(self):
        valid = "supply_inspection=" + json.dumps(dict.fromkeys(_CHECKS, True))
        for bad in ("", valid + "\n" + valid, "supply_inspection=[]",
                    "supply_inspection={", valid[:-1] + ',"navigation":true}'):
            with self.assertRaises(RuntimeError):
                parse_supply_check(bad)


if __name__ == "__main__":
    unittest.main()
