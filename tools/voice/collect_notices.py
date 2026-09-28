"""Collect dependency licensing evidence for an installed voice pack."""
import importlib.metadata as metadata
import json
from pathlib import Path
import shutil
import sys

target = Path(sys.argv[1]).resolve()
target.mkdir(parents=True, exist_ok=True)
inventory = []
for distribution in metadata.distributions():
    name = distribution.metadata["Name"]
    directory = target / name
    directory.mkdir(exist_ok=True)
    inventory.append({"name": name, "version": distribution.version,
                      "license": distribution.metadata.get("License-Expression") or distribution.metadata.get("License"),
                      "projectUrls": distribution.metadata.get_all("Project-URL")})
    for relative in distribution.files or []:
        path = distribution.locate_file(relative)
        if path.is_file() and any(word in path.name.lower() for word in ("license", "licence", "copying", "notice")):
            shutil.copy2(path, directory / (str(relative).replace("/", "_").replace("\\", "_")))
    # num2words is LGPL; preserve replaceable original source, not just an attribution.
    if name.lower() == "num2words":
        shutil.copytree(distribution.locate_file("num2words"), directory / "source/num2words", dirs_exist_ok=True, ignore=shutil.ignore_patterns("__pycache__"))
(target / "dependency-inventory.json").write_text(json.dumps(inventory, indent=2), encoding="utf-8")
