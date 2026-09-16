"""Observer-safe native diplomacy workspace validation against a real campaign.

The reviewed Player17 fixture already carries an identified contact, an active
non-aggression agreement and retained history. The validator authors an
unidentified signal contact and a pending incoming access petition so the
graphical smoke exercises both secrecy redaction and live proposal flow, then
drives the packaged native client through the relations workspace: contact
selection, the negotiation modal, a real command and the proposals tab.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

TICKS_PER_SIMULATION_DAY = 1000


def _source_row(fixture: Path) -> dict:
    root = json.loads(fixture.read_text(encoding="utf-8"))
    rows = root.get("Rows")
    if not isinstance(rows, list):
        raise RuntimeError("Player17 diplomacy fixture has no source-authored rows")
    matches = [row for row in rows if row.get("Name") == "valid-current17"]
    if len(matches) != 1 or not isinstance(matches[0].get("InputJson"), str):
        raise RuntimeError("Player17 diplomacy fixture has no unique valid-current17 input")
    return json.loads(matches[0]["InputJson"])


def _author_diplomacy_source(source: dict) -> tuple[dict, int]:
    authored = copy.deepcopy(source)
    galaxy = authored.get("Galaxy", {})
    systems = galaxy.get("Systems", [])
    diplomacy = authored.get("Diplomacy", {})
    days = authored.get("SimulationDays")
    player = galaxy.get("PlayerCivilizationId")
    if (authored.get("FormatVersion") != 17 or not systems or
            not isinstance(diplomacy, dict) or
            isinstance(days, bool) or not isinstance(days, (int, float)) or
            not math.isfinite(days) or
            isinstance(player, bool) or not isinstance(player, int)):
        raise RuntimeError("Diplomacy source row is not a Player17 campaign")
    contacts = diplomacy.get("Contacts")
    proposals = diplomacy.get("Proposals")
    if not isinstance(contacts, list) or not isinstance(proposals, list):
        raise RuntimeError("Diplomacy source row lacks contact/proposal lists")
    channels = [contact for contact in contacts
                if contact.get("ObserverCivilizationId") == player and
                contact.get("CommunicationAvailable") and
                isinstance(contact.get("TargetCivilizationId"), int)]
    if len(channels) != 1:
        raise RuntimeError("Diplomacy fixture must carry one open player channel")
    counterpart = channels[0]["TargetCivilizationId"]
    tick = round(days * TICKS_PER_SIMULATION_DAY)
    contacts.append({
        "ObserverCivilizationId": player,
        "ContactId": "unresolved-signal",
        "TargetCivilizationId": None,
        "FirstObservedTick": tick - 4,
        "LastObservedTick": tick,
        "LastObservedSystemId": None,
        "Awareness": 1,
        "Condition": 0,
        "CommunicationAvailable": False,
        "Confidence": 0.4,
    })
    proposal_id = diplomacy.get("NextProposalId")
    if (isinstance(proposal_id, bool) or not isinstance(proposal_id, int) or
            proposal_id <= 0):
        raise RuntimeError("Diplomacy fixture has no valid next proposal id")
    proposals.append({
        "ProposalId": proposal_id,
        "ProposerCivilizationId": counterpart,
        "RecipientCivilizationId": player,
        "Kind": 1,  # access_request
        "AgreementType": None,
        "Status": 0,  # pending
        "CreatedAtTick": tick,
        "ResolvedAtTick": None,
        "Summary": "Counterpart transit access petition",
        "ExternalTermsReference": None,
    })
    diplomacy["NextProposalId"] = proposal_id + 1
    # A communicated foreign territorial claim over the player's home system so
    # the strategic territory overlay proves its dashed-arc rendering path.
    home = next((civ.get("HomeSystemId") for civ in galaxy.get(
        "Civilizations", []) if civ.get("Id") == player), None)
    if not isinstance(home, int) or isinstance(home, bool):
        raise RuntimeError("Diplomacy fixture has no player home system")
    surveys = next((entry.get("SystemSurveys") for entry in
                    galaxy.get("Knowledge", [])
                    if entry.get("CivilizationId") == player), None)
    if not isinstance(surveys, list) or not any(
            survey.get("SystemId") == home and survey.get("Level") == 3
            for survey in surveys):
        raise RuntimeError("Player home system is not fully surveyed")
    observer_knowledge = next(
        entry for entry in galaxy["Knowledge"]
        if entry.get("CivilizationId") == player)
    known_civilizations = observer_knowledge.setdefault(
        "KnownCivilizationIds", [])
    if counterpart not in known_civilizations:
        known_civilizations.append(counterpart)
    claims = diplomacy.setdefault("Claims", [])
    if not isinstance(claims, list):
        raise RuntimeError("Diplomacy claims payload is malformed")
    claim_id = diplomacy.get("NextClaimId", 1)
    if isinstance(claim_id, bool) or not isinstance(claim_id, int):
        raise RuntimeError("Diplomacy fixture has no valid next claim id")
    claims.append({
        "ClaimId": claim_id,
        "ClaimantCivilizationId": counterpart,
        "SystemId": home,
        "AssertedAtTick": tick - 2,
        "Active": True,
        "KnownToCivilizationIds": [player, counterpart],
    })
    diplomacy["NextClaimId"] = claim_id + 1
    return authored, counterpart


def _diagnostic(stdout: str) -> dict:
    match = re.search(r"(?:^|\s)diplomacy=(\{[^{}]*\})(?:\s|$)", stdout)
    if not match:
        raise RuntimeError("Native diplomacy smoke did not report its evidence")
    try:
        state = json.loads(match.group(1))
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native diplomacy diagnostic is malformed") from error
    for key in ("contacts", "identified", "unidentified", "redacted",
                "channels", "agreements", "history", "proposals",
                "selected_civ", "last_observed_system", "portrait",
                "command_accepted", "proposal_id", "pending_after",
                "incoming_pending"):
        value = state.get(key)
        if isinstance(value, bool) or not isinstance(value, int):
            raise RuntimeError(f"Native diplomacy reported invalid {key}")
    if state["contacts"] < 2 or state["identified"] < 1:
        raise RuntimeError("Native diplomacy did not list the contact directory")
    if state["unidentified"] < 1:
        raise RuntimeError("Native diplomacy did not list the unidentified contact")
    if state["redacted"] != state["unidentified"]:
        raise RuntimeError("Unidentified contacts leaked identity details")
    if state["channels"] < 1:
        raise RuntimeError("Native diplomacy lost the open channel")
    if state["agreements"] < 1 or state["history"] < 1:
        raise RuntimeError("Native diplomacy lost agreements or history rows")
    if state["selected_civ"] <= 0:
        raise RuntimeError("Native diplomacy did not select the counterpart")
    if state["portrait"] != 1:
        raise RuntimeError("Native diplomacy did not decode the species portrait")
    if state["command_accepted"] != 1 or state["proposal_id"] <= 0:
        raise RuntimeError("Native diplomacy command was not accepted")
    if state["pending_after"] < 1:
        raise RuntimeError("Proposals tab did not show the sent proposal")
    if state["incoming_pending"] < 1:
        raise RuntimeError("Proposals tab did not show the incoming petition")
    return state


def _territory(stdout: str) -> dict:
    match = re.search(r"(?:^|\s)territory=(\{[^{}]*\})(?:\s|$)", stdout)
    if not match:
        raise RuntimeError("Native smoke did not report territory evidence")
    try:
        state = json.loads(match.group(1))
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native territory diagnostic is malformed") from error
    if not state.get("valid"):
        raise RuntimeError("Territory overlay did not build its projection")
    if state.get("claims", 0) < 1:
        raise RuntimeError("Territorial claim arc did not reach the render list")
    if state.get("regions", 0) < 1:
        raise RuntimeError("Territory regions did not reach the render list")
    return state


def _capture(path: Path) -> bytes:
    data = path.read_bytes() if path.is_file() else b""
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError(f"Native diplomacy did not capture a frame: {path}")
    if min(data[54:]) == max(data[54:]):
        raise RuntimeError("Native diplomacy capture contains no rendered variation")
    return data


def _saved_proposal(payload: dict, proposal_id: int) -> dict:
    proposals = payload.get("Diplomacy", {}).get("Proposals", [])
    matches = [proposal for proposal in proposals
               if proposal.get("ProposalId") == proposal_id]
    if len(matches) != 1:
        raise RuntimeError("Diplomacy smoke did not persist a unique proposal")
    return matches[0]


def _payload_check(payload: dict, systems: int) -> dict:
    days = payload.get("SimulationDays")
    if (payload.get("FormatVersion") != 17 or
            len(payload.get("Galaxy", {}).get("Systems", [])) != systems or
            isinstance(days, bool) or not isinstance(days, (int, float)) or
            not math.isfinite(days)):
        raise RuntimeError("Diplomacy smoke damaged the campaign payload")
    return payload.get("Diplomacy", {})


def validate_native_diplomacy_export(folder: Path, env: dict[str, str],
                                     player17_fixture: Path):
    source = _source_row(player17_fixture)
    systems = source.get("Galaxy", {}).get("Systems", [])
    if source.get("FormatVersion") != 17 or not systems:
        raise RuntimeError("Diplomacy source row is not a Player17 campaign")
    authored, counterpart = _author_diplomacy_source(source)
    player = source["Galaxy"]["PlayerCivilizationId"]
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean_env = dict(env, PATH=str(system_root / "System32") +
                     os.pathsep + str(system_root))
    captures, diagnostics = [], []
    first_proposal = None
    with tempfile.TemporaryDirectory(prefix="stellar-native-diplomacy-") as temporary:
        work = Path(temporary)
        save = work / "diplomacy.player17.json"
        save.write_text(json.dumps(authored, ensure_ascii=False), encoding="utf-8")
        for replay in (False, True):
            capture = work / ("diplomacy-loaded.bmp" if replay else
                              "diplomacy-ordered.bmp")
            args = [str(folder / "stellar-continuum-native.exe"),
                    "--asset-root", str(folder), "--save-path", str(save),
                    "--load", "--diplomacy-smoke", str(capture)]
            result = subprocess.run(args, cwd=work, env=clean_env,
                                    capture_output=True, text=True,
                                    encoding="utf-8", errors="strict",
                                    timeout=120)
            if result.returncode != 0:
                raise RuntimeError(
                    f"Native diplomacy smoke failed ({result.returncode}):\n"
                    f"{result.stdout}\n{result.stderr}")
            if any(token not in result.stdout for token in
                   ("gpu_driver=vulkan ", f"systems={len(systems)} ",
                    "save=ok ")):
                raise RuntimeError(
                    "Native diplomacy did not confirm Vulkan, campaign and save")
            state = _diagnostic(result.stdout)
            _territory(result.stdout)
            if state["selected_civ"] != counterpart:
                raise RuntimeError("Native diplomacy selected the wrong contact")
            proposals_capture = capture.with_name(
                capture.stem + "-proposals" + capture.suffix)
            workspace_pixels = _capture(capture)
            proposal_pixels = _capture(proposals_capture)
            if hashlib.sha256(workspace_pixels).digest() == \
                    hashlib.sha256(proposal_pixels).digest():
                raise RuntimeError(
                    "Native diplomacy captures do not show two distinct views")
            if not save.is_file():
                raise RuntimeError("Diplomacy smoke did not write its isolated save")
            payload = json.loads(save.read_text(encoding="utf-8"))
            diplomacy = _payload_check(payload, len(systems))
            proposal = _saved_proposal(payload, state["proposal_id"])
            if (proposal.get("ProposerCivilizationId") != player or
                    proposal.get("RecipientCivilizationId") != counterpart or
                    proposal.get("Kind") != 1 or proposal.get("Status") != 0):
                raise RuntimeError(
                    "Persisted proposal is not the player's pending access request")
            for contact in diplomacy.get("Contacts", []):
                if (contact.get("ContactId") == "unresolved-signal" and
                        contact.get("TargetCivilizationId") is not None):
                    raise RuntimeError(
                        "Unidentified contact gained a target civilization")
            if replay:
                prior = _saved_proposal(payload, first_proposal)
                if prior.get("Status") != 0:
                    raise RuntimeError(
                        "First-run proposal did not survive the reload")
            else:
                first_proposal = state["proposal_id"]
            for path in (capture, proposals_capture):
                evidence = folder.parent / f"{folder.name}-{path.name}"
                shutil.copy2(path, evidence)
                captures.append(str(evidence))
            diagnostics.append(result.stdout.strip())
    return {"nativeDiplomacyContacts": True,
            "nativeDiplomacyObserverRedaction": True,
            "nativeDiplomacyCommand": True,
            "nativeDiplomacyReload": True,
            "diplomacyCaptures": captures,
            "diplomacyDiagnostics": diagnostics}
