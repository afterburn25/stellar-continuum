import copy
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

import native_settlement_preparation_runtime as runtime
from test_native_first_survey_runtime import source_payload


def bmp(width, height):
    stride = (width * 3 + 3) & ~3
    pixels = (bytes(range(251)) * ((stride * height // 251) + 1))[:stride * height]
    return (b"BM" + struct.pack("<IHHI", 54 + len(pixels), 0, 0, 54) +
            struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0,
                        len(pixels), 2835, 2835, 0, 0) + pixels)


class NativeSettlementPreparationRuntimeTests(unittest.TestCase):
    def setup_source(self, root, fault=None):
        value = source_payload()
        value["Galaxy"]["Knowledge"][0]["SystemSurveys"][0].update(
            Level=2 if fault == "partial" else 3, Progress=1.0)
        value["Galaxy"]["PlanetaryBodies"][0].update(
            Id=1001, SystemId=2 if fault == "wrong_body" else 1, ParentBodyId=None)
        source = root / "survey.json"
        source.write_text(json.dumps(value), encoding="utf-8")
        return source

    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory() as temporary:
            root, package = Path(temporary), Path(temporary) / "package"
            package.mkdir()
            source = self.setup_source(root, fault)
            calls = []

            def launch(args, **unused):
                save = Path(args[args.index("--save-path") + 1])
                before = json.loads(save.read_text())
                after = copy.deepcopy(before)
                if fault == "changedpayload":
                    after["SimulationDays"] += 1
                save.write_text(json.dumps(after), encoding="utf-8")
                width = int(args[args.index("--width") + 1]); height = int(args[args.index("--height") + 1])
                capture = Path(args[-1]); capture.write_bytes(b"bad" if fault == "invalidbmp" else bmp(width, height))
                for suffix in ("-assessment", "-costs", "-shipyard"):
                    if fault != "missingartifacts" or suffix != "-costs":
                        capture.with_name(capture.stem + suffix + ".bmp").write_bytes(bmp(width, height))
                proof = {"system_id": 1, "body_id": 1001, "player_id": 0,
                         "read_only": True, "unsuitable_site": True,
                         "colony_cost_visible": True, "outpost_cost_visible": True,
                         "shipyard_opened": True, "review_closed": True,
                         "days": before["SimulationDays"]}
                text = "settlement_preparation=" + json.dumps(proof, separators=(",", ":"))
                if fault == "malformedproof": text = "settlement_preparation={bad}"
                if fault == "duplicateproof": text = text[:-1] + ',"days":0}'
                if fault == "wrongproof":
                    text = text.replace('"system_id":1', '"system_id":2')
                if fault == "booleanproof":
                    text = text.replace('"body_id":1001', '"body_id":true')
                if fault == "falseflag":
                    text = text.replace('"shipyard_opened":true', '"shipyard_opened":false')
                if fault == "badtime":
                    text = text.replace('"days":100.0', '"days":99.0')
                calls.append(args)
                return subprocess.CompletedProcess(args, 0, "gpu_driver=vulkan systems=500 save=ok \n" + text, "")

            with mock.patch.object(runtime.subprocess, "run", side_effect=launch):
                if fault:
                    with self.assertRaises(RuntimeError): runtime.validate_native_settlement_preparation_export(package, {}, source)
                else:
                    result = runtime.validate_native_settlement_preparation_export(package, {}, source)
                    self.assertTrue(result["nativeSettlementPreparation"])
                    self.assertEqual(len(calls), 2)
                    self.assertEqual(len(result["settlementPreparationCaptures"]), 8)
                    self.assertEqual(len(set(result["settlementPreparationCaptures"])), 8)
                    self.assertEqual(len(set(result["settlementPreparationSaveCaptures"])), 2)

    def test_complete_serial_preparation(self): self.exercise()
    def test_rejects_malformed_proof(self): self.exercise("malformedproof")
    def test_rejects_changed_payload(self): self.exercise("changedpayload")
    def test_rejects_missing_artifacts(self): self.exercise("missingartifacts")
    def test_rejects_invalid_bmp(self): self.exercise("invalidbmp")
    def test_rejects_duplicate_proof_keys(self): self.exercise("duplicateproof")
    def test_rejects_boolean_proof_identity(self): self.exercise("booleanproof")
    def test_rejects_wrong_proof_target(self): self.exercise("wrongproof")
    def test_rejects_false_required_flag(self): self.exercise("falseflag")
    def test_rejects_wrong_proof_time(self): self.exercise("badtime")
    def test_rejects_partial_source_survey(self): self.exercise("partial")
    def test_rejects_wrong_source_body(self): self.exercise("wrong_body")


if __name__ == "__main__":
    unittest.main()
