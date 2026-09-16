"""Exact approved generated star discs; no implicit directory-wide asset packaging."""
from __future__ import annotations

import hashlib
import json


NATIVE_STAR_ART_SOURCES = {
    key: (f"assets/visual/stars/{key}.png", f"assets/visual/stars/{key}.png")
    for key in ("star-a-white", "star-f-dwarf", "star-g-dwarf", "star-giant",
                "star-hot-blue", "star-k-dwarf", "star-m-dwarf", "star-neutron",
                "star-protostar", "star-pulsar", "star-white-dwarf")
}
NATIVE_STAR_ART_SOURCES["credits"] = (
    "docs/engine/NATIVE_STAR_ART_SOURCES.md", "Licenses/Star-art-sources.md")


def native_star_art_asset_files(root):
    declaration = json.loads((root / "export/native-star-art-assets.json").read_text(encoding="utf-8"))
    if declaration.get("schemaVersion") != 1:
        raise RuntimeError("Unsupported native star art asset declaration")
    records = declaration.get("assets", {})
    if not isinstance(records, dict) or set(records) != set(NATIVE_STAR_ART_SOURCES):
        raise RuntimeError("Native star art asset set differs from reviewed content")
    files = {}
    for key, (source, destination) in NATIVE_STAR_ART_SOURCES.items():
        record = records[key]
        if not isinstance(record, dict) or record.get("source") != source or record.get("runtimePath") != destination:
            raise RuntimeError(f"Unreviewed native star art {key} path")
        path = root / source
        if not path.is_file():
            raise RuntimeError(f"Missing native star art {key}: {path}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != record.get("sha256"):
            raise RuntimeError(f"Native star art {key} differs from reviewed content: {path}")
        files[destination] = path
    return files
