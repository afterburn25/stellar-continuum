"""Observer-safe native notification feed validation against a real campaign.

The validator reuses the authored diplomacy source (identified channel,
unidentified signal contact, communicated foreign claim) and drives the
packaged native client through the recent-events flow: the player submits a
diplomatic proposal, the session harvests the observer-filtered bulletin into
the notification feed, the top-bar toggle opens the panel, and the card's
OPEN RELATIONS shortcut focuses the counterpart in the relations workspace.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from native_diplomacy_runtime import _author_diplomacy_source, _source_row


def _diagnostic(stdout: str) -> dict:
    match = re.search(r"(?:^|\s)notifications=(\{[^{}]*\})(?:\s|$)", stdout)
    if not match:
        raise RuntimeError("Native notification smoke did not report its evidence")
    try:
        state = json.loads(match.group(1))
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native notification diagnostic is malformed") from error
    for key in ("panel", "items", "unread", "diplomacy", "contact",
                "focused_civ"):
        value = state.get(key)
        if isinstance(value, bool) or not isinstance(value, int):
            raise RuntimeError(f"Native notification reported invalid {key}")
    if state["panel"] != 1:
        raise RuntimeError("Native notification panel did not open")
    if state["items"] < 1 or state["unread"] < 1:
        raise RuntimeError("Native notification feed did not publish the bulletin")
    if state["diplomacy"] < 1 or state["contact"] <= 0:
        raise RuntimeError(
            "Native notification bulletin lost its identified contact")
    if state["focused_civ"] != state["contact"]:
        raise RuntimeError(
            "OPEN RELATIONS did not focus the notification's contact")
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
    return state


def _capture(path: Path) -> bytes:
    data = path.read_bytes() if path.is_file() else b""
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError(f"Native notification did not capture a frame: {path}")
    if min(data[54:]) == max(data[54:]):
        raise RuntimeError("Native notification capture contains no rendered variation")
    return data


def validate_native_notification_export(folder: Path, env: dict[str, str],
                                        player17_fixture: Path):
    source = _source_row(player17_fixture)
    systems = source.get("Galaxy", {}).get("Systems", [])
    if source.get("FormatVersion") != 17 or not systems:
        raise RuntimeError("Notification source row is not a Player17 campaign")
    authored, counterpart = _author_diplomacy_source(source)
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean_env = dict(env, PATH=str(system_root / "System32") +
                     os.pathsep + str(system_root))
    captures, diagnostics = [], []
    with tempfile.TemporaryDirectory(prefix="stellar-native-notify-") as temporary:
        work = Path(temporary)
        save = work / "notification.player17.json"
        save.write_text(json.dumps(authored, ensure_ascii=False),
                        encoding="utf-8")
        for replay in (False, True):
            capture = work / ("notification-loaded.bmp" if replay else
                              "notification-ordered.bmp")
            args = [str(folder / "stellar-continuum-native.exe"),
                    "--asset-root", str(folder), "--save-path", str(save),
                    "--load", "--notification-smoke", str(capture)]
            result = subprocess.run(args, cwd=work, env=clean_env,
                                    capture_output=True, text=True,
                                    encoding="utf-8", errors="strict",
                                    timeout=120)
            if result.returncode != 0:
                raise RuntimeError(
                    f"Native notification smoke failed ({result.returncode}):\n"
                    f"{result.stdout}\n{result.stderr}")
            if any(token not in result.stdout for token in
                   ("gpu_driver=vulkan ", f"systems={len(systems)} ",
                    "save=ok ")):
                raise RuntimeError(
                    "Native notification did not confirm Vulkan, campaign and save")
            state = _diagnostic(result.stdout)
            _territory(result.stdout)
            if state["contact"] != counterpart:
                raise RuntimeError(
                    "Notification bulletin is not bound to the proposal counterpart")
            contact_capture = capture.with_name(
                capture.stem + "-contact" + capture.suffix)
            panel_pixels = _capture(capture)
            contact_pixels = _capture(contact_capture)
            if hashlib.sha256(panel_pixels).digest() == \
                    hashlib.sha256(contact_pixels).digest():
                raise RuntimeError(
                    "Native notification captures do not show the panel and "
                    "the focused workspace")
            if not save.is_file():
                raise RuntimeError(
                    "Notification smoke did not write its isolated save")
            payload = json.loads(save.read_text(encoding="utf-8"))
            days = payload.get("SimulationDays")
            if (payload.get("FormatVersion") != 17 or
                    len(payload.get("Galaxy", {}).get("Systems", [])) !=
                    len(systems) or
                    isinstance(days, bool) or
                    not isinstance(days, (int, float)) or
                    not math.isfinite(days)):
                raise RuntimeError(
                    "Notification smoke damaged the campaign payload")
            # Retained history must not flood the feed: only the proposal sent
            # during the session may publish (plus at most one live side-effect
            # bulletin), on every load.
            if state["items"] != state["diplomacy"]:
                raise RuntimeError(
                    "Native notification feed published unexpected categories")
            if state["items"] > 2:
                raise RuntimeError(
                    "Loaded campaign republished retained diplomatic history")
            for path in (capture, contact_capture):
                evidence = folder.parent / f"{folder.name}-{path.name}"
                shutil.copy2(path, evidence)
                captures.append(str(evidence))
            diagnostics.append(result.stdout.strip())
    return {"nativeNotificationFeed": True,
            "nativeNotificationObserverRedaction": True,
            "nativeNotificationContactFocus": True,
            "nativeNotificationReload": True,
            "notificationCaptures": captures,
            "notificationDiagnostics": diagnostics}
