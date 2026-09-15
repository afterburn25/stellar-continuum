"""Reviewed native UI font inputs; no machine font is redistributed."""
from __future__ import annotations

import hashlib
import json


def native_ui_asset_files(root):
    declaration = json.loads((root / "export/native-ui-assets.json").read_text(encoding="utf-8"))
    if declaration.get("schemaVersion") != 1:
        raise RuntimeError("Unsupported native UI asset declaration")
    paths = {
        "font": ("assets/visual/fonts/Rajdhani-SemiBold.ttf", "assets/visual/fonts/Rajdhani-SemiBold.ttf"),
        "license": ("assets/visual/fonts/OFL-Rajdhani.txt", "Licenses/OFL-Rajdhani.txt"),
    }
    files = {}
    for kind, (source, destination) in paths.items():
        record = declaration.get(kind, {})
        if record.get("source") != source or record.get("runtimePath") != destination:
            raise RuntimeError(f"Unreviewed native UI {kind} path")
        path = root / source
        if not path.is_file():
            raise RuntimeError(f"Missing native UI {kind}: {path}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != record.get("sha256"):
            raise RuntimeError(f"Native UI {kind} differs from reviewed content: {path}")
        files[destination] = path
    return files
