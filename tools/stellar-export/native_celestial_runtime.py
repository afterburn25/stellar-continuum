"""Exact approved orbital imagery; no implicit directory-wide asset packaging."""
from __future__ import annotations

import hashlib
import json


NATIVE_CELESTIAL_SOURCES = {
    key: (f"assets/visual/sol/{key}.jpg", f"assets/visual/sol/{key}.jpg")
    for key in ("mercury", "venus", "earth", "mars", "jupiter", "saturn", "uranus", "neptune", "moon")
}
NATIVE_CELESTIAL_SOURCES["credits"] = (
    "docs/SOL_VISUAL_SOURCES.md", "Licenses/Sol-visual-sources.md")


def native_celestial_asset_files(root):
    declaration = json.loads((root / "export/native-celestial-assets.json").read_text(encoding="utf-8"))
    if declaration.get("schemaVersion") != 1:
        raise RuntimeError("Unsupported native celestial asset declaration")
    records = declaration.get("assets", {})
    if not isinstance(records, dict) or set(records) != set(NATIVE_CELESTIAL_SOURCES):
        raise RuntimeError("Native celestial asset set differs from reviewed content")
    files = {}
    for key, (source, destination) in NATIVE_CELESTIAL_SOURCES.items():
        record = records[key]
        if not isinstance(record, dict) or record.get("source") != source or record.get("runtimePath") != destination:
            raise RuntimeError(f"Unreviewed native celestial {key} path")
        path = root / source
        if not path.is_file():
            raise RuntimeError(f"Missing native celestial {key}: {path}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != record.get("sha256"):
            raise RuntimeError(f"Native celestial {key} differs from reviewed content: {path}")
        files[destination] = path
    return files
