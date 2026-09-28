"""Exact approved generated planet discs; no implicit directory-wide asset packaging."""
from __future__ import annotations

import hashlib
import json


NATIVE_PLANET_ART_SOURCES = {
    key: (f"assets/visual/planets/{key}.png", f"assets/visual/planets/{key}.png")
    for key in ("arid-world", "barren-world", "continental-world", "cracked-world",
                "desert-world", "frozen-world", "gaia-world", "inferno-world",
                "ocean-world", "tomb-world", "tropical-world", "volcano-world",
                "wormhole-anomaly",
                "mercury", "venus", "earth", "mars", "jupiter", "saturn",
                "uranus", "neptune", "moon")
}
NATIVE_PLANET_ART_SOURCES["credits"] = (
    "docs/engine/NATIVE_PLANET_ART_SOURCES.md", "Licenses/Planet-art-sources.md")


def native_planet_art_asset_files(root):
    declaration = json.loads((root / "export/native-planet-art-assets.json").read_text(encoding="utf-8"))
    if declaration.get("schemaVersion") != 1:
        raise RuntimeError("Unsupported native planet art asset declaration")
    records = declaration.get("assets", {})
    if not isinstance(records, dict) or set(records) != set(NATIVE_PLANET_ART_SOURCES):
        raise RuntimeError("Native planet art asset set differs from reviewed content")
    files = {}
    for key, (source, destination) in NATIVE_PLANET_ART_SOURCES.items():
        record = records[key]
        if not isinstance(record, dict) or record.get("source") != source or record.get("runtimePath") != destination:
            raise RuntimeError(f"Unreviewed native planet art {key} path")
        path = root / source
        if not path.is_file():
            raise RuntimeError(f"Missing native planet art {key}: {path}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != record.get("sha256"):
            raise RuntimeError(f"Native planet art {key} differs from reviewed content: {path}")
        files[destination] = path
    return files
