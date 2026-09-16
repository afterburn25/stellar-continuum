"""Exact approved native surface artwork; no directory-wide surface asset packaging."""
from __future__ import annotations

import hashlib
import json


NATIVE_SURFACE_ART_SOURCES = {
    "temperate-ground-albedo-v1": (
        "assets/visual/surface/temperate-ground-albedo-v1.png",
        "assets/visual/surface/temperate-ground-albedo-v1.png"),
    "credits": (
        "docs/engine/NATIVE_SURFACE_ART_SOURCES.md",
        "Licenses/Surface-art-sources.md"),
}


def native_surface_art_asset_files(root):
    declaration = json.loads(
        (root / "export/native-surface-art-assets.json").read_text(encoding="utf-8"))
    if declaration.get("schemaVersion") != 1:
        raise RuntimeError("Unsupported native surface art asset declaration")
    records = declaration.get("assets", {})
    if not isinstance(records, dict) or set(records) != set(NATIVE_SURFACE_ART_SOURCES):
        raise RuntimeError("Native surface art asset set differs from reviewed content")
    files = {}
    for key, (source, destination) in NATIVE_SURFACE_ART_SOURCES.items():
        record = records[key]
        if (not isinstance(record, dict) or record.get("source") != source or
                record.get("runtimePath") != destination):
            raise RuntimeError(f"Unreviewed native surface art {key} path")
        path = root / source
        if not path.is_file():
            raise RuntimeError(f"Missing native surface art {key}: {path}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != record.get("sha256"):
            raise RuntimeError(f"Native surface art {key} differs from reviewed content: {path}")
        files[destination] = path
    return files
