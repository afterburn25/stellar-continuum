import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from native_audio_runtime import (
    native_audio_asset_files, validate_native_audio_export)


def diagnostic():
    return {
        "required": 1, "music": 1, "music_frames": 9361450, "device": 1,
        "voices": 2, "voiced_chunks": 12, "clipped_samples": 0,
        "peak": 0.5233, "master": 0.78, "music_gain": 0.64, "sfx_gain": 0.82,
        "settings": 1, "support": 1,
        "voice_settings": 1, "voice_pipeline": 1, "voice_lines": 1,
        "voice_backend": "Backend: windows-sapi · 2 voices",
        "video_settings": 1,
    }


class NativeAudioRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory(prefix="stellar-audio-test-") as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir()
            calls = []

            def launch(args, *, cwd, **unused):
                capture = Path(args[args.index("--audio-smoke") + 1])
                save = Path(args[args.index("--save-path") + 1])
                self.assertEqual(cwd, capture.parent)
                self.assertEqual(cwd, save.parent)
                calls.append(args)
                state = diagnostic()
                if fault == "required": state["required"] = 0
                if fault == "music": state["music"] = 0
                if fault == "frames": state["music_frames"] = 0
                if fault == "voices": state["voices"] = 0
                if fault == "chunks": state["voiced_chunks"] = 0
                if fault == "clipped": state["clipped_samples"] = 4
                if fault == "peak": state["peak"] = 0
                if fault == "gain": state["sfx_gain"] = 1.4
                if fault == "settings_flag": state["settings"] = 0
                if fault == "settings_mix": state["master"] = 0.4
                if fault == "support_flag": state["support"] = 0
                if fault == "voice_flag": state["voice_settings"] = 0
                if fault == "voice_lines": state["voice_lines"] = 0
                if fault == "voice_backend": state["voice_backend"] = "none"
                if fault == "video_flag": state["video_settings"] = 0
                varied = bytes(range(256)) if fault != "blank" else bytes(200)
                if fault != "capture":
                    capture.write_bytes(b"BM" + b"\0" * 52 + varied * 40)
                record = {"FormatVersion": 17}
                if fault == "save": record["FormatVersion"] = 16
                save.write_text(json.dumps(record), encoding="utf-8")
                if fault != "settings_missing":
                    persisted = {"master": 0.78, "music": 0.64, "sfx": 0.82}
                    if fault == "settings": persisted["master"] = 2
                    (save.parent / "audio-settings.json").write_text(
                        json.dumps(persisted), encoding="utf-8")
                if fault != "bundle_missing":
                    import zipfile
                    support = save.parent / "support"
                    support.mkdir(exist_ok=True)
                    bundle = support / "support-TEST.zip"
                    if fault == "bundle_bad":
                        bundle.write_bytes(b"PK not a zip")
                    else:
                        with zipfile.ZipFile(bundle, "w") as archive:
                            archive.writestr("game-TEST.log", "log")
                            archive.writestr("system-TEST.txt", "info")
                            archive.writestr("audio.player17.json",
                                             save.read_text(encoding="utf-8"))
                if fault != "voice_settings_missing":
                    voice = {"enableVoices": True, "subtitles": True,
                             "frequency": "Normal", "subtitleSize": 18}
                    if fault == "voice_state":
                        voice["enableVoices"] = False
                    (save.parent / "voice-settings.json").write_text(
                        json.dumps(voice), encoding="utf-8")
                if fault != "video_settings_missing":
                    video = {"display": "Borderless", "vsync": "On",
                             "frameCap": "Automatic"}
                    if fault == "video_state":
                        video["vsync"] = "Off"
                    (save.parent / "video-settings.json").write_text(
                        json.dumps(video), encoding="utf-8")
                stdout = "gpu_driver=vulkan systems=500 save=ok " + \
                    f"audio={json.dumps(state, separators=(',', ':'))}"
                if fault == "diagnostic":
                    stdout = "gpu_driver=vulkan systems=500 save=ok "
                return subprocess.CompletedProcess(args, 0, stdout, "")

            with mock.patch("native_audio_runtime.subprocess.run",
                            side_effect=launch):
                return validate_native_audio_export(package, {})

    def test_complete_actual_contract(self):
        result = self.exercise()
        self.assertTrue(result["nativeAudioStreams"])
        self.assertTrue(result["nativeAudioDevice"])
        self.assertTrue(result["nativeAudioCapture"].endswith("audio-ordered.bmp"))

    def test_missing_stream_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("required")
    def test_silent_music_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("music")
    def test_undecoded_music_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("frames")
    def test_no_voices_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("voices")
    def test_no_output_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("chunks")
    def test_clipping_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("clipped")
    def test_silent_peak_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("peak")
    def test_out_of_range_gain_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("gain")
    def test_missing_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("capture")
    def test_blank_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("blank")
    def test_damaged_payload_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("save")
    def test_invalid_settings_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("settings")
    def test_missing_settings_file_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("settings_missing")
    def test_unexercised_settings_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("settings_flag")
    def test_nondefault_settings_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("settings_mix")
    def test_missing_diagnostic_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("diagnostic")
    def test_unreported_support_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("support_flag")
    def test_missing_bundle_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("bundle_missing")
    def test_damaged_bundle_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("bundle_bad")
    def test_unexercised_voice_settings_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("voice_flag")
    def test_silent_voice_pipeline_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("voice_lines")
    def test_wrong_voice_backend_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("voice_backend")
    def test_missing_voice_settings_file_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("voice_settings_missing")
    def test_wrong_voice_state_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("voice_state")
    def test_unexercised_video_settings_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("video_flag")
    def test_missing_video_settings_file_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("video_settings_missing")
    def test_wrong_video_state_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("video_state")


class NativeAudioAssetTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory(prefix="stellar-audio-decl-") as temporary:
            root = Path(temporary)
            declaration = json.loads(
                (Path(__file__).resolve().parents[2] /
                 "export/native-audio-assets.json").read_text(encoding="utf-8"))
            import hashlib
            for key, record in declaration["assets"].items():
                target = root / record["source"]
                target.parent.mkdir(parents=True, exist_ok=True)
                if fault == key:
                    target.write_bytes(b"corrupted")
                else:
                    target.write_bytes(b"content-" + key.encode())
                    record["sha256"] = hashlib.sha256(
                        target.read_bytes()).hexdigest()
            if fault == "missing":
                (root / "assets/audio/sfx/ui-hover.wav").unlink()
            if fault == "extra":
                declaration["assets"]["bonus"] = dict(record)
            (root / "export").mkdir()
            (root / "export/native-audio-assets.json").write_text(
                json.dumps(declaration), encoding="utf-8")
            return native_audio_asset_files(root)

    def test_declaration_packages_exact_files(self):
        files = self.exercise()
        self.assertEqual(len(files), 8)
        self.assertIn("assets/audio/music/claimed-by-the-void-loop.mp3", files)
        self.assertIn("assets/audio/sfx/strategic-alert.wav", files)
        self.assertIn("Licenses/dr_mp3-MIT-0.txt", files)

    def test_unreviewed_content_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("music")
    def test_missing_file_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("missing")
    def test_unreviewed_extra_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("extra")


if __name__ == "__main__":
    unittest.main()
