"""Reviewed Adaptive Research runtime-data declaration and copy helpers."""

from __future__ import annotations

import json
from pathlib import Path, PurePosixPath
import shutil


def _is_linked(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    return path.is_symlink() or (is_junction is not None and is_junction())


def load_research_runtime_files(repository: Path, declaration: Path) -> list[tuple[str, str]]:
    repository = repository.resolve()
    try:
        document = json.loads(declaration.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"Cannot read Adaptive Research runtime-data declaration {declaration}: {error}") from error
    if not isinstance(document, dict):
        raise RuntimeError("Adaptive Research runtime-data declaration must be an object")
    if type(document.get("schemaVersion")) is not int or document["schemaVersion"] != 1:
        raise RuntimeError("Unsupported Adaptive Research runtime-data declaration schema")
    if document.get("root") != "data/research/v1" or document.get("destination") != "Data/research/v1":
        raise RuntimeError("Adaptive Research runtime-data roots do not match the reviewed layout")
    names = document.get("files")
    if not isinstance(names, list) or not names or any(not isinstance(name, str) for name in names):
        raise RuntimeError("Adaptive Research runtime-data files must be a nonempty string array")
    if names != sorted(names) or len(names) != len({name.casefold() for name in names}):
        raise RuntimeError("Adaptive Research runtime-data files must be unique and sorted")

    source_root = repository / document["root"]
    declared: list[tuple[str, str]] = []
    for name in names:
        components = name.split("/")
        relative = PurePosixPath(name)
        if (relative.is_absolute() or any(part in ("", ".", "..") for part in components) or
                relative.suffix != ".json" or "\\" in name or ":" in name or
                any(character in name for character in ";$<>") or
                any(ord(character) < 32 or ord(character) == 127 for character in name)):
            raise RuntimeError(f"Unsafe Adaptive Research runtime-data path: {name}")
        source = source_root.joinpath(*relative.parts)
        ancestry = [source, *source.parents]
        if (not source.is_file() or
                any(_is_linked(path)
                    for path in ancestry if path == source_root or path.is_relative_to(source_root)) or
                source.absolute() != source.resolve() or
                not source.resolve().is_relative_to(source_root.resolve())):
            raise RuntimeError(f"Missing or linked Adaptive Research runtime data: {source}")
        declared.append((f"Data/research/v1/{name}", f"data/research/v1/{name}"))

    canonical = sorted(path.relative_to(source_root).as_posix()
                       for path in source_root.rglob("*.json") if path.is_file())
    if names != canonical:
        missing = sorted(set(canonical) - set(names))
        extra = sorted(set(names) - set(canonical))
        raise RuntimeError(
            "Adaptive Research runtime-data declaration differs from canonical JSON inventory"
            f"; undeclared={missing}; nonexistent={extra}")
    return declared


def copy_research_runtime_files(repository: Path, output: Path,
                                declaration: Path) -> list[str]:
    declared = load_research_runtime_files(repository, declaration)
    required: list[str] = []
    for destination_name, source_name in declared:
        destination = output / destination_name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(repository / source_name, destination)
        required.append(destination_name)
    return required
