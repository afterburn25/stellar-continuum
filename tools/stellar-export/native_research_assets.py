"""Verified research illustrations, without planned unlocks or directory-wide export."""
import hashlib
import json
import re


def native_research_asset_files(root):
    declaration = json.loads((root / "export/native-research-assets.json").read_text(encoding="utf-8"))
    if declaration.get("schemaVersion") != 1 or not isinstance(declaration.get("assets"), list):
        raise RuntimeError("Unsupported research artwork declaration")
    files = {}
    prefix = "assets/visual/catalog/"
    metadata = {prefix + "catalog.json", prefix + "production/provenance.json"}
    for record in declaration["assets"]:
        path = record.get("path", "")
        if path not in metadata and not re.fullmatch(
                r"assets/visual/catalog/(thumbnails|portraits)/research-[a-z_]+\.png", path):
            raise RuntimeError("Unreviewed research artwork path")
        source = root / path
        if path in files or not source.is_file():
            raise RuntimeError(f"Missing or duplicate research artwork: {path}")
        if hashlib.sha256(source.read_bytes()).hexdigest() != record.get("sha256"):
            raise RuntimeError(f"Research artwork differs from reviewed content: {path}")
        files[path] = source
    if not metadata <= files.keys():
        raise RuntimeError("Missing research illustration metadata")
    catalog = json.loads(files[prefix + "catalog.json"].read_text(encoding="utf-8"))
    art_ids = {entry["art"] for entry in catalog["research"]}
    expected = metadata | {f"{prefix}{size}/{art}.png" for size in ("thumbnails", "portraits") for art in art_ids}
    if expected != files.keys():
        raise RuntimeError("Research illustration coverage differs from current bindings")
    return files
