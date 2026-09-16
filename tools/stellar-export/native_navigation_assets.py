"""Validated, approved source photos used by native navigation."""
from __future__ import annotations
import hashlib
import json

NAMES = ("galaxy", "home", "inspection", "zoom_in", "zoom_out", "economy", "research", "shipyard", "construction", "exploration", "colonization", "logistics", "relations", "settings")
SOURCES = {
    "galaxy": ("assets/visual/space/campaign-galaxy-four-arm-v1.png",) * 2,
    "home": ("assets/visual/sol/earth.jpg",) * 2,
    "inspection": ("assets/visual/sol/earth-map.jpg",) * 2,
    "zoom_in": ("assets/visual/space/deep-field-v2.png",) * 2,
    "zoom_out": ("assets/visual/space/galactic-dust-detail-v1.png",) * 2,
    "economy": ("assets/visual/catalog/portraits/research-economic_trade.png",) * 2,
    "research": ("assets/visual/catalog/portraits/research-foundations.png",) * 2,
    "shipyard": ("assets/visual/ships/interstellar-bulk-freighter.png",) * 2,
    "construction": ("assets/visual/catalog/portraits/human_station_industry--l1.png",) * 2,
    "exploration": ("assets/visual/ships/pathfinder-scout.jpg",) * 2,
    "colonization": ("assets/visual/ships/interstellar-colony-ship.jpg",) * 2,
    "logistics": ("assets/visual/ships/resource-outpost-ship.png",) * 2,
    "relations": ("assets/visual/catalog/portraits/research-social_admin.png",) * 2,
    "settings": ("assets/visual/catalog/portraits/research-computing.png",) * 2,
}

def native_navigation_asset_files(root):
    try:
        declaration = json.loads((root / "export/native-navigation-assets.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise RuntimeError("Cannot read native navigation asset declaration") from error
    if (not isinstance(declaration, dict) or declaration.get("schemaVersion") != 1 or declaration.get("size") != 256 or not isinstance(declaration.get("assets"), dict) or set(declaration["assets"]) != set(NAMES)):
        raise RuntimeError("Unsupported native navigation asset declaration")
    files = {}
    for name in NAMES:
        record = declaration["assets"][name]
        if not isinstance(record, dict):
            raise RuntimeError("Malformed native navigation declaration")
        for field in ("source", "runtimePath", "sourceSha256", "runtimeSha256"):
            if not isinstance(record.get(field), str):
                raise RuntimeError("Malformed native navigation declaration")
        expected_source, expected_runtime = SOURCES[name]
        if record["source"] != expected_source or record["runtimePath"] != expected_runtime:
            raise RuntimeError("Unapproved native navigation asset path")
        source, runtime = root / expected_source, root / expected_runtime
        if not source.is_file() or not runtime.is_file():
            raise RuntimeError(f"Missing native navigation asset: {source} or {runtime}")
        if hashlib.sha256(source.read_bytes()).hexdigest() != record["sourceSha256"].lower():
            raise RuntimeError(f"Navigation source hash mismatch: {source}")
        if hashlib.sha256(runtime.read_bytes()).hexdigest() != record["runtimeSha256"].lower():
            raise RuntimeError(f"Navigation runtime hash mismatch: {runtime}")
        files[record["runtimePath"]] = runtime
    return files
