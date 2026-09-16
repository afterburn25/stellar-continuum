import json
import unittest

from native_inspection_runtime import _CHECKS, parse_inspection_check


class InspectionEvidenceTests(unittest.TestCase):
    def test_complete(self):
        state = dict.fromkeys(_CHECKS, True)
        self.assertEqual(state, parse_inspection_check("system_inspection=" + json.dumps(state)))

    def test_missing_extra_false_and_non_boolean(self):
        for bad in (False, 1, "true", None):
            state = dict.fromkeys(_CHECKS, True)
            state["campaign_unchanged"] = bad
            with self.assertRaises(RuntimeError):
                parse_inspection_check("system_inspection=" + json.dumps(state))
        for delta in ("remove", "extra"):
            state = dict.fromkeys(_CHECKS, True)
            if delta == "remove":
                state.pop("unknown_redacted")
            else:
                state["unexpected"] = True
            with self.assertRaises(RuntimeError):
                parse_inspection_check("system_inspection=" + json.dumps(state))

    def test_reject_ambiguous_or_malformed_proof(self):
        valid = "system_inspection=" + json.dumps(dict.fromkeys(_CHECKS, True))
        for bad in ("", valid + "\n" + valid, "system_inspection=[]",
                    "system_inspection={", valid[:-1] + ',"known_clicked":true}'):
            with self.assertRaises(RuntimeError):
                parse_inspection_check(bad)


if __name__ == "__main__":
    unittest.main()
