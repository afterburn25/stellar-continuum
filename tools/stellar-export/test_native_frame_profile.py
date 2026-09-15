import json
import unittest

from native_frame_profile import validate_steady_profile


def profile(samples=120):
    metrics = {"mean_ms": 2.0, "p50_ms": 1.0, "p95_ms": 3.0,
               "p99_ms": 4.0, "max_ms": 5.0}
    value = {"samples": samples}
    for phase in ("interval", "update", "scene", "submission", "throttle", "present"):
        value[phase] = dict(metrics)
    value["readback"] = {key: 0.0 for key in metrics}
    return value


class NativeFrameProfileTests(unittest.TestCase):
    def test_complete_token_allows_later_stdout(self):
        payload = profile()
        parsed = validate_steady_profile("before steady_profile=" + json.dumps(payload) + " save=ok\n", 120)
        self.assertEqual(parsed, payload)

    def test_missing_truncated_and_duplicate_tokens_are_rejected(self):
        for stdout in ("save=ok", "steady_profile={\"samples\":120", "steady_profile={} steady_profile={}"):
            with self.subTest(stdout=stdout), self.assertRaises(RuntimeError):
                validate_steady_profile(stdout, 120)

    def test_invalid_json_and_sample_contract_are_rejected(self):
        float_samples = profile(); float_samples["samples"] = 120.0
        for payload in ("{not-json}", json.dumps(profile(119)), json.dumps(profile(True)), json.dumps(float_samples)):
            with self.subTest(payload=payload), self.assertRaises(RuntimeError):
                validate_steady_profile("steady_profile=" + payload, 120)
        with self.assertRaises(RuntimeError):
            validate_steady_profile("steady_profile=" + json.dumps(profile()), 119)

    def test_duplicate_json_members_are_rejected(self):
        payload = json.dumps(profile()).replace('"samples": 120', '"samples":120,"samples":120')
        with self.assertRaises(RuntimeError):
            validate_steady_profile("steady_profile=" + payload, 120)

    def test_profile_frame_request_must_be_zero_or_bounded_integer(self):
        for value in (True, -1, 1, 119, 120.0, 3601):
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                validate_steady_profile("steady_profile=" + json.dumps(profile()), value)

    def test_metrics_must_be_complete_numeric_finite_and_ordered(self):
        cases = []
        missing = profile(); del missing["scene"]["p99_ms"]; cases.append(missing)
        boolean = profile(); boolean["update"]["mean_ms"] = True; cases.append(boolean)
        nonfinite = profile(); nonfinite["present"]["max_ms"] = float("inf"); cases.append(nonfinite)
        negative = profile(); negative["submission"]["p50_ms"] = -1.; cases.append(negative)
        unordered = profile(); unordered["interval"]["p95_ms"] = .5; cases.append(unordered)
        above_max = profile(); above_max["scene"]["mean_ms"] = 6.; cases.append(above_max)
        readback = profile(); readback["readback"]["max_ms"] = .1; cases.append(readback)
        for value in cases:
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                validate_steady_profile("steady_profile=" + json.dumps(value), 120)


if __name__ == "__main__":
    unittest.main()
