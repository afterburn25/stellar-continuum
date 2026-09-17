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


def moving_payload(day, position):
    value = payload(day)
    value["Galaxy"].update({"PlayerCivilizationId": 0, "Colonies": [{"CivilizationId": 0}],
        "Fleets": [{"Id": identity, "CivilizationId": 0, "IsActive": True,
                    "TransitPhase": 2, "X": position, "Y": identity}
                   for identity in (1, 2)]})
    return value


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

    def test_invalid_workload_requests_rejected_before_launch(self):
        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(campaign.subprocess, "run") as launch:
            for minimum in (True, -1, 1.0, 2501, 1):
                with self.subTest(minimum=minimum), self.assertRaises(RuntimeError):
                    campaign.validate_native_campaign_profile(Path(temporary), {}, minimum_moving_fleets=minimum)
            launch.assert_not_called()

    def test_workload_requires_real_motion_and_unique_owned_fleets(self):
        before = moving_payload(6., 0.)
        result = campaign._fleet_progress(before, moving_payload(8., 1.), 2)
        self.assertEqual(result["moved_fleet_ids"], [1, 2])
        for after in (moving_payload(8., 0.), payload(8.)):
            with self.subTest(after=after), self.assertRaises(RuntimeError):
                campaign._fleet_progress(before, after, 2)
        for field, broken in (("Fleets", {}), ("Colonies", [None]),
                              ("Fleets", [{"Id": 1, "CivilizationId": 0}] * 2)):
            invalid = moving_payload(6., 0.); invalid["Galaxy"][field] = broken
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                campaign._fleet_workload(invalid)

    def exercise(self, fault=None, initial=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "package"; root.mkdir(); (root / "stellar-continuum-native.exe").write_bytes(b"x")
            initial_save = Path(temporary) / "original.player17.json"
            original = json.dumps(moving_payload(6., 0.)).encode()
            if initial:
                initial_save.write_bytes(original)
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
                    before = (6. if initial else 0.) + active_index * 2.
                    after = before + 2.
                    value = state(before, before + .5, before + 1.0, after)
                    if fault == "stale_save": save.write_text(json.dumps(payload(before)))
                    elif fault == "save_shape": save.write_text(json.dumps({"FormatVersion": 17, "SimulationDays": after, "Galaxy": {"Systems": []}}))
                    else:
                        actual = moving_payload(after, 0. if fault == "stationary" else active_index + 1.) if initial else payload(after)
                        save.write_text(json.dumps(actual))
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
                if fault == "original_mutation": initial_save.write_bytes(b"changed")
                return mock.Mock(returncode=0, stdout="gpu_driver=vulkan systems=500 save=ok ", stderr="")

            with mock.patch.object(campaign.subprocess, "run", side_effect=invoke), mock.patch.object(campaign, "_bmp", side_effect=bmp):
                if fault:
                    with self.assertRaises(RuntimeError): campaign.validate_native_campaign_profile(root, {}, profile_frames=120,
                        initial_save=initial_save if initial else None, minimum_moving_fleets=2 if initial else 0)
                    return
                result = campaign.validate_native_campaign_profile(root, {}, profile_frames=120,
                    initial_save=initial_save if initial else None, minimum_moving_fleets=2 if initial else 0)
            self.assertEqual(len(calls), 4)
            self.assertEqual([row["after_days"] for row in result["campaignStates"]], [8., 10.] if initial else [2., 4.])
            self.assertEqual(len(result["campaignProfiles"]), 2)
            self.assertEqual(len(result["campaignColdProfiles"]), 2)
            if initial:
                self.assertEqual(initial_save.read_bytes(), original)
                self.assertEqual(len(result["campaignInitialSaveSha256"]), 64)
                for args in calls:
                    self.assertIn("--load", args)
                    self.assertNotIn(str(initial_save), args)
                self.assertEqual([w["moved_fleet_ids"] for w in result["campaignWorkloads"]], [[1, 2], [1, 2]])

    def test_two_active_sizes_each_have_exact_paused_reload(self): self.exercise()

    def test_existing_campaign_is_copied_and_real_fleet_progress_is_proved(self): self.exercise(initial=True)

    def test_existing_workload_rejects_stationary_fleets_and_original_mutation(self):
        for fault in ("stationary", "original_mutation"):
            with self.subTest(fault=fault): self.exercise(fault, initial=True)

    def test_lifecycle_rejects_process_renderer_timing_bitmap_and_save_faults(self):
        for fault in ("process_failure", "wrong_renderer", "missing_timing", "missing_bmp", "stale_save", "save_shape", "paused_mutation", "source_mutation"):
            with self.subTest(fault=fault): self.exercise(fault)


if __name__ == "__main__": unittest.main()
