"""Validated Godot semantic SVG icons and their native PNG raster assets."""
from __future__ import annotations
import hashlib
import json

NAMES = ("galaxy", "home", "inspection", "zoom_in", "zoom_out", "economy", "research", "shipyard", "construction", "exploration", "colonization", "logistics", "relations", "settings")
SOURCES = {
    "galaxy": ("assets/visual/ui/navigation/nav_galaxy.svg", "assets/visual/native-navigation/nav_galaxy.png"),
    "home": ("assets/visual/ui/navigation/nav_home.svg", "assets/visual/native-navigation/nav_home.png"),
    "inspection": ("assets/visual/ui/navigation/nav_inspection.svg", "assets/visual/native-navigation/nav_inspection.png"),
    "zoom_in": ("assets/visual/icons/navigation/nav_zoom_in.svg", "assets/visual/native-navigation/nav_zoom_in.png"),
    "zoom_out": ("assets/visual/icons/navigation/nav_zoom_out.svg", "assets/visual/native-navigation/nav_zoom_out.png"),
    "economy": ("assets/visual/ui/navigation/nav_economy.svg", "assets/visual/native-navigation/nav_economy.png"),
    "research": ("assets/visual/ui/navigation/nav_research.svg", "assets/visual/native-navigation/nav_research.png"),
    "shipyard": ("assets/visual/ui/navigation/nav_shipyard.svg", "assets/visual/native-navigation/nav_shipyard.png"),
    "construction": ("assets/visual/ui/navigation/nav_construction.svg", "assets/visual/native-navigation/nav_construction.png"),
    "exploration": ("assets/visual/ui/navigation/nav_exploration.svg", "assets/visual/native-navigation/nav_exploration.png"),
    "colonization": ("assets/visual/ui/navigation/nav_colonization.svg", "assets/visual/native-navigation/nav_colonization.png"),
    "logistics": ("assets/visual/ui/navigation/nav_logistics.svg", "assets/visual/native-navigation/nav_logistics.png"),
    "relations": ("assets/visual/ui/navigation/nav_relations.svg", "assets/visual/native-navigation/nav_relations.png"),
    "settings": ("assets/visual/ui/navigation/nav_settings.svg", "assets/visual/native-navigation/nav_settings.png"),
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
