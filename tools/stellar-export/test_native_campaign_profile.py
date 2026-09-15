import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import native_campaign_profile as campaign
from test_native_frame_profile import cold_profile, profile as steady_profile


def state(before=0.0, middle=1.0, completed=1.5, after=2.0):
    return {"samples": 120, "speed_multiplier": 8, "before_days": before,
            "mid_save_day": middle, "mid_save_completed_day": completed,
            "after_days": after, "mid_save_completed": True,
            "advanced_after_mid_save": True, "speed_input": True,
            "resume_input": True, "pause_input": True, "final_saved": True}


def payload(day):
    return {"FormatVersion": 17, "SimulationDays": day,
            "Galaxy": {"Systems": [{} for _ in range(500)]}, "SavedAtUtc": "now"}


class CampaignProfileTests(unittest.TestCase):
    def test_strict_campaign_fields_reject_valid_sample_faults(self):
        valid = state()
        self.assertEqual(campaign.validate_campaign_profile("campaign_profile=" + json.dumps(valid), 120), valid)
        cases = []
        wrong_samples = state(); wrong_samples["samples"] = 120.0; cases.append(wrong_samples)
        wrong_speed = state(); wrong_speed["speed_multiplier"] = True; cases.append(wrong_speed)
        wrong_flag = state(); wrong_flag["resume_input"] = 1; cases.append(wrong_flag)
        nonfinite = state(); nonfinite["after_days"] = float("inf"); cases.append(nonfinite)
        unordered = state(); unordered["mid_save_completed_day"] = unordered["after_days"]; cases.append(unordered)
        missing = state(); del missing["mid_save_completed_day"]; cases.append(missing)
        for value in cases:
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                campaign.validate_campaign_profile("campaign_profile=" + json.dumps(value), 120)
        duplicate = json.dumps(valid).replace('"samples": 120', '"samples":120,"samples":120')
        for stdout in ("save=ok", "campaign_profile={", "campaign_profile=" + duplicate,
                       "campaign_profile=" + json.dumps(valid) + " campaign_profile=" + json.dumps(valid)):
            with self.subTest(stdout=stdout), self.assertRaises(RuntimeError):
                campaign.validate_campaign_profile(stdout, 120)

    def test_invalid_frame_args_rejected_before_launch(self):
        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(campaign.subprocess, "run") as launch:
            for frames in (True, -1, 0, 119, 120.0, 3601):
                with self.subTest(frames=frames), self.assertRaises(RuntimeError):
                    campaign.validate_native_campaign_profile(Path(temporary), {}, profile_frames=frames)
            launch.assert_not_called()

    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "package"; root.mkdir(); (root / "stellar-continuum-native.exe").write_bytes(b"x")
            calls, source_paths = [], []

            def bmp(path, width, height):
                if not path.is_file() or path.read_bytes() != b"bmp":
                    raise RuntimeError("missing bitmap")
                return b"pixels"

            def invoke(args, **unused):
                calls.append(args)
                active = "--campaign-profile" in args
                save = Path(args[args.index("--save-path") + 1])
                capture = Path(args[args.index("--campaign-profile") + 1] if active else args[args.index("--smoke") + 1])
                if active:
                    active_index = len(source_paths)
                    before, after = (0.0, 2.0) if active_index == 0 else (2.0, 4.0)
                    value = state(before, before + .5, before + 1.0, after)
                    if fault == "stale_save": save.write_text(json.dumps(payload(before)))
                    elif fault == "save_shape": save.write_text(json.dumps({"FormatVersion": 17, "SimulationDays": after, "Galaxy": {"Systems": []}}))
                    else: save.write_text(json.dumps(payload(after)))
                    source_paths.append(save)
                    if fault != "missing_bmp": capture.write_bytes(b"bmp")
                    stdout = "gpu_driver=vulkan systems=500 save=ok image_uploads=3 campaign_profile=" + json.dumps(value)
                    if fault != "missing_timing": stdout += " steady_profile=" + json.dumps(steady_profile()) + " cold_profile=" + json.dumps(cold_profile())
                    if fault == "wrong_renderer": stdout = stdout.replace("gpu_driver=vulkan ", "gpu_driver=software ")
                    return mock.Mock(returncode=1 if fault == "process_failure" else 0, stdout=stdout, stderr="failed")
                paused_payload = json.loads(save.read_text())
                if fault == "paused_mutation": paused_payload["Galaxy"]["Systems"][0]["changed"] = True
                save.write_text(json.dumps(paused_payload)); capture.write_bytes(b"bmp")
                if fault == "source_mutation":
                    original = source_paths[-1]; changed = json.loads(original.read_text()); changed["Galaxy"]["Systems"][0]["changed"] = True; original.write_text(json.dumps(changed))
                return mock.Mock(returncode=0, stdout="gpu_driver=vulkan systems=500 save=ok ", stderr="")

            with mock.patch.object(campaign.subprocess, "run", side_effect=invoke), mock.patch.object(campaign, "_bmp", side_effect=bmp):
                if fault:
                    with self.assertRaises(RuntimeError): campaign.validate_native_campaign_profile(root, {}, profile_frames=120)
                    return
                result = campaign.validate_native_campaign_profile(root, {}, profile_frames=120)
            self.assertEqual(len(calls), 4)
            self.assertEqual([row["after_days"] for row in result["campaignStates"]], [2.0, 4.0])
            self.assertEqual(len(result["campaignProfiles"]), 2)
            self.assertEqual(len(result["campaignColdProfiles"]), 2)

    def test_two_active_sizes_each_have_exact_paused_reload(self): self.exercise()

    def test_lifecycle_rejects_process_renderer_timing_bitmap_and_save_faults(self):
        for fault in ("process_failure", "wrong_renderer", "missing_timing", "missing_bmp", "stale_save", "save_shape", "paused_mutation", "source_mutation"):
            with self.subTest(fault=fault): self.exercise(fault)


if __name__ == "__main__": unittest.main()
