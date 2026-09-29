import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from native_system_travel_runtime import validate_native_system_travel_export


class NativeSystemTravelExportTests(unittest.TestCase):
    def exercise(self, fault=None, *, voice_check=False, replay_check=False):
        source = {"FormatVersion": 17, "SavedAtUtc": "source", "SimulationDays": 0,
                  "Galaxy": {"PlayerCivilizationId": 0, "Systems": [0, 1, 2],
                             "Knowledge": [{"CivilizationId": 0, "KnownSystemIds": [0],
                                            "SystemSurveys": [{"SystemId": 0, "Level": 3, "Progress": 1}]}],
                             "Fleets": [{"Id": 7, "CivilizationId": 0, "IsActive": True,
                                         "CurrentSystemId": 0, "DestinationSystemId": None,
                                         "MissionOrderRevision": 0, "TransitPhase": 0,
                                         "PlannedRouteSystemIds": [], "LocalTransitPositionX": 0,
                                         "LocalTransitPositionY": 0}], "Economies": [{"Credits": 200}]}}
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary)/"package"
            package.mkdir()
            calls = []

            def run(args, *, cwd, env, **unused):
                self.assertNotEqual(cwd, package)
                self.assertIn("--load", args)
                save = Path(args[args.index("--save-path")+1])
                payload = json.loads(save.read_text())
                fleet = payload["Galaxy"]["Fleets"][0]
                capture = Path(args[-1])
                calls.append(args)
                marker = ""
                voice_marker = ""
                journal_marker = ""
                if "--fleet-smoke" in args:
                    self.assertNotIn("--audio-check", args)
                    self.assertNotIn("--voice-check", args)
                    payload["SimulationDays"] = 1
                    fleet.update(DestinationSystemId=2, MissionOrderRevision=1, TransitPhase=1,
                                 PlannedRouteSystemIds=[1, 2], LocalTransitPositionX=.1)
                    marker = " fleet=7:2:1:0.1"
                    if fault == "preparation": fleet["TransitPhase"] = 2
                elif "--system-travel-smoke" in args or "--system-travel-reload-smoke" in args:
                    if "--record" in args or "--replay" in args:
                        self.assertNotIn("--system-travel-reload-smoke", args)
                    if voice_check and "--replay" not in args:
                        self.assertIn("--audio-check", args)
                        self.assertIn("--voice-check", args)
                    else:
                        self.assertNotIn("--audio-check", args)
                        self.assertNotIn("--voice-check", args)
                    if "--record" in args:
                        journal = Path(args[args.index("--record") + 1])
                        journal.write_text("journal", encoding="utf-8")
                        journal_marker = (' replay={"commands":17,"checkpoints":42,'
                                          '"file":"moved.replay"}')
                    if "--replay" in args:
                        self.assertIn("--replay-exit", args)
                        journal = Path(args[args.index("--replay") + 1])
                        self.assertTrue(journal.is_file())
                        if fault != "replay_missing":
                            counts = (7, 3) if fault == "replay_counts" else (17, 42)
                            journal_marker = (' replay_verified={"commands":%d,"checkpoints":%d}'
                                              % counts)
                    observer=payload["Galaxy"]["Knowledge"][0]
                    self.assertEqual(observer["KnownSystemIds"], [0, 1])
                    self.assertNotIn(2, observer["KnownSystemIds"])
                    state = {"fleet_id": 7, "system_id": 0, "destination_id": 2, "order_revision": 1,
                             "before_x": .1, "before_y": 0, "after_x": .2, "after_y": 0,
                             "before_days": 1, "after_days": 2,
                             **{key: True for key in ("selected", "canonical_moved", "rendered_moved",
                                                       "paused_stable", "pause_retained", "known_arrow",
                                                       "unknown_denied", "knowledge_unchanged", "lanes_connected")}}
                    payload["SimulationDays"] = 2
                    fleet["LocalTransitPositionX"] = .2
                    if "--system-travel-reload-smoke" in args:
                        state.update(before_x=.2, before_days=2, canonical_moved=False, rendered_moved=False)
                        if fault == "reload": payload["Galaxy"]["Economies"][0]["Credits"] += 1
                    if fault in state: state[fault] = False
                    if fault == "nonfinite": state["after_x"] = float("nan")
                    if fault == "frozen": state["after_x"] = .1
                    if fault == "mismatch": fleet["LocalTransitPositionX"] = .3
                    if fault == "order": fleet["MissionOrderRevision"] = 2
                    if fault == "foreign": fleet["CivilizationId"] = 1
                    if fault == "clock": payload["SimulationDays"] = 3
                    marker = " system_travel=" + json.dumps(state)
                    if voice_check:
                        audio = {"assets_loaded": True, "music_starts": 1,
                                 "confirm_count": 0, "queued_music_bytes": 2048,
                                 "boot_services": 0, "stopped": True}
                        voice = {"available": True, "played": 1, "unknown_denied": True,
                                 "overlap_prevented": True, "queue_bounded": True, "stopped": True}
                        if fault == "voice": voice["unknown_denied"] = False
                        voice_marker = ("audio_check=" + json.dumps(audio, separators=(",", ":")) + "\n" +
                                        "voice_check=" + json.dumps(voice, separators=(",", ":")) + "\n")
                else:
                    if fault == "reload": payload["Galaxy"]["Economies"][0]["Credits"] += 1
                payload["SavedAtUtc"] = "later-" + str(len(calls))
                save.write_text(json.dumps(payload))
                if "--fleet-smoke" in args:
                    # The real atomic save leaves .bak/.integrity sidecars that
                    # the validator's recon rewrite must clear before relaunch.
                    Path(str(save) + ".bak").write_text("{}")
                    Path(str(save) + ".integrity").write_text("{}")
                if "--system-travel" in args:
                    self.assertFalse(list(save.parent.glob(save.name + ".*")),
                                     "stale save sidecars survived the recon rewrite")
                if fault != "capture": capture.write_bytes(b"BM"+bytes(54))
                stdout = voice_marker + "gpu_driver=vulkan systems=3 save=ok screenshot=test.bmp"+journal_marker+marker
                return subprocess.CompletedProcess(args, 0, stdout, "")

            with mock.patch("native_system_travel_runtime._source_row", return_value=copy.deepcopy(source)), \
                 mock.patch("native_system_travel_runtime.subprocess.run", side_effect=run):
                result = validate_native_system_travel_export(package, {}, Path("source"),
                                                              voice_check=voice_check,
                                                              replay_check=replay_check)
            self.assertEqual(len(calls), 4 if replay_check else 3)
            self.assertTrue(result["nativeSystemTravelInput"])
            self.assertTrue(result["nativeSystemLocalTransit"])
            self.assertTrue(result["nativeSystemTravelPausedReload"])
            if replay_check:
                self.assertTrue(result["nativeSystemTravelReplayVerified"])
                self.assertEqual(result["systemTravelReplay"],
                                 {"commands": 17, "checkpoints": 42})
            else:
                self.assertNotIn("nativeSystemTravelReplayVerified", result)
            if voice_check:
                self.assertTrue(result["nativeScientistVoice"])
                self.assertEqual(result["nativeScientistVoiceDiagnostics"]["moved"]["voice"]["played"], 1)
            else:
                self.assertNotIn("nativeScientistVoice", result)
            return result

    def test_real_input_motion_and_full_paused_reload(self): self.exercise()
    def test_replay_check_verifies_journaled_saves(self):
        self.exercise(replay_check=True)
    def test_replay_check_requires_verified_line(self):
        with self.assertRaisesRegex(RuntimeError, "replay_verified"):
            self.exercise("replay_missing", replay_check=True)
    def test_replay_check_rejects_count_mismatch(self):
        with self.assertRaisesRegex(RuntimeError, "different journal"):
            self.exercise("replay_counts", replay_check=True)
    def test_route_must_be_in_local_departure(self):
        with self.assertRaises(RuntimeError): self.exercise("preparation")
    def test_known_arrow_navigation_is_required(self):
        with self.assertRaises(RuntimeError): self.exercise("known_arrow")
    def test_unknown_arrow_must_be_denied(self):
        with self.assertRaises(RuntimeError): self.exercise("unknown_denied")
    def test_false_lane_membership_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("lanes_connected")
    def test_unproven_pause_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("paused_stable")
    def test_nonfinite_position_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("nonfinite")
    def test_frozen_position_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("frozen")
    def test_diagnostic_must_match_saved_position(self):
        with self.assertRaises(RuntimeError): self.exercise("mismatch")
    def test_navigation_cannot_issue_another_order(self):
        with self.assertRaises(RuntimeError): self.exercise("order")
    def test_foreign_fleet_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("foreign")
    def test_diagnostic_must_match_saved_clock(self):
        with self.assertRaises(RuntimeError): self.exercise("clock")
    def test_reload_cannot_mutate_the_world(self):
        with self.assertRaises(RuntimeError): self.exercise("reload")
    def test_missing_capture_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("capture")
    def test_voice_check_routes_only_moved_and_paused_reload(self): self.exercise(voice_check=True)
    def test_voice_evidence_is_strict_when_requested(self):
        with self.assertRaises(RuntimeError): self.exercise("voice", voice_check=True)


if __name__ == "__main__": unittest.main()
