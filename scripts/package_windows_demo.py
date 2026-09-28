#!/usr/bin/env python3
"""Validate and zip the Windows export without executing Windows binaries."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import zipfile


ROOT = Path(__file__).resolve().parents[1]
ENGINE_VERSION = "4.7.2"
RELEASE_URL = "https://github.com/godotengine/godot/releases/tag/4.7.2-stable"
EDITOR_SHA256 = "129f82db7bafd54ae14bb5bb284041c73860e8c7a009a3a026ca5e946cbff247"
TEMPLATES_SHA256 = "92f8681e349ef1f90891b792da95e3b2b0bd1ed610b78018c58feb2d87e15a9d"
EXE = "StellarContinuum.exe"
PCK = "StellarContinuum.pck"
PAYLOAD = "data_Game_windows_x86_64"
REQUIRED_MANAGED = ("Game.dll", "Game.runtimeconfig.json", "GodotSharp.dll",
                    "System.Private.CoreLib.dll", "coreclr.dll", "hostfxr.dll")


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def validate_export(directory: Path) -> None:
    executable = directory / EXE
    with executable.open("rb") as stream:
        header = stream.read(64)
        if len(header) < 64 or header[:2] != b"MZ":
            raise ValueError("Windows executable is missing its PE header.")
        stream.seek(struct.unpack_from("<I", header, 60)[0])
        pe = stream.read(6)
        if pe != b"PE\0\0\x64\x86":
            raise ValueError("Windows executable must be a PE x86_64 binary.")
    with (directory / PCK).open("rb") as stream:
        if stream.read(4) != b"GDPC":
            raise ValueError("Godot resource pack is missing or invalid.")

    payload = directory / PAYLOAD
    for name in REQUIRED_MANAGED:
        path = payload / name
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"Self-contained .NET export is incomplete: {path}")
    if not (directory / "data/research/v1/index.json").is_file():
        raise ValueError("Public research catalog is missing from the distributable.")
    for path in directory.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"Symlink is not permitted in the demo package: {path}")


def package(directory: Path, output: Path, revision: str, run_url: str) -> Path:
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("A full lowercase Git commit SHA is required.")
    if not re.fullmatch(r"https://github\.com/[\w.-]+/[\w.-]+/actions/runs/\d+", run_url):
        raise ValueError("An exact GitHub Actions run URL is required.")
    validate_export(directory)
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"[0-9A-Za-z.+-]+", version):
        raise ValueError("VERSION is not safe for a demo filename.")

    shutil.copyfile(ROOT / "docs/WINDOWS_DEMO_README.txt", directory / "README.txt")
    shutil.copyfile(ROOT / "docs/SOL_VISUAL_SOURCES.md", directory / "PLANET_IMAGE_CREDITS.md")
    shutil.copyfile(ROOT / "data/astronomy/README.md", directory / "STAR_CATALOGUE_CREDITS.md")
    shutil.copyfile(ROOT / "docs/CINEMATIC_MAP_AND_SURFACE.md", directory / "MAP_AND_SURFACE_GUIDE.md")
    shutil.copyfile(ROOT / "docs/GAME_MODES.md", directory / "GAME_MODES.md")
    (directory / "VERSION").write_text(version + "\n", encoding="utf-8")
    files = {
        path.relative_to(directory).as_posix(): {"sha256": sha256(path), "bytes": path.stat().st_size}
        for path in sorted(directory.rglob("*")) if path.is_file() and path.name != "BUILD.json"
    }
    manifest = {
        "product": "Stellar Continuum development build",
        "version": version,
        "git_commit": revision,
        "workflow_run": run_url,
        "platform": "Windows x86_64",
        "entry_point": EXE,
        "configuration": "ExportRelease",
        "self_contained_dotnet": True,
        "godot_version": ENGINE_VERSION + ".stable.mono",
        "godot_release": RELEASE_URL,
        "godot_editor_sha256": EDITOR_SHA256,
        "godot_export_templates_sha256": TEMPLATES_SHA256,
        "validation": {
            "source_startup": "passed in Linux CI",
            "linux_export_startup": "passed outside the source tree",
            "windows_binary_execution": "not performed yet; candidate awaits hosted Windows smoke",
        },
        "files": files,
    }
    (directory / "BUILD.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    output.mkdir(parents=True, exist_ok=True)
    name = f"StellarContinuum-{version}-windows-x64-{revision[:12]}"
    archive_path = output / (name + ".zip")
    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(directory.rglob("*")):
            if path.is_file():
                archive.write(path, name + "/" + path.relative_to(directory).as_posix())
    with zipfile.ZipFile(archive_path) as archive:
        if archive.testzip() is not None:
            raise ValueError("Windows demo ZIP failed its integrity check.")
        for relative, info in files.items():
            actual = hashlib.sha256(archive.read(name + "/" + relative)).hexdigest()
            if actual != info["sha256"]:
                raise ValueError(f"Packaged file checksum mismatch: {relative}")
    archive_path.with_suffix(".zip.sha256").write_text(
        f"{sha256(archive_path)}  {archive_path.name}\n", encoding="utf-8")
    return archive_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("export_directory", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--run-url", required=True)
    args = parser.parse_args()
    try:
        archive = package(args.export_directory, args.output, args.revision, args.run_url)
        print(f"Windows demo packaged: {archive}")
        return 0
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"Windows demo packaging failed: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
