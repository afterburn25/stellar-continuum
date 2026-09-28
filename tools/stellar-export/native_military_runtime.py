"""Relocated mouse-driven strategic military orders and durable Player17 proof."""
from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from native_fleet_runtime import _source_row, _fleet
from native_new_game_runtime import _bmp

_CHECKS = {"selection", "hold", "defend", "retreat", "locate", "zoom_preserved",
           "only_order_changed", "order_saved"}


def parse_military_check(stdout: str) -> dict:
    records = re.findall(r"^military_inspection=(.*)$", stdout, re.MULTILINE)
    if len(records) != 1:
        raise RuntimeError("Expected one military inspection proof")

    def unique_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate military key")
            result[key] = value
        return result

    try:
        state = json.loads(records[0], object_pairs_hook=unique_pairs)
    except (TypeError, ValueError) as error:
        raise RuntimeError("Malformed military proof") from error
    if (not isinstance(state, dict) or set(state) != _CHECKS or
            any(value is not True for value in state.values())):
        raise RuntimeError("Military inspection proof is incomplete")
    return state


def military_source(fixture: Path) -> tuple[dict, int]:
    """Author only an isolated test ship; never alter the player's actual slot."""
    source = _source_row(fixture)
    galaxy = source["Galaxy"]
    eligible = [fleet for fleet in galaxy["Fleets"]
                if fleet.get("CivilizationId") == galaxy["PlayerCivilizationId"]
                and fleet.get("IsActive") and fleet.get("CurrentSystemId") is not None
                and fleet.get("DestinationSystemId") is None]
    if not eligible:
        raise RuntimeError("Military source has no stationed owned ship")
    fleet_id = eligible[0]["Id"]
    fleet, _ = _fleet(source, fleet_id)
    fleet.update(Name="Home Guard Corvette", Role=3, DesignId="patrol_corvette")
    fleet["Combat"] = {"ProfileId": "patrol_corvette_mk1", "Shields": 35,
                       "Armor": 45, "Hull": 95, "WeaponCooldownRemainingDays": 0,
                       "Order": 0, "TargetFleetId": None, "DefendSystemId": None,
                       "RetreatProgressDays": 0, "RetreatStarted": False,
                       "IsDisengaged": False, "DisengagedSystemId": None}
    return source, fleet_id


def check_military_payload(payload: dict, source: dict, fleet_id: int) -> None:
    if (payload.get("FormatVersion") != 17 or not payload.get("SavedAtUtc") or
            payload.get("SimulationDays") != source["SimulationDays"] or
            payload.get("Galaxy", {}).get("PlayerCivilizationId") !=
            source["Galaxy"]["PlayerCivilizationId"] or
            len(payload.get("Galaxy", {}).get("Systems", [])) !=
            len(source["Galaxy"]["Systems"])):
        raise RuntimeError("Military input changed paused campaign identity or time")
    fleet, _ = _fleet(payload, fleet_id)
    before, _ = _fleet(source, fleet_id)
    combat = fleet.get("Combat", {})
    if (fleet.get("Role") != 3 or fleet.get("CurrentSystemId") != before["CurrentSystemId"] or
            fleet.get("DestinationSystemId") != before["DestinationSystemId"] or
            fleet.get("MissionOrderRevision") != before["MissionOrderRevision"] or
            type(combat.get("Order")) is not int or combat["Order"] != 1 or
            combat.get("DefendSystemId") != before["CurrentSystemId"] or
            combat.get("TargetFleetId") is not None):
        raise RuntimeError("Military save did not retain the owned ship's Defend order")


def validate_native_military_export(folder: Path, env: dict[str, str],
                                    player17_fixture: Path) -> dict:
    folder = folder.resolve()
    source, fleet_id = military_source(player17_fixture)
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean = dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))
    captures, diagnostics, baseline = [], [], None
    with tempfile.TemporaryDirectory(prefix="stellar-military-路径-") as temporary:
        work = Path(temporary)
        cwd = work / "different-cwd-目录"
        cwd.mkdir()
        save = work / "military.player17.json"
        save.write_text(json.dumps(source, ensure_ascii=False), encoding="utf-8")
        for width, height in ((1280, 720), (1920, 1080)):
            capture = work / f"military-{width}x{height}.bmp"
            args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                    "--save-path", str(save), "--load", "--width", str(width),
                    "--height", str(height), "--smoke", str(capture), "--military-check"]
            result = subprocess.run(args, cwd=cwd, env=clean, capture_output=True,
                                    text=True, encoding="utf-8", timeout=150)
            if result.returncode:
                raise RuntimeError(f"Native military failed ({result.returncode}): "
                                   f"{result.stdout}\n{result.stderr}")
            parse_military_check(result.stdout)
            if any(token not in result.stdout for token in
                   ("gpu_driver=vulkan ", f"systems={len(source['Galaxy']['Systems'])} ", "save=ok ")):
                raise RuntimeError("Military did not prove Vulkan, campaign and saving")
            for suffix in ("", "-military-ready", "-military-retreat", "-military-located"):
                image = capture.with_stem(capture.stem + suffix)
                _bmp(image, width, height)
                target = folder.parent / f"{folder.name}-{image.name}"
                shutil.copy2(image, target)
                captures.append(str(target))
            payload = json.loads(save.read_text(encoding="utf-8"))
            check_military_payload(payload, source, fleet_id)
            payload.pop("SavedAtUtc", None)
            if baseline is not None and payload != baseline:
                raise RuntimeError("Military reload changed Player17 state beyond its saved order")
            baseline = copy.deepcopy(payload)
            diagnostics.append(result.stdout.strip())
    return {"nativeMilitaryOrders": True, "nativeMilitaryLocate": True,
            "militaryOrderReloadVerified": True, "militaryCaptures": captures,
            "militaryDiagnostics": diagnostics}
