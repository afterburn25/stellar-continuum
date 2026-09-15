"""Exact approved species identity portraits; no implicit directory-wide asset packaging."""
from __future__ import annotations

import hashlib
import json


NATIVE_SPECIES_SOURCES = {
    key: (f"assets/visual/species/{key}.jpg", f"assets/visual/species/{key}.jpg")
    for key in ('terran-baseline', 'pelagic-high-pressure', 'compact-high-gravity', 'cryogenic-hydrocarbon')
}
NATIVE_SPECIES_SOURCES.update({
    key: (f"assets/visual/species/{key}.png", f"assets/visual/species/{key}.png")
    for key in ('terran-baseline-communications-v2', 'pelagic-high-pressure-communications-v2',
                'compact-high-gravity-communications-v2', 'cryogenic-hydrocarbon-communications-v2')
})
NATIVE_SPECIES_SOURCES["credits"] = (
    "docs/engine/NATIVE_SPECIES_ART_SOURCES.md", "Licenses/Species-visual-sources.md")


def native_species_asset_files(root):
    declaration = json.loads((root / "export/native-species-assets.json").read_text(encoding="utf-8"))
    if declaration.get("schemaVersion") != 1:
        raise RuntimeError("Unsupported native species asset declaration")
    records = declaration.get("assets", {})
    if not isinstance(records, dict) or set(records) != set(NATIVE_SPECIES_SOURCES):
        raise RuntimeError("Native species asset set differs from reviewed content")
    files = {}
    for key, (source, destination) in NATIVE_SPECIES_SOURCES.items():
        record = records[key]
        if not isinstance(record, dict) or record.get("source") != source or record.get("runtimePath") != destination:
            raise RuntimeError(f"Unreviewed native species {key} path")
        path = root / source
        if not path.is_file():
            raise RuntimeError(f"Missing native species {key}: {path}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != record.get("sha256"):
            raise RuntimeError(f"Native species {key} differs from reviewed content: {path}")
        files[destination] = path
    return files
