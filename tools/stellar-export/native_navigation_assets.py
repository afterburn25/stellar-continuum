from __future__ import annotations
import hashlib
import json

NAMES = ("research", "shipyard", "construction", "relations")
SOURCES = {name: (f"assets/visual/ui/navigation/nav_{name}.svg", f"assets/visual/ui/navigation/nav_{name}.png") for name in NAMES}
def native_navigation_asset_files(root):
    try:
        declaration = json.loads((root / "export/native-navigation-assets.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise RuntimeError("Cannot read native navigation asset declaration") from error
    if (not isinstance(declaration, dict) or declaration.get("schemaVersion") != 1 or
            declaration.get("size") != 256 or not isinstance(declaration.get("assets"), dict) or
            set(declaration["assets"]) != set(NAMES)):
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
            raise RuntimeError(f"Navigation SVG hash mismatch: {source}")
        if hashlib.sha256(runtime.read_bytes()).hexdigest() != record["runtimeSha256"].lower():
            raise RuntimeError(f"Navigation PNG hash mismatch: {runtime}")
        files[record["runtimePath"]] = runtime
    return files
