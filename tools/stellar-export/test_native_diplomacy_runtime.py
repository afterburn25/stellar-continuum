import copy
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

from native_diplomacy_runtime import validate_native_diplomacy_export, _notifications


def source():
    return {"FormatVersion": 17, "GameVersion": "0.1.7-alpha",
            "SavedAtUtc": "source", "SimulationDays": 0,
            "Control": {"FixtureOnly": True},
            "Galaxy": {"PlayerCivilizationId": 0,
                       "Systems": [{"Id": 0, "X": .123456789, "Y": -.987654321}],
                       "Civilizations": [{"Id": 0, "HomeSystemId": 0}, {"Id": 1}, {"Id": 2}],
                       "Knowledge": [{"CivilizationId": 0, "KnownCivilizationIds": [],
                                      "SystemSurveys": [{"SystemId": 0, "Level": 3}]}]},
            "Diplomacy": {"Contacts": [
                {"ObserverCivilizationId": 0, "ContactId": "known", "TargetCivilizationId": 1,
                "CommunicationAvailable": True}], "Relationships": [], "AccessPermissions": [],
                "Claims": [], "ClaimResponses": [], "Proposals": [],
                "Agreements": [{"AgreementId": 2, "CivilizationAId": 0,
                                "CivilizationBId": 1, "Type": 1, "Status": 0}],
                "RecentHistory": [], "NextClaimId": 1, "NextAgreementId": 3,
                "NextProposalId": 8, "NextEventId": 1}}


def bmp(path: Path, width: int, height: int, marker=1):
    stride = ((width * 24 + 31) // 32) * 4
    pixels = bytearray(stride * height)
    pixels[0] = marker
    size = 54 + len(pixels)
    header = b"BM" + struct.pack("<I", size) + b"\0\0\0\0" + struct.pack("<I", 54)
    header += struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0,
                          len(pixels), 0, 0, 0, 0)
    path.write_bytes(header + pixels)


# Shared Player17 fixture builders for the notification/logistics smoke
# validators. The third civilization gives author_diplomacy_fixture its
# required secondary contact target.
def fixture_payload():
    return {
        "FormatVersion": 17,
        "SimulationDays": 42.25,
        "Galaxy": {
            "Systems": list(range(20)),
            "PlayerCivilizationId": 0,
            "Civilizations": [
                {"Id": 0, "Name": "Human Commonwealth",
                 "SpeciesId": "terran_baseline", "HomeSystemId": 0},
                {"Id": 1, "Name": "Kesh Exchange",
                 "SpeciesId": "pelagic_high_pressure", "HomeSystemId": 2},
                {"Id": 2, "Name": "Veil Compact",
                 "SpeciesId": "cryogenic_hydrocarbon", "HomeSystemId": 4}],
            "Knowledge": [
                {"CivilizationId": 0, "KnownSystemIds": [0],
                 "KnownCivilizationIds": [],
                 "SystemSurveys": [
                     {"SystemId": 0, "Level": 3, "Progress": 1}]}],
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
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary) / "package"
            package.mkdir()
            configuration = package / "Configuration"
            configuration.mkdir()
            (configuration / "runtime-config.json").write_text(
                json.dumps({"gameVersion": "0.1.8-alpha"}))
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
                self.assertEqual(payload["GameVersion"],
                                 "0.1.8-alpha" if reload else "0.1.7-alpha")
                payload["GameVersion"] = "0.1.8-alpha"
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
                    if fault == "wrong_version": payload["GameVersion"] = "0.1.9-alpha"
                elif fault == "reload": payload["Galaxy"]["PlayerCivilizationId"] = 2
                elif fault == "reload_version": payload["GameVersion"] = "0.1.9-alpha"
                payload["SavedAtUtc"] = "saved-" + str(len(calls))
                save.write_text(json.dumps(payload))
                flag = "--diplomacy-reload-smoke" if reload else "--diplomacy-smoke"
                capture = Path(args[args.index(flag) + 1])
                width, height = int(args[args.index("--width") + 1]), int(args[args.index("--height") + 1])
                if fault != "capture":
                    bmp(capture, width, height, 1)
                    bmp(capture.with_name(capture.stem + "-unknown.bmp"), width, height, 2)
                    if fault != "map_capture":
                        marker = 1 if fault == "map_same_known" else 2 if fault == "map_same_unknown" else 3
                        bmp(capture.with_name(capture.stem + "-map.bmp"), width, height, marker)
                    bmp(capture.with_name(capture.stem + "-events.bmp"), width, height, 4)
                    bmp(capture.with_name(capture.stem + "-events-contact.bmp"), width, height,
                        4 if fault == "events_duplicate" else 5)
                state = {"mode": "paused_reload" if reload else "progress",
                         "selection_changed": True, "unknown_redacted": True,
                         "portrait_visible": fault != "portrait", "accepted": not reload,
                         "proposal_id": proposal["ProposalId"], "target_id": 1, "paused": True}
                if fault == "spoof": state["accepted"] = True
                if fault == "numeric_flag": state["paused"] = 1
                if fault == "wrong_target": state["target_id"] = 2
                territory = {"valid": True, "regions": 1, "claims": 1, "fill_runs": 1,
                             "contour_points": 4, "fog_texels": 32, "unexplored": 1,
                             "fill_images": 1, "contour_segments": 1, "claim_segments": 1,
                             "fog_images": 1, "cached_image_bytes": 4096}
                if fault == "territory_missing": territory["claims"] = 0
                if fault == "territory_boolean": territory["regions"] = True
                if fault == "territory_extra": territory["extra"] = 1
                if fault == "territory_budget": territory["cached_image_bytes"] = 16 * 1024 * 1024 + 1
                if fault == "territory_duplicate":
                    territory_encoded = '{"valid":true,"valid":true}'
                else:
                    territory_encoded = json.dumps(territory)
                encoded = "{malformed" if fault == "malformed" else json.dumps(state)
                notifications = {"mode": "paused_reload" if reload else "progress",
                                 "opened": True, "closed": True,
                                 "items": 0 if reload else 2, "unread_before": 0 if reload else 2,
                                 "unread_after": 0, "focused_target": -1 if reload else 1,
                                 "canonical_unchanged": True, "paused": True}
                if fault == "notifications_missing": notification_line = ""
                elif fault == "notifications_bad":
                    notifications["opened"] = False
                    notification_line = "\nnotifications=" + json.dumps(notifications)
                else: notification_line = "\nnotifications=" + json.dumps(notifications)
                stdout = "gpu_driver=vulkan systems=1 save=ok territory=" + territory_encoded + notification_line + "\ndiplomacy=" + encoded
                calls.append(args)
                return subprocess.CompletedProcess(args, 0, stdout, "")

            with mock.patch("native_diplomacy_runtime._source_row", return_value=copy.deepcopy(source())), \
                 mock.patch("native_diplomacy_runtime.subprocess.run", side_effect=run):
                result = validate_native_diplomacy_export(package, {}, fixture)
            self.assertEqual(len(calls), 2)
            self.assertTrue(result["nativeDiplomacy"])
            self.assertTrue(result["nativeDiplomacyPausedReload"])
            self.assertTrue(result["nativeTerritory"])
            self.assertEqual(result["territoryChecks"]["reload"]["claim_segments"], 1)
            self.assertEqual(len(result["diplomacyCaptures"]), 6)
            self.assertEqual(len(result["notificationCaptures"]), 4)

    def test_accepts_fixture_version_upgrade_and_paused_reload(self): self.exercise()
    def test_rejects_wrong_saved_game_version(self):
        with self.assertRaisesRegex(RuntimeError, "game version"): self.exercise("wrong_version")
    def test_rejects_game_version_change_on_paused_reload(self):
        with self.assertRaisesRegex(RuntimeError, "paused reload"): self.exercise("reload_version")
    def test_rejects_missing_runtime_version_config(self):
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary) / "package"
            package.mkdir()
            fixture = Path(temporary) / "fixture.json"
            fixture.write_text(json.dumps({"Rows": [{"Name": "valid-current17",
                                                       "InputJson": json.dumps(source())}]}))
            with self.assertRaisesRegex(RuntimeError, "configuration"):
                validate_native_diplomacy_export(package, {}, fixture)
    def test_rejects_malformed_runtime_version_config(self):
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary) / "package"
            configuration = package / "Configuration"
            configuration.mkdir(parents=True)
            (configuration / "runtime-config.json").write_text("{")
            fixture = Path(temporary) / "fixture.json"
            fixture.write_text(json.dumps({"Rows": [{"Name": "valid-current17",
                                                       "InputJson": json.dumps(source())}]}))
            with self.assertRaisesRegex(RuntimeError, "configuration"):
                validate_native_diplomacy_export(package, {}, fixture)
    def test_rejects_missing_runtime_game_version(self):
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary) / "package"
            configuration = package / "Configuration"
            configuration.mkdir(parents=True)
            (configuration / "runtime-config.json").write_text(json.dumps({"gameVersion": " "}))
            fixture = Path(temporary) / "fixture.json"
            fixture.write_text(json.dumps({"Rows": [{"Name": "valid-current17",
                                                       "InputJson": json.dumps(source())}]}))
            with self.assertRaisesRegex(RuntimeError, "gameVersion"):
                validate_native_diplomacy_export(package, {}, fixture)
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
    def test_rejects_missing_notifications(self):
        with self.assertRaisesRegex(RuntimeError, "notification"):
            self.exercise("notifications_missing")
    def test_rejects_invalid_notifications(self):
        with self.assertRaisesRegex(RuntimeError, "notification"):
            self.exercise("notifications_bad")
    def test_rejects_duplicate_notification_capture(self):
        with self.assertRaisesRegex(RuntimeError, "notification"):
            self.exercise("events_duplicate")
    def test_rejects_missing_regional_map_capture(self):
        with self.assertRaises(RuntimeError): self.exercise("map_capture")
    def test_rejects_map_capture_identical_to_known_view(self):
        with self.assertRaises(RuntimeError): self.exercise("map_same_known")
    def test_rejects_map_capture_identical_to_unknown_view(self):
        with self.assertRaises(RuntimeError): self.exercise("map_same_unknown")
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
    def test_rejects_missing_territory_claim(self):
        with self.assertRaises(RuntimeError): self.exercise("territory_missing")
    def test_rejects_boolean_territory_counter(self):
        with self.assertRaises(RuntimeError): self.exercise("territory_boolean")
    def test_rejects_extra_territory_field(self):
        with self.assertRaises(RuntimeError): self.exercise("territory_extra")
    def test_rejects_oversized_territory_cache(self):
        with self.assertRaises(RuntimeError): self.exercise("territory_budget")
    def test_rejects_duplicate_territory_field(self):
        with self.assertRaises(RuntimeError): self.exercise("territory_duplicate")



class NotificationProofTests(unittest.TestCase):
    def test_rejects_false_or_mistyped_claims_and_wrong_target(self):
        valid = {"mode": "progress", "opened": True, "closed": True, "items": 2,
                 "unread_before": 2, "unread_after": 0, "focused_target": 7,
                 "canonical_unchanged": True, "paused": True}
        self.assertEqual(_notifications("notifications=" + json.dumps(valid), "progress", 7), valid)
        for key, value in (("canonical_unchanged", False), ("paused", 1), ("items", True),
                           ("focused_target", 8), ("unread_after", 2), ("extra", True)):
            with self.subTest(field=key):
                bad = dict(valid, **{key: value})
                with self.assertRaises(RuntimeError):
                    _notifications("notifications=" + json.dumps(bad), "progress", 7)

    def test_rejects_retained_alerts_on_reload(self):
        proof = {"mode": "paused_reload", "opened": True, "closed": True, "items": 2,
                 "unread_before": 2, "unread_after": 0, "focused_target": -1,
                 "canonical_unchanged": True, "paused": True}
        with self.assertRaises(RuntimeError):
            _notifications("notifications=" + json.dumps(proof), "paused_reload", 7)

    def test_rejects_malformed_duplicate_or_nonobject_proof(self):
        for value in ('{bad}', '{"opened":true,"opened":false}', '[]', 'null',
                      '{}\nnotifications={}'):
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                _notifications("notifications=" + value, "progress", 7)

if __name__ == "__main__": unittest.main()
