"""Player17 diplomacy workspace smoke validation for the native client."""
from __future__ import annotations

import copy
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

from native_fleet_runtime import _source_row
from native_galaxy_runtime import _bmp
from native_client_runtime import _validate_capture


_TERRITORY_FIELDS = {"valid", "regions", "claims", "fill_runs", "contour_points",
                     "fog_texels", "unexplored", "fill_images", "contour_segments",
                     "claim_segments", "fog_images", "cached_image_bytes"}


def _normalized(payload: dict) -> dict:
    result = copy.deepcopy(payload)
    result.pop("SavedAtUtc", None)
    return result


def _canonical_progress_baseline(payload: dict) -> dict:
    """Mirror Player17's current fixture-to-native serialization boundary."""
    expected = copy.deepcopy(payload)
    # player_campaign_json_tests.cpp establishes that Control is fixture-only
    # and native serialization round-trips X/Y through single precision.
    expected.pop("Control", None)

    def visit(value):
        if isinstance(value, list):
            for item in value:
                visit(item)
        elif isinstance(value, dict):
            for key, item in value.items():
                if key in ("X", "Y") and type(item) in (int, float):
                    value[key] = struct.unpack("<f", struct.pack("<f", item))[0]
                else:
                    visit(item)
    visit(expected)
    return expected


def _preserve_fixture(validate):
    def guarded(folder: Path, env: dict[str, str], fixture: Path):
        if not fixture.is_file():
            raise RuntimeError("Native diplomacy Player17 fixture is missing")
        source_hash = hashlib.sha256(fixture.read_bytes()).digest()
        try:
            return validate(folder, env, fixture)
        finally:
            if hashlib.sha256(fixture.read_bytes()).digest() != source_hash:
                raise RuntimeError("Native diplomacy modified its read-only Player17 fixture")
    return guarded


def author_diplomacy_fixture(source: dict) -> tuple[dict, int, int]:
    """Create an isolated, legal contact/proposal fixture from Player17 data."""
    authored = copy.deepcopy(source)
    galaxy = authored.get("Galaxy")
    diplomacy = authored.get("Diplomacy")
    if authored.get("FormatVersion") != 17 or not isinstance(galaxy, dict) or not isinstance(diplomacy, dict):
        raise RuntimeError("Native diplomacy needs a current Player17 campaign")
    player = galaxy.get("PlayerCivilizationId")
    if type(player) is not int:
        raise RuntimeError("Native diplomacy fixture lacks a player civilization")
    contacts = diplomacy.get("Contacts")
    proposals = diplomacy.get("Proposals")
    if not isinstance(contacts, list) or not isinstance(proposals, list):
        raise RuntimeError("Native diplomacy fixture lacks contacts or proposals")
    known = [row for row in contacts if row.get("ObserverCivilizationId") == player and
             type(row.get("TargetCivilizationId")) is int and row.get("CommunicationAvailable") is True]
    if len(known) != 1:
        raise RuntimeError("Native diplomacy fixture needs one known player communication channel")
    partner = known[0]["TargetCivilizationId"]
    civilizations = {row.get("Id") for row in galaxy.get("Civilizations", []) if isinstance(row, dict)}
    alternatives = [identifier for identifier in civilizations if type(identifier) is int and identifier not in (player, partner)]
    if not alternatives:
        raise RuntimeError("Native diplomacy fixture lacks a second civilization")
    second = min(alternatives)
    existing_ids = {row.get("ContactId") for row in contacts if isinstance(row, dict)}
    if {"native-smoke-unidentified", "native-smoke-second"} & existing_ids:
        raise RuntimeError("Native diplomacy fixture reserves its authored contact ids")
    contacts.extend((
        {"ObserverCivilizationId": player, "ContactId": "native-smoke-unidentified",
         "TargetCivilizationId": None, "FirstObservedTick": 20, "LastObservedTick": 20,
         "LastObservedSystemId": None, "Awareness": 1, "Condition": 0,
         "CommunicationAvailable": False, "Confidence": .35},
        {"ObserverCivilizationId": player, "ContactId": "native-smoke-second",
         "TargetCivilizationId": second, "FirstObservedTick": 21, "LastObservedTick": 21,
         "LastObservedSystemId": None, "Awareness": 2, "Condition": 0,
         "CommunicationAvailable": False, "Confidence": .7},
    ))
    # Match DiplomaticState::snapshot's deterministic observer/contact ordering.
    # Sort only the isolated authored input; reload comparisons remain exact.
    contacts.sort(key=lambda row: (row["ObserverCivilizationId"], row["ContactId"]))
    proposal_id = diplomacy.get("NextProposalId")
    if type(proposal_id) is not int or proposal_id < 1 or any(row.get("ProposalId") == proposal_id for row in proposals):
        raise RuntimeError("Native diplomacy fixture has no usable next proposal id")
    proposals.append({"ProposalId": proposal_id, "ProposerCivilizationId": partner,
                      "RecipientCivilizationId": player, "Kind": 0, "AgreementType": 4,
                      "Status": 0, "CreatedAtTick": 22, "ResolvedAtTick": None,
                      "Summary": "Native smoke research exchange", "ExternalTermsReference": None})
    diplomacy["NextProposalId"] = proposal_id + 1
    homes = [row.get("HomeSystemId") for row in galaxy.get("Civilizations", [])
             if isinstance(row, dict) and row.get("Id") == player]
    if len(homes) != 1 or type(homes[0]) is not int:
        raise RuntimeError("Native diplomacy fixture lacks a player home system")
    home = homes[0]
    knowledge_rows = [row for row in galaxy.get("Knowledge", []) if isinstance(row, dict) and
                      row.get("CivilizationId") == player]
    if len(knowledge_rows) != 1:
        raise RuntimeError("Native diplomacy fixture lacks player knowledge")
    knowledge = knowledge_rows[0]
    surveys = knowledge.get("SystemSurveys")
    if not isinstance(surveys, list) or not any(isinstance(row, dict) and row.get("SystemId") == home and
                                                row.get("Level") == 3 for row in surveys):
        raise RuntimeError("Native diplomacy fixture needs a fully surveyed player home")
    known_civilizations = knowledge.get("KnownCivilizationIds")
    if not isinstance(known_civilizations, list) or any(type(value) is not int for value in known_civilizations):
        raise RuntimeError("Native diplomacy fixture has malformed known civilizations")
    if partner not in known_civilizations:
        known_civilizations.append(partner)
    claims = diplomacy.get("Claims")
    claim_id = diplomacy.get("NextClaimId")
    if (not isinstance(claims, list) or type(claim_id) is not int or claim_id < 1 or
            any(isinstance(row, dict) and row.get("ClaimId") == claim_id for row in claims)):
        raise RuntimeError("Native diplomacy fixture has no usable next claim id")
    claims.append({"ClaimId": claim_id, "ClaimantCivilizationId": partner, "SystemId": home,
                   "AssertedAtTick": 20, "Active": True,
                   "KnownToCivilizationIds": [player, partner]})
    diplomacy["NextClaimId"] = claim_id + 1
    return authored, proposal_id, partner


def _state(stdout: str, mode: str) -> dict:
    match = re.search(r"(?:^|\s)diplomacy=(\{[^\n]+\})\s*$", stdout)
    if not match:
        raise RuntimeError("Native diplomacy did not report workspace evidence at the end of stdout")
    try:
        state = json.loads(match.group(1))
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native diplomacy diagnostic is malformed") from error
    expected_flags = {"selection_changed": True, "unknown_redacted": True,
                      "portrait_visible": True, "paused": True,
                      "accepted": mode == "progress"}
    if state.get("mode") != mode or type(state.get("mode")) is not str or \
            any(type(state.get(key)) is not bool or state[key] != value
                for key, value in expected_flags.items()):
        raise RuntimeError("Native diplomacy diagnostic did not prove its required UI behavior")
    for key in ("proposal_id", "target_id"):
        if type(state.get(key)) is not int or state[key] < 0:
            raise RuntimeError(f"Native diplomacy diagnostic has invalid {key}")
    return state


def _without_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate object field")
        result[key] = value
    return result


def _territory(stdout: str) -> dict:
    rows = re.findall(r"(?:^|\s)territory=(\{[^{}\n]*\})(?=\s|$)", stdout)
    if len(rows) != 1:
        raise RuntimeError("Native territory did not report exactly one diagnostic")
    try:
        state = json.loads(rows[0], object_pairs_hook=_without_duplicate_keys)
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native territory diagnostic is malformed") from error
    if not isinstance(state, dict) or set(state) != _TERRITORY_FIELDS or state.get("valid") is not True:
        raise RuntimeError(f"Native territory diagnostic has an unexpected schema: {state!r}")
    for field in _TERRITORY_FIELDS - {"valid"}:
        value = state.get(field)
        if type(value) is not int or value < 0:
            raise RuntimeError(f"Native territory diagnostic has invalid {field}: {state!r}")
    if state["cached_image_bytes"] > 16 * 1024 * 1024:
        raise RuntimeError(f"Native territory diagnostic exceeded its image cache budget: {state!r}")
    if any(state[field] < 1 for field in ("regions", "claims", "fill_images", "fog_images",
                                          "contour_segments", "claim_segments")):
        raise RuntimeError(f"Native territory diagnostic did not prove its rendered claim overlay: {state!r}")
    return state

def _notifications(stdout: str, mode: str, target_id: int) -> dict:
    rows = re.findall(r"(?m)^notifications=(\{[^\n]+\})$", stdout)
    if len(rows) != 1:
        raise RuntimeError("Native diplomacy did not report exactly one notification diagnostic")
    try:
        state = json.loads(rows[0], object_pairs_hook=_without_duplicate_keys)
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native diplomacy notification diagnostic is malformed") from error
    expected = {"mode": mode, "opened": True, "closed": True, "items": 2 if mode == "progress" else 0,
                "unread_before": 2 if mode == "progress" else 0, "unread_after": 0,
                "focused_target": target_id if mode == "progress" else -1,
                "canonical_unchanged": True, "paused": True}
    if (not isinstance(state, dict) or set(state) != set(expected) or
            any(type(state[k]) is not type(v) or state[k] != v for k, v in expected.items())):
        raise RuntimeError("Native diplomacy notification diagnostic is invalid")
    return state


def _verify_progress(before: dict, after: dict, proposal_id: int, target_id: int) -> None:
    if _normalized(before) == _normalized(after):
        raise RuntimeError("Native diplomacy progress did not save the accepted proposal")
    changed = {key for key in set(before) | set(after) if before.get(key) != after.get(key)}
    if changed - {"Diplomacy", "SavedAtUtc"}:
        raise RuntimeError("Native diplomacy changed unrelated Player17 state")
    before_diplomacy = before.get("Diplomacy", {})
    diplomacy = after.get("Diplomacy", {})
    for key in ("Contacts", "Relationships", "AccessPermissions", "Claims", "ClaimResponses"):
        if diplomacy.get(key) != before_diplomacy.get(key):
            raise RuntimeError(f"Native diplomacy changed unrelated {key}")
    before_proposals = before_diplomacy.get("Proposals", [])
    after_proposals = diplomacy.get("Proposals", [])
    before_by_id = {row.get("ProposalId"): row for row in before_proposals}
    after_by_id = {row.get("ProposalId"): row for row in after_proposals}
    if len(before_by_id) != len(before_proposals) or \
            len(after_by_id) != len(after_proposals) or \
            set(after_by_id) != set(before_by_id) or \
            any(after_by_id[identifier] != row for identifier, row in before_by_id.items()
                if identifier != proposal_id):
        raise RuntimeError("Native diplomacy changed preexisting proposals")
    authored_before = before_by_id.get(proposal_id)
    authored_after = after_by_id.get(proposal_id)
    mutable_proposal_fields = {"Status", "ResolvedAtTick"}
    if not isinstance(authored_before, dict) or not isinstance(authored_after, dict) or \
            set(authored_after) != set(authored_before) or \
            any(authored_after.get(key) != value for key, value in authored_before.items()
                if key not in mutable_proposal_fields):
        raise RuntimeError("Native diplomacy changed authored proposal terms")
    before_agreements = before_diplomacy.get("Agreements", [])
    after_agreements = diplomacy.get("Agreements", [])
    if after_agreements[:len(before_agreements)] != before_agreements or \
            len(after_agreements) != len(before_agreements) + 1:
        raise RuntimeError("Native diplomacy changed preexisting agreements")
    before_history = before_diplomacy.get("RecentHistory", [])
    after_history = diplomacy.get("RecentHistory", [])
    if after_history[:len(before_history)] != before_history or \
            len(after_history) != len(before_history) + 2:
        raise RuntimeError("Native diplomacy did not record exactly its acceptance events")
    for key, increment in (("NextClaimId", 0), ("NextProposalId", 0),
                           ("NextAgreementId", 1), ("NextEventId", 2)):
        if type(before_diplomacy.get(key)) is not int or \
                diplomacy.get(key) != before_diplomacy[key] + increment:
            raise RuntimeError(f"Native diplomacy has invalid {key} after acceptance")
    mutable_diplomacy_fields = {"Proposals", "Agreements", "RecentHistory",
                                "NextAgreementId", "NextEventId"}
    if any(diplomacy.get(key) != value for key, value in before_diplomacy.items()
           if key not in mutable_diplomacy_fields) or \
            any(key not in before_diplomacy and key not in mutable_diplomacy_fields
                for key in diplomacy):
        raise RuntimeError("Native diplomacy changed an unrelated root field")
    proposals = [row for row in diplomacy.get("Proposals", []) if row.get("ProposalId") == proposal_id]
    if len(proposals) != 1 or type(proposals[0].get("ProposalId")) is not int or \
            proposals[0].get("ProposerCivilizationId") != target_id or \
            proposals[0].get("RecipientCivilizationId") != after["Galaxy"].get("PlayerCivilizationId") or \
            type(proposals[0].get("AgreementType")) is not int or \
            proposals[0].get("AgreementType") != 4 or type(proposals[0].get("Status")) is not int or \
            proposals[0].get("Status") != 1 or \
            type(proposals[0].get("ResolvedAtTick")) is not int:
        raise RuntimeError("Native diplomacy did not resolve the authored incoming research exchange")
    player = after["Galaxy"]["PlayerCivilizationId"]
    agreements = [row for row in diplomacy.get("Agreements", [])
                  if type(row.get("Type")) is int and row.get("Type") == 4 and
                  type(row.get("Status")) is int and row.get("Status") == 0 and
                  {row.get("CivilizationAId"), row.get("CivilizationBId")} == {player, target_id}]
    if len(agreements) != 1:
        raise RuntimeError("Native diplomacy acceptance did not create the paired research agreement")


@_preserve_fixture
def validate_native_diplomacy_export(folder: Path, env: dict[str, str], fixture: Path):
    folder, fixture = folder.resolve(), fixture.resolve()
    source = _source_row(fixture)
    authored, proposal_id, target_id = author_diplomacy_fixture(source)
    systems = source.get("Galaxy", {}).get("Systems", [])
    system_root = Path(os.environ.get("SystemRoot", r"C:\\Windows"))
    clean = dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))
    captures, notification_captures, notification_checks, diagnostics = [], [], [], []
    with tempfile.TemporaryDirectory(prefix="stellar-native-diplomacy-") as temporary:
        work = Path(temporary)
        save = work / "diplomacy.player17.json"
        save.write_text(json.dumps(authored, ensure_ascii=False), encoding="utf-8")
        before = _canonical_progress_baseline(authored)
        for width, height, flag, mode, label in ((1280, 720, "--diplomacy-smoke", "progress", "known"),
                                                  (1920, 1080, "--diplomacy-reload-smoke", "paused_reload", "reload")):
            capture = work / f"diplomacy-{label}.bmp"
            args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                    "--save-path", str(save), "--load", "--width", str(width), "--height", str(height),
                    flag, str(capture)]
            result = subprocess.run(args, cwd=work, env=clean, capture_output=True, text=True, timeout=120)
            if result.returncode != 0:
                raise RuntimeError(f"Native diplomacy {mode} launch failed ({result.returncode}): {result.stderr}")
            if any(token not in result.stdout for token in ("gpu_driver=vulkan ", f"systems={len(systems)} ", "save=ok ")):
                raise RuntimeError("Native diplomacy did not confirm Vulkan, campaign and save")
            state = _state(result.stdout, mode)
            territory = _territory(result.stdout)
            notifications = _notifications(result.stdout, mode, target_id)
            if state["proposal_id"] != proposal_id or state["target_id"] != target_id:
                raise RuntimeError("Native diplomacy diagnostic selected the wrong proposal or counterpart")
            _bmp(capture, width, height)
            sidecar = capture.with_name(capture.stem + "-unknown.bmp")
            map_capture = capture.with_name(capture.stem + "-map.bmp")
            events_capture = capture.with_name(capture.stem + "-events.bmp")
            contact_capture = capture.with_name(capture.stem + "-events-contact.bmp")
            _validate_capture(events_capture, width, height)
            _validate_capture(contact_capture, width, height)
            _bmp(sidecar, width, height)
            _bmp(map_capture, width, height)
            if (events_capture.read_bytes() == contact_capture.read_bytes() or
                    events_capture.read_bytes() == map_capture.read_bytes()):
                raise RuntimeError("Native diplomacy notification capture duplicated another view")
            map_bytes = map_capture.read_bytes()
            if map_bytes == capture.read_bytes() or map_bytes == sidecar.read_bytes():
                raise RuntimeError("Native diplomacy regional map capture duplicated a diplomacy view")
            payload = json.loads(save.read_text(encoding="utf-8-sig"))
            if payload.get("FormatVersion") != 17 or not payload.get("SavedAtUtc"):
                raise RuntimeError("Native diplomacy did not save a current timestamped Player17 campaign")
            if mode == "progress":
                _verify_progress(before, payload, proposal_id, target_id)
                progress = payload
                territory_progress = territory
            elif _normalized(payload) != _normalized(progress):
                raise RuntimeError("Native diplomacy paused reload changed the Player17 payload")
            else:
                territory_reload = territory
            for image in (capture, sidecar, map_capture):
                evidence = folder.parent / f"{folder.name}-diplomacy-{image.name}"
                shutil.copy2(image, evidence)
                captures.append(str(evidence))
            for image in (events_capture, contact_capture):
                evidence = folder.parent / f"{folder.name}-diplomacy-{image.name}"
                shutil.copy2(image, evidence)
                notification_captures.append(str(evidence))
            notification_checks.append(notifications)
            diagnostics.append(result.stdout.strip())
    return {"nativeDiplomacy": True, "nativeDiplomacyPausedReload": True,
            "nativeTerritory": True, "territoryChecks": {"known": territory_progress,
                                                             "reload": territory_reload},
            "diplomacyCaptures": captures, "notificationCaptures": notification_captures,
            "notificationChecks": notification_checks, "diplomacyDiagnostics": diagnostics}
