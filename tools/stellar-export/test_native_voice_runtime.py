import json
import unittest

from native_voice_runtime import parse_native_voice_check


def line(**changes):
    state = {"available": True, "played": 1, "unknown_denied": True,
             "overlap_prevented": True, "queue_bounded": True, "stopped": True}
    state.update(changes)
    return "voice_check=" + json.dumps(state) + "\nsystem_travel={}"


class NativeVoiceRuntimeTests(unittest.TestCase):
    def test_expected_evidence_is_accepted(self):
        self.assertEqual(parse_native_voice_check(line())["played"], 1)

    def test_missing_duplicate_and_malformed_diagnostics_are_rejected(self):
        duplicate_key = ('voice_check={"available":true,"available":true,"played":1,'
                         '"unknown_denied":true,"overlap_prevented":true,'
                         '"queue_bounded":true,"stopped":true}\n')
        for output in ("system_travel={}", line() + "\n" + line(), "voice_check={bad json}\n",
                       duplicate_key):
            with self.subTest(output=output):
                with self.assertRaises(RuntimeError):
                    parse_native_voice_check(output)

    def test_extra_fields_and_invalid_types_are_rejected(self):
        for changes in ({"played": 0}, {"played": True}, {"played": 1.0},
                        {"available": False}, {"unknown_denied": 1},
                        {"overlap_prevented": False}, {"queue_bounded": False},
                        {"stopped": False}, {"extra": 1}):
            with self.subTest(changes=changes):
                with self.assertRaises(RuntimeError):
                    parse_native_voice_check(line(**changes))


if __name__ == "__main__":
    unittest.main()
