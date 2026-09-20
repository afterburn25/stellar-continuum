"""Exact approved native leader artwork; no implicit directory-wide asset packaging."""
from __future__ import annotations

import hashlib
import json


NATIVE_LEADER_ART_SOURCES = {
    key: (f"assets/visual/leaders/{key}.jpg", f"assets/visual/leaders/{key}.jpg")
    for key in ('chief-scientist', 'fleet-commander', 'planetary-governor')
}
NATIVE_LEADER_ART_SOURCES["credits"] = (
    "docs/engine/NATIVE_LEADER_ART_SOURCES.md", "Licenses/Leader-art-sources.md")


def native_leader_art_asset_files(root):
    declaration = json.loads((root / "export/native-leader-art-assets.json").read_text(encoding="utf-8"))
    if declaration.get("schemaVersion") != 1:
        raise RuntimeError("Unsupported native leader art asset declaration")
    records = declaration.get("assets", {})
    if not isinstance(records, dict) or set(records) != set(NATIVE_LEADER_ART_SOURCES):
        raise RuntimeError("Native leader art asset set differs from reviewed content")
    files = {}
    for key, (source, destination) in NATIVE_LEADER_ART_SOURCES.items():
        record = records[key]
        if not isinstance(record, dict) or record.get("source") != source or record.get("runtimePath") != destination:
            raise RuntimeError(f"Unreviewed native leader art {key} path")
        path = root / source
        if not path.is_file():
            raise RuntimeError(f"Missing native leader art {key}: {path}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != record.get("sha256"):
            raise RuntimeError(f"Native leader art {key} differs from reviewed content: {path}")
        files[destination] = path
    return files
