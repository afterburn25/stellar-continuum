import copy
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

from native_diplomacy_runtime import validate_native_diplomacy_export


def source():
    return {"FormatVersion": 17, "SavedAtUtc": "source", "SimulationDays": 0,
            "Control": {"FixtureOnly": True},
            "Galaxy": {"PlayerCivilizationId": 0,
                       "Systems": [{"Id": 0, "X": .123456789, "Y": -.987654321}],
                       "Civilizations": [{"Id": 0}, {"Id": 1}, {"Id": 2}]},
            "Diplomacy": {"Contacts": [
                {"ObserverCivilizationId": 0, "ContactId": "known", "TargetCivilizationId": 1,
                "CommunicationAvailable": True}], "Relationships": [], "AccessPermissions": [],
                "Claims": [], "ClaimResponses": [], "Proposals": [],
                "Agreements": [{"AgreementId": 2, "CivilizationAId": 0,
                                "CivilizationBId": 1, "Type": 1, "Status": 0}],
                "RecentHistory": [], "NextClaimId": 1, "NextAgreementId": 3,
                "NextProposalId": 8, "NextEventId": 1}}


def bmp(path: Path, width: int, height: int):
    stride = ((width * 24 + 31) // 32) * 4
    pixels = bytearray(stride * height)
    pixels[0] = 1
    size = 54 + len(pixels)
    header = b"BM" + struct.pack("<I", size) + b"\0\0\0\0" + struct.pack("<I", 54)
    header += struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0,
                          len(pixels), 0, 0, 0, 0)
    path.write_bytes(header + pixels)


class NativeDiplomacyRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary) / "package"
            package.mkdir()
            fixture = Path(temporary) / "fixture.json"
            fixture.write_text(json.dumps({"Rows": []}))
            calls = []

            def run(args, *, cwd, env, **unused):
                self.assertNotEqual(cwd, package)
                self.assertIn("--load", args)
                save = Path(args[args.index("--save-path") + 1])
                payload = json.loads(save.read_text())
                reload = "--diplomacy-reload-smoke" in args
                proposal = payload["Diplomacy"]["Proposals"][-1]
                if not reload:
                    payload.pop("Control", None)
                    system = payload["Galaxy"]["Systems"][0]
                    for key in ("X", "Y"):
                        system[key] = struct.unpack("<f", struct.pack("<f", system[key]))[0]
                    proposal["Status"] = 1
                    proposal["ResolvedAtTick"] = 23
                    payload["Diplomacy"]["Agreements"].append(
                        {"AgreementId": 3, "CivilizationAId": 0, "CivilizationBId": 1,
                         "Type": 4, "Status": 0})
                    payload["Diplomacy"]["RecentHistory"].extend(({"EventId": 1}, {"EventId": 2}))
                    payload["Diplomacy"]["NextAgreementId"] += 1
                    payload["Diplomacy"]["NextEventId"] += 2
                    if fault == "bad_proposal": proposal["Status"] = 0
                    if fault == "wrong_owner": proposal["ProposerCivilizationId"] = 2
                    if fault == "unrelated": payload["Galaxy"]["Systems"].append({"Id": 9})
                    if fault == "removed_contact": payload["Diplomacy"]["Contacts"].pop()
                    if fault == "changed_agreement": payload["Diplomacy"]["Agreements"][0]["Status"] = 1
                    if fault == "coordinate": payload["Galaxy"]["Systems"][0]["X"] += 1
                    if fault == "proposal_terms": proposal["Summary"] = "tampered"
                    if fault == "duplicate_proposal": payload["Diplomacy"]["Proposals"].append(copy.deepcopy(proposal))
                    if fault == "diplomacy_root": payload["Diplomacy"]["Unrelated"] = True
                elif fault == "reload": payload["Galaxy"]["PlayerCivilizationId"] = 2
                payload["SavedAtUtc"] = "saved-" + str(len(calls))
                save.write_text(json.dumps(payload))
                flag = "--diplomacy-reload-smoke" if reload else "--diplomacy-smoke"
                capture = Path(args[args.index(flag) + 1])
                width, height = int(args[args.index("--width") + 1]), int(args[args.index("--height") + 1])
                if fault != "capture":
                    bmp(capture, width, height)
                    bmp(capture.with_name(capture.stem + "-unknown.bmp"), width, height)
                state = {"mode": "paused_reload" if reload else "progress",
                         "selection_changed": True, "unknown_redacted": True,
                         "portrait_visible": fault != "portrait", "accepted": not reload,
                         "proposal_id": proposal["ProposalId"], "target_id": 1, "paused": True}
                if fault == "spoof": state["accepted"] = True
                if fault == "numeric_flag": state["paused"] = 1
                if fault == "wrong_target": state["target_id"] = 2
                encoded = "{malformed" if fault == "malformed" else json.dumps(state)
                stdout = "gpu_driver=vulkan systems=1 save=ok  diplomacy=" + encoded
                calls.append(args)
                return subprocess.CompletedProcess(args, 0, stdout, "")

            with mock.patch("native_diplomacy_runtime._source_row", return_value=copy.deepcopy(source())), \
                 mock.patch("native_diplomacy_runtime.subprocess.run", side_effect=run):
                result = validate_native_diplomacy_export(package, {}, fixture)
            self.assertEqual(len(calls), 2)
            self.assertTrue(result["nativeDiplomacy"])
            self.assertTrue(result["nativeDiplomacyPausedReload"])
            self.assertEqual(len(result["diplomacyCaptures"]), 4)

    def test_progress_and_paused_reload(self): self.exercise()
    def test_rejects_spoofed_reload_claim(self):
        with self.assertRaises(RuntimeError): self.exercise("spoof")
    def test_rejects_malformed_diagnostic(self):
        with self.assertRaises(RuntimeError): self.exercise("malformed")
    def test_rejects_wrong_selected_counterpart(self):
        with self.assertRaises(RuntimeError): self.exercise("wrong_target")
    def test_rejects_numeric_boolean_claim(self):
        with self.assertRaises(RuntimeError): self.exercise("numeric_flag")
    def test_rejects_missing_portrait(self):
        with self.assertRaises(RuntimeError): self.exercise("portrait")
    def test_rejects_missing_capture(self):
        with self.assertRaises(RuntimeError): self.exercise("capture")
    def test_rejects_unresolved_proposal(self):
        with self.assertRaises(RuntimeError): self.exercise("bad_proposal")
    def test_rejects_wrong_proposal_owner(self):
        with self.assertRaises(RuntimeError): self.exercise("wrong_owner")
    def test_rejects_unrelated_progress_mutation(self):
        with self.assertRaises(RuntimeError): self.exercise("unrelated")
    def test_rejects_removed_unrelated_contact(self):
        with self.assertRaises(RuntimeError): self.exercise("removed_contact")
    def test_rejects_changed_preexisting_agreement(self):
        with self.assertRaises(RuntimeError): self.exercise("changed_agreement")
    def test_accepts_player17_single_precision_coordinate_canonicalization(self):
        self.exercise()
    def test_rejects_genuine_coordinate_change_after_canonicalization(self):
        with self.assertRaises(RuntimeError): self.exercise("coordinate")
    def test_rejects_authored_proposal_term_change(self):
        with self.assertRaises(RuntimeError): self.exercise("proposal_terms")
    def test_rejects_duplicate_proposal_id(self):
        with self.assertRaises(RuntimeError): self.exercise("duplicate_proposal")
    def test_rejects_unrelated_diplomacy_root_change(self):
        with self.assertRaises(RuntimeError): self.exercise("diplomacy_root")
    def test_rejects_changed_reload_payload(self):
        with self.assertRaises(RuntimeError): self.exercise("reload")


if __name__ == "__main__": unittest.main()
