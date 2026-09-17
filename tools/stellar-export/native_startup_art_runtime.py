"""Exact approved native menu/loading artwork; no implicit directory-wide asset packaging."""
from __future__ import annotations

import hashlib
import json


NATIVE_STARTUP_ART_SOURCES = {
    key: (f"assets/visual/loading/{key}.png", f"assets/visual/loading/{key}.png")
    for key in ('stellar-continuum-splash', 'stellar-loading-splash', 'stellar-galaxy-generation', 'stellar-save-loading')
}
for key, path in {"empire-emblem": "assets/visual/branding/stellar-continuum-icon-v1.png",
                  "title-logo": "assets/visual/branding/stellar-continuum-title-v1.png",
                  "galaxy-card": "assets/visual/space/campaign-galaxy-four-arm-v1.png"}.items():
    NATIVE_STARTUP_ART_SOURCES[key] = (path, path)
NATIVE_STARTUP_ART_SOURCES["credits"] = (
    "docs/engine/NATIVE_STARTUP_ART_SOURCES.md", "Licenses/Startup-art-sources.md")


def native_startup_art_asset_files(root):
    declaration = json.loads((root / "export/native-startup-art-assets.json").read_text(encoding="utf-8"))
    if declaration.get("schemaVersion") != 1:
        raise RuntimeError("Unsupported native startup art asset declaration")
    records = declaration.get("assets", {})
    if not isinstance(records, dict) or set(records) != set(NATIVE_STARTUP_ART_SOURCES):
        raise RuntimeError("Native startup art asset set differs from reviewed content")
    files = {}
    for key, (source, destination) in NATIVE_STARTUP_ART_SOURCES.items():
        record = records[key]
        if not isinstance(record, dict) or record.get("source") != source or record.get("runtimePath") != destination:
            raise RuntimeError(f"Unreviewed native startup art {key} path")
        path = root / source
        if not path.is_file():
            raise RuntimeError(f"Missing native startup art {key}: {path}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != record.get("sha256"):
            raise RuntimeError(f"Native startup art {key} differs from reviewed content: {path}")
        files[destination] = path
    return files
