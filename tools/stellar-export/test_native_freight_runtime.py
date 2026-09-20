import copy
import json
import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from native_freight_runtime import _assert_dispatch_delta, _proof, validate_native_freight_export


def bmp(width, height):
    stride = ((width * 24 + 31) // 32) * 4
    payload = (bytes(range(251)) * ((stride * height + 250) // 251))[:stride * height]
    data = bytearray(54 + len(payload)); data[:2] = b"BM"
    struct.pack_into("<I", data, 2, len(data)); struct.pack_into("<I", data, 10, 54)
    struct.pack_into("<IiiHHI", data, 14, 40, width, height, 1, 24, 0)
    data[54:] = payload
    return bytes(data)


class FreightRuntimeTests(unittest.TestCase):
    def proof(self, mode="dispatch", **changes):
        value = {"mode": mode, "player_id": 1, "colony_id": 21, "body_id": 7,
                 "system_id": 10, "fleet_id": 3, "home_colony_id": 20,
                 "reviewed": mode == "dispatch", "cancelled": mode == "dispatch",
                 "dispatched": True, "paused": True, "day_unchanged": True}
        value.update(changes)
        return "outpost_freight=" + json.dumps(value, separators=(",", ":"))

    def test_proof_rejects_missing_malformed_and_identity(self):
        for text in ("", "outpost_freight={bad}", self.proof(player_id=-1), self.proof(dispatched=False)):
            with self.assertRaises(RuntimeError): _proof(text, "dispatch")
        duplicate = self.proof().replace('"player_id":1', '"player_id":1,"player_id":1')
        with self.assertRaises(RuntimeError): _proof(duplicate, "dispatch")

    def test_reload_proof_requires_persisted_dispatch_flags(self):
        with self.assertRaises(RuntimeError): _proof(self.proof("reload", dispatched=False), "reload")
        with self.assertRaises(RuntimeError): _proof(self.proof("reload", day_unchanged=False), "reload")

    def test_dispatch_delta_allows_only_freight_route_fields(self):
        before = {"SavedAtUtc": "before", "Galaxy": {"Fleets": [{"Id": 4, "Name": "f", "CargoMaterials": 0}]}}
        after = copy.deepcopy(before)
        after["SavedAtUtc"] = "after"
        after["Galaxy"]["Fleets"][0].update({"FreightTargetOutpostId": 21,
                                               "FreightHomeColonyId": 20,
                                               "CargoMaterials": 0})
        _assert_dispatch_delta(before, after, {"fleet_id": 4, "colony_id": 21,
                                               "home_colony_id": 20})
        after["Galaxy"]["Fleets"][0]["Name"] = "mutated"
        with self.assertRaises(RuntimeError): _assert_dispatch_delta(before, after, {"fleet_id": 4,
                                                                                       "colony_id": 21,
                                                                                       "home_colony_id": 20})

    def test_export_mock_proves_two_resolutions(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); package = root / "package"; package.mkdir()
            exe = package / "stellar-continuum-native.exe"; exe.write_bytes(b"native")
            base = {"FormatVersion": 17, "SimulationDays": 0,
                    "Galaxy": {"PlayerCivilizationId": 1,
                               "Civilizations": [{"Id": 1, "HomeSystemId": 10, "SpeciesId": "terran_baseline"}],
                               "Systems": [{"Id": 10, "X": 12.0, "Y": 24.0}],
                               "PlanetaryBodies": [{"Id": 7, "SystemId": 10,
                                                    "Environment": {"HasSolidSurface": True},
                                                    "HasRareResource": True}],
                               "Knowledge": [{"CivilizationId": 1, "KnownSystemIds": [10],
                                               "SystemSurveys": [{"SystemId": 10, "Level": 3}]}],
                               "Colonies": [{"Id": 20, "CivilizationId": 1, "SystemId": 10,
                                              "Kind": 0, "SurfaceBuildings": []}],
                               "Fleets": [{"Id": 2, "CivilizationId": 1, "Role": 1,
                                           "DesignId": "scout", "IsActive": True,
                                           "CurrentSystemId": 10, "CargoMaterials": 0}]}}
            def run(args, **kwargs):
                save = Path(args[args.index("--save-path") + 1]); width = int(args[args.index("--width") + 1]); height = int(args[args.index("--height") + 1])
                capture = Path(args[args.index("--colony-reload-smoke" if "--colony-reload-smoke" in args else "--colony-smoke") + 1])
                if "--load" not in args:
                    save.write_text(json.dumps(base)); return mock.Mock(returncode=0, stdout="gpu_driver=vulkan systems=500 save=ok ", stderr="")
                payload = json.loads(save.read_text())
                outpost, fleet = payload["Galaxy"]["Colonies"][0], payload["Galaxy"]["Fleets"][0]
                reload_mode = "--colony-reload-smoke" in args
                fleet.update({"FreightTargetOutpostId": outpost["Id"], "FreightHomeColonyId": 20})
                save.write_text(json.dumps(payload)); capture.write_bytes(bmp(width, height))
                stdout = "gpu_driver=vulkan systems=500 save=ok \n" + self.proof("reload" if reload_mode else "dispatch")
                if not reload_mode:
                    review = capture.with_name(capture.stem + "-freight-review.bmp"); review.write_bytes(bmp(width, height))
                meta = {"path": str(capture), "width": width, "height": height}
                stdout += "\nnative_capture=" + json.dumps(meta, separators=(",", ":"))
                if not reload_mode:
                    stdout += "\nnative_capture=" + json.dumps({"path": str(review), "width": width, "height": height}, separators=(",", ":"))
                return mock.Mock(returncode=0, stdout=stdout, stderr="")
            with mock.patch("native_freight_runtime.subprocess.run", side_effect=run):
                result = validate_native_freight_export(package, {})
            self.assertTrue(result["nativeFreightPausedReload"])


if __name__ == "__main__": unittest.main()
