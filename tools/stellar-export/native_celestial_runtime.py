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
# The planetary workspace renders a globe and building cards in addition to
# the orbital discs. All of its lazy-loaded images must travel with the game.
for key in ("earth-map", "earth-night-map", "earth-clouds"):
    path = f"assets/visual/sol/{key}.jpg"
    NATIVE_CELESTIAL_SOURCES[key] = (path, path)
for key in ("building-portraits-v1", "colony-panorama-v1"):
    path = f"assets/visual/planetary/{key}.png"
    NATIVE_CELESTIAL_SOURCES[key] = (path, path)
NATIVE_CELESTIAL_SOURCES["globe-credits"] = (
    "docs/IMMERSIVE_TEXTURE_PROVENANCE.md", "Licenses/Globe-texture-provenance.md")
NATIVE_CELESTIAL_SOURCES["planetary-credits"] = (
    "assets/visual/planetary/ARTWORK.md", "Licenses/Planetary-artwork.md")
NATIVE_CELESTIAL_SOURCES["earth-authored-map"] = ("assets/visual/sol/earth-map.png", "assets/visual/sol/earth-map.png")
for key in ("mars-map", "jupiter-map", "mercury-map", "venus-map", "moon-map", "saturn-map", "uranus-map", "neptune-map"):
    path = f"assets/visual/sol/{key}.png"
    NATIVE_CELESTIAL_SOURCES[key] = (path, path)
NATIVE_CELESTIAL_SOURCES["authored-planet-credits"] = (
    "docs/AUTHORED_PLANET_TEXTURE_PROVENANCE.md", "Licenses/Authored-planet-texture-provenance.md")


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
