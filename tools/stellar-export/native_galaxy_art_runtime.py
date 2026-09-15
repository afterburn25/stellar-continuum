"""Exact approved native galaxy artwork; no implicit directory-wide asset packaging."""
from __future__ import annotations

import hashlib
import json


NATIVE_GALAXY_ART_SOURCES = {
    key: (f"assets/visual/space/{key}.png", f"assets/visual/space/{key}.png")
    for key in ('deep-field-v2', 'milky-way-layer-v2', 'regional-nebula-b')
}
NATIVE_GALAXY_ART_SOURCES["credits"] = (
    "docs/engine/NATIVE_GALAXY_ART_SOURCES.md", "Licenses/Galaxy-art-sources.md")


def native_galaxy_art_asset_files(root):
    declaration = json.loads((root / "export/native-galaxy-art-assets.json").read_text(encoding="utf-8"))
    if declaration.get("schemaVersion") != 1:
        raise RuntimeError("Unsupported native galaxy art asset declaration")
    records = declaration.get("assets", {})
    if not isinstance(records, dict) or set(records) != set(NATIVE_GALAXY_ART_SOURCES):
        raise RuntimeError("Native galaxy art asset set differs from reviewed content")
    files = {}
    for key, (source, destination) in NATIVE_GALAXY_ART_SOURCES.items():
        record = records[key]
        if not isinstance(record, dict) or record.get("source") != source or record.get("runtimePath") != destination:
            raise RuntimeError(f"Unreviewed native galaxy art {key} path")
        path = root / source
        if not path.is_file():
            raise RuntimeError(f"Missing native galaxy art {key}: {path}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != record.get("sha256"):
            raise RuntimeError(f"Native galaxy art {key} differs from reviewed content: {path}")
        files[destination] = path
    return files
