import json
from pathlib import Path
import tempfile
import unittest

from native_audio_settings_runtime import (parse_native_audio_settings_check,
                                           verify_native_audio_settings_file)


def line(*, fresh=True, **changes):
    state = {"context": "startup" if fresh else "pause", "opened": True,
             "previewed": True, "muted": True, "cancel_restored": True,
             "saved": True, "reopened": True, "master": .25, "music": .5,
             "effects": .75, "initial_master": .78 if fresh else .25,
             "initial_music": .64 if fresh else .5,
             "initial_effects": .82 if fresh else .75, "initial_muted": False}
    state.update(changes)
    return "audio_settings_check=" + json.dumps(state, separators=(",", ":"))


class NativeAudioSettingsRuntimeTests(unittest.TestCase):
    def test_fresh_and_reload_evidence_are_accepted(self):
        self.assertEqual(parse_native_audio_settings_check(line(), fresh=True)["context"], "startup")
        self.assertEqual(parse_native_audio_settings_check(line(fresh=False), fresh=False)["context"], "pause")

    def test_diagnostic_requires_one_complete_strict_line(self):
        bad = ("unrelated", line() + "\n" + line(), "audio_settings_check={bad}",
               line(extra=True), line(master=True), line(master=float("nan")),
               line(master=10 ** 1000), line(context="pause"), line(opened=False),
               'audio_settings_check={"context":"startup","context":"pause"}',
               line() + " trailing-data")
        for output in bad:
            with self.subTest(output=output):
                with self.assertRaises(RuntimeError):
                    parse_native_audio_settings_check(output, fresh=True)

    def test_expected_levels_use_tolerance_and_initial_state(self):
        parse_native_audio_settings_check(line(master=.250009), fresh=True)
        for changes in ({"master": .25002}, {"initial_master": .25},
                        {"initial_muted": True}, {"effects": float("inf")}):
            with self.subTest(changes=changes):
                with self.assertRaises(RuntimeError):
                    parse_native_audio_settings_check(line(**changes), fresh=True)

    def test_persistence_requires_exact_schema_and_values(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "audio-settings.json"
            path.write_text('{"schemaVersion":1,"master":0.25,"music":0.5,"effects":0.75,"muted":false}', encoding="utf-8")
            self.assertEqual(verify_native_audio_settings_file(path), path.read_bytes())
            for text in ('{}', '{"schemaVersion":true,"master":0.25,"music":0.5,"effects":0.75,"muted":false}',
                         '{"schemaVersion":1,"master":true,"music":0.5,"effects":0.75,"muted":false}',
                         '{"schemaVersion":1,"master":0.25,"music":0.5,"effects":0.75,"muted":false,"extra":1}',
                         '{"schemaVersion":1,"master":0.25,"master":0.25,"music":0.5,"effects":0.75,"muted":false}'):
                path.write_text(text, encoding="utf-8")
                with self.subTest(text=text):
                    with self.assertRaises(RuntimeError):
                        verify_native_audio_settings_file(path)

    def test_persistence_rejects_invalid_utf8_and_oversize_documents(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "audio-settings.json"
            for data in (b'\xff', b"{" + b" " * 4096):
                path.write_bytes(data)
                with self.subTest(data=data[:1]):
                    with self.assertRaises(RuntimeError):
                        verify_native_audio_settings_file(path)


if __name__ == "__main__":
    unittest.main()
