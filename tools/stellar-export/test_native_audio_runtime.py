import json
import unittest

from native_audio_runtime import parse_native_audio_check


def line(**changes):
    state = {"assets_loaded": True, "music_starts": 1, "confirm_count": 2,
             "queued_music_bytes": 2048, "boot_services": 3, "stopped": True}
    state.update(changes)
    return "audio_check=" + json.dumps(state) + "\nnew_game={}"


class NativeAudioRuntimeTests(unittest.TestCase):
    def test_fresh_and_reload_checks_accept_expected_evidence(self):
        self.assertEqual(parse_native_audio_check(line(), fresh=True)["confirm_count"], 2)
        self.assertEqual(parse_native_audio_check(line(confirm_count=0, boot_services=0), fresh=False)["boot_services"], 0)

    def test_missing_duplicate_and_malformed_diagnostics_are_rejected(self):
        for output in ("new_game={}", line() + "\n" + line(), "audio_check={bad json}\n"):
            with self.subTest(output=output):
                with self.assertRaises(RuntimeError):
                    parse_native_audio_check(output, fresh=True)

    def test_invalid_counters_and_shutdown_are_rejected(self):
        for changes in ({"music_starts": 0}, {"confirm_count": -1},
                        {"queued_music_bytes": 0}, {"queued_music_bytes": 288001},
                        {"boot_services": True}, {"stopped": False},
                        {"assets_loaded": False}):
            with self.subTest(changes=changes):
                with self.assertRaises(RuntimeError):
                    parse_native_audio_check(line(**changes), fresh=True)

    def test_fresh_requires_boot_services_and_confirm(self):
        for changes in ({"confirm_count": 0}, {"boot_services": 0}):
            with self.subTest(changes=changes):
                with self.assertRaises(RuntimeError):
                    parse_native_audio_check(line(**changes), fresh=True)

    def test_unexpected_fields_are_rejected(self):
        with self.assertRaises(RuntimeError):
            parse_native_audio_check(line(extra=1), fresh=True)


if __name__ == "__main__":
    unittest.main()
