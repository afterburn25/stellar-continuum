import copy
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

from native_fresh_progression_runtime import validate_native_fresh_progression_export


def bmp(width, height):
    stride = (width * 3 + 3) & ~3
    pixels = bytes(range(251)) * ((stride * height // 251) + 1)
    pixels = pixels[:stride * height]
    return (b"BM" + struct.pack("<IHHI", 54 + len(pixels), 0, 0, 54) +
            struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0,
                        len(pixels), 2835, 2835, 0, 0) + pixels)


def payload(days=0):
    return {"FormatVersion": 17, "SavedAtUtc": "saved", "SimulationDays": days,
            "Galaxy": {"Seed": 115501, "PlayerCivilizationId": 0,
                       "Civilizations": [{"Id": 0, "IsPlayer": True}],
                       "Systems": [{"Id": i} for i in range(500)],
                       "Fleets": [
                           {"Id": 7, "CivilizationId": 0, "Role": 0,
                            "DesignId": "warp_scout", "IsActive": True},
                           {"Id": 8, "CivilizationId": 0, "Role": 1,
                            "DesignId": "science_vessel", "IsActive": True}],
                       "ConstructionStates": [{"CivilizationId": 0,
                           "CompletedProjectIds": ["orbital_launch_complex",
                                                    "orbital_shipyard",
                                                    "warp_test_facility"]}],
                       "ShipyardStates": [{"CivilizationId": 0,
                                            "NextOrderSequence": 3,
                                            "ActiveDesignId": None,
                                            "ActiveOrderId": None,
                                            "QueuedBuilds": [],
                                            "ReservedPopulationMillions": 0,
                                            "ReservedPopulationSpeciesId": None,
                                            "ReservedPopulationSourceColonyId": None}],
                       "Economies": [{"CivilizationId": 0, "Credits": 1}]},
            "AdaptiveResearch": {"Civilizations": [{"CivilizationId": 0,
                "Research": {"Research": {"Research": {"Research": {
                    "Core": {"Capabilities": [
                        {"CapabilityId": "spacecraft_construction"},
                        {"CapabilityId": "experimental_interstellar_transit"},
                        {"CapabilityId": "orbital_industry"}],
                        "Nodes": [{"NodeId": node, "Maturity": 7} for node in (
                            "in_space_assembly", "asteroid_prospecting", "asteroid_mining",
                            "vacuum_refining", "orbital_manufacturing", "orbital_shipyard",
                            "gravitational_physics", "field_theory", "warp_metric_theory",
                            "exotic_energy_coupling", "micro_field_distortion",
                            "warp_field_control", "prototype_warp_drive")]}}}}}}]}}


def proof(mode, days=100):
    return {"mode": mode, "seed": 115501, "player_id": 0,
            "before_days": 0 if mode == "fresh" else days,
            "after_days": days, "scout_id": 7, "science_id": 8,
            "research_actions": 13 if mode == "fresh" else 0,
            "construction_actions": 3 if mode == "fresh" else 0,
            "ship_actions": 2 if mode == "fresh" else 0,
            "no_instant_ships": True, "offline_quarter_day_steps": 400 if mode == "fresh" else 0,
            "ui_input": True, "paused": True, "save_roundtrip": True}


class NativeFreshProgressionRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary) / "package"
            package.mkdir()
            calls = []

            def run(args, *, cwd, env, **unused):
                save = Path(args[args.index("--save-path") + 1])
                reload = "--fresh-progression-reload-smoke" in args
                mode = "paused_reload" if reload else "fresh"
                test_days = 100.25 if fault == "fractional_quarter" else 100.1 if fault == "nonquarter" else 100
                current = json.loads(save.read_text()) if reload else payload(test_days)
                current["SavedAtUtc"] = f"saved-{len(calls)}"
                if fault == "changed_reload" and reload:
                    current["Galaxy"]["Economies"][0]["Credits"] = 2
                if fault == "fleet_design":
                    current["Galaxy"]["Fleets"][0]["DesignId"] = "unknown"
                if fault == "fleet_owner":
                    current["Galaxy"]["Fleets"][0]["CivilizationId"] = 1
                if fault == "missing_prereq":
                    current["Galaxy"]["ConstructionStates"][0]["CompletedProjectIds"].pop()
                if fault == "archived_gate":
                    current["AdaptiveResearch"]["Civilizations"][0]["Research"]["Research"]["Research"]["Research"]["Core"]["Nodes"][-1]["Maturity"] = 8
                if fault == "prototype_demonstrated":
                    current["AdaptiveResearch"]["Civilizations"][0]["Research"]["Research"]["Research"]["Research"]["Core"]["Nodes"][-1]["Maturity"] = 5
                if fault == "duplicate_nodes":
                    nodes = current["AdaptiveResearch"]["Civilizations"][0]["Research"]["Research"]["Research"]["Research"]["Core"]["Nodes"]
                    nodes.append(copy.deepcopy(nodes[0]))
                if fault == "unfinished_order":
                    current["Galaxy"]["ShipyardStates"][0]["ActiveOrderId"] = "ship-3"
                if fault == "extra_order":
                    current["Galaxy"]["ShipyardStates"][0]["NextOrderSequence"] = 4
                save.write_text(json.dumps(current), encoding="utf-8")
                marker = "--fresh-progression-reload-smoke" if reload else "--fresh-progression-smoke"
                capture = Path(args[args.index(marker) + 1])
                if fault != "missing_capture":
                    capture.write_bytes(bmp(1279 if fault == "geometry" else int(args[args.index("--width") + 1]),
                                            int(args[args.index("--height") + 1])))
                report = proof(mode, test_days)
                if fault == "fractional_quarter" and mode == "fresh":
                    report["offline_quarter_day_steps"] = 401
                if fault == "missing_proof": line = ""
                else:
                    if fault == "forged_id": report["scout_id"] = 99
                    if fault == "invalid_type": report["seed"] = True
                    if fault == "forged_steps": report["offline_quarter_day_steps"] = 1
                    if fault == "forged_seed": report["seed"] = 115500
                    if fault == "missing_research_action": report["research_actions"] = 12
                    if fault == "floating_seed": report["seed"] = 115501.0
                    line = "fresh_progression=" + json.dumps(report, separators=(",", ":"))
                    if fault == "duplicate_proof": line += "\n" + line
                    if fault == "duplicate_keys": line = line.replace('"mode":"fresh"', '"mode":"fresh","mode":"fresh"', 1)
                dimensions = {"path": str(capture), "width": int(args[args.index("--width") + 1]),
                              "height": int(args[args.index("--height") + 1])}
                stdout = ("gpu_driver=vulkan systems=500 save=ok \n" +
                          f"native_capture={json.dumps(dimensions, separators=(',', ':'))}\n" + line)
                calls.append(args)
                if fault == "terminal":
                    return subprocess.CompletedProcess(args, 9, stdout, "terminal-diagnostic")
                return subprocess.CompletedProcess(args, 0, stdout, "")

            with mock.patch("native_fresh_progression_runtime.subprocess.run", side_effect=run):
                result = validate_native_fresh_progression_export(package, {})
            self.assertEqual(len(calls), 2)
            self.assertTrue(result["nativeFreshProgression"])
            self.assertTrue(result["nativeFreshProgressionReload"])
            self.assertEqual(len(result["freshProgressionCaptures"]), 2)
            self.assertEqual(len(result["freshProgressionSaveCaptures"]), 2)

    def test_success(self): self.exercise()
    def test_fractional_quarter_days_are_valid(self): self.exercise("fractional_quarter")
    def test_nonquarter_days_are_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("nonquarter")
    def test_missing_proof(self):
        with self.assertRaises(RuntimeError): self.exercise("missing_proof")
    def test_duplicate_proof(self):
        with self.assertRaises(RuntimeError): self.exercise("duplicate_proof")
    def test_forged_id(self):
        with self.assertRaises(RuntimeError): self.exercise("forged_id")
    def test_fleet_design_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("fleet_design")
    def test_fleet_owner_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("fleet_owner")
    def test_invalid_type(self):
        with self.assertRaises(RuntimeError): self.exercise("invalid_type")
    def test_archived_research_gate_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("archived_gate")
    def test_demonstrated_prototype_gate_is_valid(self):
        self.exercise("prototype_demonstrated")
    def test_duplicate_research_nodes_are_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("duplicate_nodes")
    def test_forged_steps_are_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("forged_steps")
    def test_forged_seed_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("forged_seed")
    def test_missing_research_action_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("missing_research_action")
    def test_floating_seed_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("floating_seed")
    def test_duplicate_json_keys_are_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("duplicate_keys")
    def test_unrelated_payload_changed_on_reload(self):
        with self.assertRaises(RuntimeError): self.exercise("changed_reload")
    def test_missing_prerequisite(self):
        with self.assertRaises(RuntimeError): self.exercise("missing_prereq")
    def test_unfinished_order_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("unfinished_order")
    def test_extra_order_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("extra_order")
    def test_missing_capture(self):
        with self.assertRaises(RuntimeError): self.exercise("missing_capture")
    def test_capture_geometry(self):
        with self.assertRaises(RuntimeError): self.exercise("geometry")
    def test_terminal_diagnostics_propagate(self):
        with self.assertRaisesRegex(RuntimeError, "terminal-diagnostic"):
            self.exercise("terminal")


if __name__ == "__main__":
    unittest.main()
