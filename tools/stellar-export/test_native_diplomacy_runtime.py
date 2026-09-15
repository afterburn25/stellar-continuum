import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from native_diplomacy_runtime import (
    _author_diplomacy_source, validate_native_diplomacy_export)


def fixture_payload():
    return {
        "FormatVersion": 17,
        "SimulationDays": 42.25,
        "Galaxy": {
            "Systems": list(range(20)),
            "PlayerCivilizationId": 0,
            "Civilizations": [
                {"Id": 0, "Name": "Human Commonwealth",
                 "SpeciesId": "terran_baseline"},
                {"Id": 1, "Name": "Kesh Exchange",
                 "SpeciesId": "pelagic_high_pressure"}],
        },
        "Diplomacy": {
            "Contacts": [
                {"ObserverCivilizationId": 0,
                 "ContactId": "populated-player-counterpart",
                 "TargetCivilizationId": 1, "FirstObservedTick": 10,
                 "LastObservedTick": 10, "LastObservedSystemId": 0,
                 "Awareness": 5, "Condition": 0,
                 "CommunicationAvailable": True, "Confidence": 0.9}],
            "Relationships": [
                {"CivilizationAId": 0, "CivilizationBId": 1,
                 "PoliticalState": 1, "Trust": 0.2, "Hostility": 0.0,
                 "Fear": 0.1, "Respect": 0.3, "Cooperation": 0.4,
                 "Grievances": []}],
            "AccessPermissions": [],
            "Claims": [],
            "ClaimResponses": [],
            "Agreements": [
                {"AgreementId": 1, "CivilizationAId": 0,
                 "CivilizationBId": 1, "Type": 1, "Status": 0,
                 "StartedAtTick": 13, "EndedAtTick": None,
                 "ExternalTermsReference": None}],
            "Proposals": [
                {"ProposalId": 1, "ProposerCivilizationId": 0,
                 "RecipientCivilizationId": 1, "Kind": 0,
                 "AgreementType": 1, "Status": 1, "CreatedAtTick": 12,
                 "ResolvedAtTick": 13,
                 "Summary": "Retained non-aggression proposal",
                 "ExternalTermsReference": None}],
            "RecentHistory": [
                {"EventId": 1, "Tick": 10, "Kind": 2,
                 "PrimaryCivilizationId": 0, "SecondaryCivilizationId": 1,
                 "SystemId": 0, "Summary": "Channel opened.",
                 "KnownToCivilizationIds": [0]}],
            "NextClaimId": 1, "NextAgreementId": 2,
            "NextProposalId": 2, "NextEventId": 2,
        },
    }


FIXTURE = {"Rows": [{"Name": "valid-current17",
                     "InputJson": json.dumps(fixture_payload(),
                                             separators=(",", ":"))}]}


def diagnostic():
    return {"contacts": 2, "identified": 1, "unidentified": 1, "redacted": 1,
            "channels": 1, "agreements": 1, "history": 6, "proposals": 2,
            "selected_civ": 1, "last_observed_system": 0, "portrait": 1,
            "command_accepted": 1, "proposal_id": 3, "pending_after": 1,
            "incoming_pending": 1}


def payload(proposal_id):
    record = fixture_payload()
    record["Diplomacy"]["Proposals"].extend([
        {"ProposalId": 2, "ProposerCivilizationId": 1,
         "RecipientCivilizationId": 0, "Kind": 1, "AgreementType": None,
         "Status": 0, "CreatedAtTick": 42250, "ResolvedAtTick": None,
         "Summary": "Counterpart transit access petition",
         "ExternalTermsReference": None},
        {"ProposalId": proposal_id, "ProposerCivilizationId": 0,
         "RecipientCivilizationId": 1, "Kind": 1, "AgreementType": None,
         "Status": 0, "CreatedAtTick": 42250, "ResolvedAtTick": None,
         "Summary": "Request for transit access.",
         "ExternalTermsReference": None}])
    record["Diplomacy"]["Contacts"].append(
        {"ObserverCivilizationId": 0, "ContactId": "unresolved-signal",
         "TargetCivilizationId": None, "FirstObservedTick": 42246,
         "LastObservedTick": 42250, "LastObservedSystemId": None,
         "Awareness": 1, "Condition": 0, "CommunicationAvailable": False,
         "Confidence": 0.4})
    return record


class NativeDiplomacyRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory(prefix="stellar-diplomacy-test-") as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir()
            fixture = root / "fixture.json"
            fixture.write_text(json.dumps(FIXTURE), encoding="utf-8")
            calls = []

            def launch(args, *, cwd, **unused):
                shot = Path(args[args.index("--diplomacy-smoke") + 1])
                save = Path(args[args.index("--save-path") + 1])
                self.assertEqual(cwd, shot.parent)
                self.assertEqual(cwd, save.parent)
                calls.append(args)
                state = diagnostic()
                if fault == "redacted": state["redacted"] = 0
                if fault == "channel": state["channels"] = 0
                if fault == "portrait": state["portrait"] = 0
                if fault == "command": state["command_accepted"] = 0
                if fault == "incoming": state["incoming_pending"] = 0
                if fault == "counterpart": state["selected_civ"] = 2
                record = payload(state["proposal_id"])
                if fault == "save": record["Galaxy"]["Systems"] = [1]
                if fault == "proposal":
                    record["Diplomacy"]["Proposals"][-1]["Kind"] = 4
                if fault == "unidentified_leak":
                    record["Diplomacy"]["Contacts"][-1]["TargetCivilizationId"] = 2
                if len(calls) == 2 and fault == "reload":
                    record["Diplomacy"]["Proposals"][-1]["Status"] = 2
                save.write_text(json.dumps(record), encoding="utf-8")
                varied = bytes(range(256)) if fault != "blank" else bytes(200)
                shot.write_bytes(b"BM" + b"\0" * 52 + varied * 40)
                proposals_path = shot.with_name(
                    shot.stem + "-proposals" + shot.suffix)
                if fault != "capture":
                    other = (varied if fault == "same_capture"
                             else bytes(reversed(range(256))))
                    proposals_path.write_bytes(b"BM" + b"\0" * 52 + other * 40)
                return subprocess.CompletedProcess(
                    args, 0,
                    f"gpu_driver=vulkan systems=20 image_uploads=5 save=ok "
                    f"diplomacy={json.dumps(state, separators=(',', ':'))}", "")

            with mock.patch("native_diplomacy_runtime.subprocess.run",
                            side_effect=launch):
                result = validate_native_diplomacy_export(package, {}, fixture)
            self.assertEqual(len(calls), 2)
            self.assertIn("--load", calls[0])
            self.assertIn("--load", calls[1])
            self.assertEqual(len(result["diplomacyCaptures"]), 4)
            self.assertTrue(result["nativeDiplomacyObserverRedaction"])
            self.assertTrue(result["nativeDiplomacyCommand"])

    def test_complete_actual_contract(self): self.exercise()
    def test_identity_leak_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("redacted")
    def test_missing_channel_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("channel")
    def test_missing_portrait_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("portrait")
    def test_rejected_command_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("command")
    def test_missing_incoming_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("incoming")
    def test_wrong_counterpart_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("counterpart")
    def test_damaged_payload_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("save")
    def test_foreign_proposal_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("proposal")
    def test_unidentified_save_leak_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("unidentified_leak")
    def test_reload_resolution_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("reload")
    def test_missing_proposals_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("capture")
    def test_identical_captures_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("same_capture")
    def test_blank_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("blank")


class NativeDiplomacyAuthoringTests(unittest.TestCase):
    def test_authors_unidentified_and_petition(self):
        authored, counterpart = _author_diplomacy_source(fixture_payload())
        self.assertEqual(counterpart, 1)
        diplomacy = authored["Diplomacy"]
        self.assertEqual(len(diplomacy["Contacts"]), 2)
        added = diplomacy["Contacts"][-1]
        self.assertIsNone(added["TargetCivilizationId"])
        self.assertEqual(added["Awareness"], 1)
        self.assertEqual(len(diplomacy["Proposals"]), 2)
        petition = diplomacy["Proposals"][-1]
        self.assertEqual(petition["ProposalId"], 2)
        self.assertEqual(petition["Status"], 0)
        self.assertEqual(petition["CreatedAtTick"], 42250)
        self.assertEqual(diplomacy["NextProposalId"], 3)

    def test_missing_channel_rejected(self):
        broken = fixture_payload()
        broken["Diplomacy"]["Contacts"] = []
        with self.assertRaises(RuntimeError):
            _author_diplomacy_source(broken)


if __name__ == "__main__":
    unittest.main()
