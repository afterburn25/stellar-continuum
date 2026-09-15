import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from native_ship_art_runtime import (
    native_ship_art_asset_files, validate_native_ship_art_export)


FIXTURE = {
    "Rows": [{"Name": "valid-current17",
              "InputJson": json.dumps(
                  {"FormatVersion": 17,
                   "Galaxy": {"Systems": list(range(500)),
                              "PlayerCivilizationId": 4,
                              "ConstructionStates": [
                                  {"CivilizationId": 4,
                                   "CompletedProjectIds": []}],
                              "Fleets": [{"Id": 9, "CivilizationId": 4,
                                          "IsActive": True,
                                          "DestinationSystemId": None,
                                          "PlannedRouteSystemIds": [],
                                          "MissionOrderRevision": 3,
                                          "TransitProgress": 0.0}]},
                   "AdaptiveResearch": {"Civilizations": [
                       {"CivilizationId": 4,
                        "Research": {"Research": {"Research": {"Research": {
                            "Core": {"Capabilities": []}}}}}}]}},
                   separators=(",", ":"))}]}


def diagnostic():
    return {
        "shipyard_art_rows": 4, "fleet_art_rows": 3, "decoded_sources": 6,
        "cached_entries": 6, "cache_bytes": 1204224, "routed_fleets": 1,
        "drawn_legs": 2, "chevron_segments": 2, "trail_strokes": 3,
        "position_circles": 1, "route_lines": 21, "routed_fleet_id": 9,
        "destination": 7,
    }


def payload():
    return {"FormatVersion": 17, "SavedAtUtc": "now", "SimulationDays": 2.5,
            "Galaxy": {"Systems": list(range(500)),
                       "PlayerCivilizationId": 4,
                       "Fleets": [{"Id": 9, "CivilizationId": 4,
                                   "IsActive": True,
                                   "DestinationSystemId": 7,
                                   "PlannedRouteSystemIds": [12, 7],
                                   "MissionOrderRevision": 4,
                                   "TransitProgress": 0.4}]}}


class NativeShipArtRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory(prefix="stellar-ship-art-test-") as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir()
            fixture = root / "fixture.json"
            fixture.write_text(json.dumps(FIXTURE), encoding="utf-8")
            calls = []

            def launch(args, *, cwd, **unused):
                shipyard = Path(args[args.index("--ship-art-smoke") + 1])
                save = Path(args[args.index("--save-path") + 1])
                self.assertEqual(cwd, shipyard.parent)
                self.assertEqual(cwd, save.parent)
                calls.append(args)
                state = diagnostic()
                if fault == "shipyard_rows": state["shipyard_art_rows"] = 0
                if fault == "fleet_rows": state["fleet_art_rows"] = 0
                if fault == "decoded": state["decoded_sources"] = 7
                if fault == "recount": state["decoded_sources"] = 5
                if fault == "budget": state["cache_bytes"] = 5 * 1024 * 1024
                if fault == "route": state["routed_fleets"] = 0
                if fault == "trails": state["trail_strokes"] = 1
                if fault == "destination": state["destination"] = 8
                record = payload()
                if fault == "save": record["Galaxy"]["Systems"] = [1]
                if fault == "foreign": record["Galaxy"]["Fleets"][0]["CivilizationId"] = 2
                if len(calls) == 1 and fault == "unrouted":
                    record["Galaxy"]["Fleets"][0]["PlannedRouteSystemIds"] = []
                if len(calls) == 2 and fault == "reload":
                    record["Galaxy"]["Fleets"][0]["TransitProgress"] = 0.9
                save.write_text(json.dumps(record), encoding="utf-8")
                varied = bytes(range(256)) if fault != "blank" else bytes(200)
                shipyard.write_bytes(b"BM" + b"\0" * 52 + varied * 40)
                map_path = shipyard.with_name(shipyard.stem + "-map" + shipyard.suffix)
                if fault != "capture":
                    other = (varied if fault == "same_capture"
                             else bytes(reversed(range(256))))
                    map_path.write_bytes(b"BM" + b"\0" * 52 + other * 40)
                return subprocess.CompletedProcess(
                    args, 0,
                    f"gpu_driver=vulkan systems=500 image_uploads=24 save=ok "
                    f"ship_art={json.dumps(state, separators=(',', ':'))}", "")

            with mock.patch("native_ship_art_runtime.subprocess.run",
                            side_effect=launch):
                result = validate_native_ship_art_export(package, {}, fixture)
            self.assertEqual(len(calls), 2)
            self.assertIn("--load", calls[0])
            self.assertIn("--load", calls[1])
            self.assertEqual(len(result["shipArtCaptures"]), 4)
            self.assertTrue(result["nativeShipyardArtwork"])
            self.assertTrue(result["nativeFleetRouteEffects"])

    def test_complete_actual_contract(self): self.exercise()
    def test_missing_shipyard_rows_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("shipyard_rows")
    def test_missing_fleet_rows_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("fleet_rows")
    def test_extra_source_decode_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("decoded")
    def test_repeated_decode_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("recount")
    def test_cache_budget_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("budget")
    def test_missing_route_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("route")
    def test_inconsistent_trails_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("trails")
    def test_destination_mismatch_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("destination")
    def test_damaged_payload_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("save")
    def test_foreign_fleet_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("foreign")
    def test_unrouted_fleet_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("unrouted")
    def test_reload_mutation_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("reload")
    def test_missing_map_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("capture")
    def test_identical_captures_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("same_capture")
    def test_blank_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("blank")


class NativeShipArtAssetTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory(prefix="stellar-ship-decl-") as temporary:
            root = Path(temporary)
            assets = root / "assets/visual/ships"
            docs = root / "docs/engine"
            assets.mkdir(parents=True)
            docs.mkdir(parents=True)
            declaration = json.loads(
                (Path(__file__).resolve().parents[2] /
                 "export/native-ship-art-assets.json").read_text(encoding="utf-8"))
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
                (root / "assets/visual/ships/pathfinder-scout.jpg").unlink()
            (root / "export").mkdir()
            (root / "export/native-ship-art-assets.json").write_text(
                json.dumps(declaration), encoding="utf-8")
            files = native_ship_art_asset_files(root)
            return files

    def test_declaration_packages_exact_files(self):
        files = self.exercise()
        self.assertEqual(len(files), 7)
        self.assertIn("Licenses/Ship-art-sources.md", files)
        self.assertIn("assets/visual/ships/patrol-corvette.jpg", files)

    def test_unreviewed_content_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("patrol-corvette")

    def test_missing_source_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("missing")


if __name__ == "__main__":
    unittest.main()
