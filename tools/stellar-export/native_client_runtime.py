"""Pinned runtime packaging for the opt-in native galaxy preview."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import struct

from native_ui_runtime import native_ui_asset_files
from native_celestial_runtime import native_celestial_asset_files
from native_small_body_runtime import native_small_body_asset_files
from native_planet_runtime import native_planet_asset_files
from native_environment_runtime import native_environment_asset_files
from native_species_runtime import native_species_asset_files
from native_startup_art_runtime import native_startup_art_asset_files
from native_galaxy_art_runtime import native_galaxy_art_asset_files
from native_leader_art_runtime import native_leader_art_asset_files
from native_ship_art_runtime import native_ship_art_asset_files
from native_audio_assets import native_audio_asset_files
from native_navigation_assets import native_navigation_asset_files
from native_research_assets import native_research_asset_files
from native_voice_runtime import native_voice_asset_files


def _verified_file(path: Path, expected_hash: str) -> Path:
    if not path.is_file():
        raise RuntimeError(f"Missing native client dependency: {path}")
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected_hash:
        raise RuntimeError(f"Native client dependency differs from reviewed SDL release: {path}")
    return path


def copy_native_client_runtime(root, build, output, inspect_dependencies):
    lock = json.loads((root / "third_party/SDL3/runtime-lock.json").read_text(encoding="utf-8"))
    client = build / "stellar-continuum-native.exe"
    if not client.is_file():
        raise RuntimeError(f"Native client executable is missing: {client}")
    dependency_root = build / "_deps/stellar-sdl3" / lock["archiveRoot"]
    library = _verified_file(build / "SDL3.dll", lock["runtime"]["sha256"])
    license_file = _verified_file(dependency_root / lock["license"]["path"], lock["license"]["sha256"])
    windows = set(lock["windowsImports"])
    # Media Foundation is the application's Windows audio decoder. Keep these
    # reviewed system imports separate from the pinned SDL dependency policy.
    audio_windows = {"mfplat.dll", "mfreadwrite.dll"}
    imports = inspect_dependencies(client, {"sdl3.dll"}, windows | audio_windows)
    if "sdl3.dll" not in {name.lower() for name in imports}:
        raise RuntimeError("Native client does not import its declared SDL runtime")
    library_imports = inspect_dependencies(library, set(), windows)
    files = {
        "stellar-continuum-native.exe": client,
        "SDL3.dll": library,
        "Licenses/SDL3-zlib.txt": license_file,
    }
    files.update(native_ui_asset_files(root))
    files.update(native_celestial_asset_files(root))
    files.update(native_small_body_asset_files(root))
    files.update(native_planet_asset_files(root))
    files.update(native_environment_asset_files(root))
    files.update(native_species_asset_files(root))
    files.update(native_startup_art_asset_files(root))
    files.update(native_galaxy_art_asset_files(root))
    files.update(native_leader_art_asset_files(root))
    files.update(native_ship_art_asset_files(root))
    files.update(native_audio_asset_files(root))
    files.update(native_navigation_asset_files(root))
    files.update(native_research_asset_files(root))
    stellar_manifest=root / "assets/visual/stellar/manifest.json"
    stellar_art=json.loads(stellar_manifest.read_text(encoding="utf-8"))
    if stellar_art.get("version") != 1:
        raise RuntimeError("Unsupported stellar artwork manifest version")
    declarations=stellar_art["files"]
    names=[item["filename"] for item in declarations]
    used={name for item in stellar_art["objects"] for name in
          (item["closeAsset"],item["distanceAsset"]) if name}
    types=[item["objectType"] for item in stellar_art["objects"]]
    definitions=json.loads((root/"data/stellar/population-v1.json").read_text(encoding="utf-8"))
    expected_types={item["id"] for item in definitions["objects"]}|{"m-red-dwarf-quiet","central-supermassive-black-hole"}
    if len(names)!=len(set(names)) or set(names)!=used or len(types)!=len(set(types)) or set(types)!=expected_types:
        raise RuntimeError("Missing or duplicate stellar artwork mapping")
    for item in stellar_art["files"]:
        name=item["filename"]
        if Path(name).name!=name: raise RuntimeError("Unsafe stellar artwork filename")
        files["assets/visual/stellar/"+name]=_verified_file(stellar_manifest.parent/name,item["sha256"])
    files["assets/visual/stellar/manifest.json"]=stellar_manifest
    eruption_root=root/"assets/visual/stellar-eruptions"
    eruption_manifest=json.loads((eruption_root/"manifest.json").read_text(encoding="utf-8"))
    if eruption_manifest.get("schemaVersion")!=1 or len(eruption_manifest["visualSets"])!=204:
        raise RuntimeError("Incomplete stellar eruption sequence registry")
    files["assets/visual/stellar-eruptions/manifest.json"]=eruption_root/"manifest.json"
    for sequence in eruption_manifest["visualSets"]:
        for name in sequence["textures"]:
            path=Path(name)
            if path.is_absolute() or '..' in path.parts:raise RuntimeError("Unsafe eruption texture path")
            for size in (256,512,1024):
                relative=f"assets/visual/stellar-eruptions/{size}/{path.as_posix()}"
                source=eruption_root/str(size)/path
                if not source.is_file():raise RuntimeError(f"Missing eruption image: {source}")
                files[relative]=source
    files["Data/stellar/stellar-activity-v1.json"]=root/"data/stellar/stellar-activity-v1.json"
    files["Data/stellar/population-v1.json"]=root/"data/stellar/population-v1.json"
    files["Data/stellar/population-profiles-v1.json"]=root/"data/stellar/population-profiles-v1.json"
    files["Licenses/Stellar-artwork.md"]=root/"docs/stellar-asset-validation.md"
    files["Documentation/Stellar-generation.md"]=root/"docs/stellar-generation-validation.md"
    files["Documentation/Stellar-population-profiles.md"]=root/"docs/stellar-population-profiles.md"
    files.update(native_voice_asset_files(root))
    for relative, source in files.items():
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    return {
        "entryPoint": "stellar-continuum-native.exe",
        "renderer": "Vulkan",
        "mode": "native-galaxy-preview",
        "graphicalParity": False,
        "requiredFiles": list(files),
        "windowsImports": sorted({name for name in imports + library_imports if name.lower() != "sdl3.dll"}),
        "runtimeDependencies": [{"name": "SDL3.dll", "version": lock["version"],
                                 "sha256": lock["runtime"]["sha256"],
                                 "source": lock["releaseUrl"], "license": "Licenses/SDL3-zlib.txt"}],
    }

def _validate_capture(path: Path, width: int, height: int):
    data = path.read_bytes() if path.is_file() else b""
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError("Native client did not capture its rendered frame")
    declared = struct.unpack_from("<I", data, 2)[0]
    offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    actual_width, actual_height, planes, bits = struct.unpack_from("<iiHH", data, 18)
    compression = struct.unpack_from("<I", data, 30)[0]
    row = ((actual_width * bits + 31) // 32) * 4 if actual_width > 0 else 0
    required = row * abs(actual_height)
    if (declared != len(data) or dib_size not in (40, 108, 124) or
            actual_width != width or abs(actual_height) != height or
            planes != 1 or bits not in (24, 32) or compression not in (0, 3) or
            offset < 14 + dib_size or required <= 0 or offset + required > len(data)):
        raise RuntimeError("Native client capture has invalid renderer geometry")
    pixel_width = actual_width * (bits // 8)
    row_stride = ((actual_width * bits + 31) // 32) * 4
    if pixel_width <= 0 or offset + pixel_width > len(data):
        raise RuntimeError("Native client capture has invalid renderer geometry")
    first_pixel = data[offset:offset + bits // 8]
    uniform_row = first_pixel * actual_width
    if all(data[offset + index * row_stride:offset + index * row_stride + pixel_width]
           == uniform_row for index in range(abs(actual_height))):
        raise RuntimeError("Native client capture contains no rendered variation")


def validate_native_client_export(folder: Path, env: dict[str, str]):
    folder = folder.resolve()
    # This is an explicit local GPU preview check, never part of a headless
    # preset. A working installed Vulkan graphics driver is required.
    with tempfile.TemporaryDirectory(prefix="stellar-native-client-") as temporary:
        work = Path(temporary)
        screenshot = work / "native-preview-1280x720.bmp"
        loaded_screenshot = work / "native-preview-1920x1080.bmp"
        save = work / "campaign.player17.json"
        clean_env = dict(env)
        system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
        clean_env["PATH"] = str(system_root / "System32") + os.pathsep + str(system_root)
        result = subprocess.run(
            [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
             "--save-path", str(save), "--width", "1280", "--height", "720",
             "--smoke", str(screenshot)],
            cwd=work, env=clean_env, capture_output=True, text=True, timeout=60)
        if result.returncode != 0:
            raise RuntimeError(f"Relocated native client failed ({result.returncode}): {result.stderr}")
        if "gpu_driver=vulkan" not in result.stdout or "systems=500 " not in result.stdout:
            raise RuntimeError(f"Native client did not confirm its renderer/campaign: {result.stdout}")
        _validate_capture(screenshot, 1280, 720)
        if not save.is_file() or save.stat().st_size == 0:
            raise RuntimeError("Native client did not complete its isolated manual save")
        before = json.loads(save.read_text(encoding="utf-8"))
        if before.get("FormatVersion") != 17 or len(before.get("Galaxy", {}).get("Systems", [])) != 500:
            raise RuntimeError("Native client save does not contain its Player17 500-system campaign")
        loaded = subprocess.run(
            [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
             "--save-path", str(save), "--width", "1920", "--height", "1080",
             "--load", "--smoke", str(loaded_screenshot)],
            cwd=work, env=clean_env, capture_output=True, text=True, timeout=60)
        if loaded.returncode != 0 or "gpu_driver=vulkan" not in loaded.stdout:
            raise RuntimeError(f"Relocated native saved campaign failed to load: {loaded.stderr}")
        _validate_capture(loaded_screenshot, 1920, 1080)
        after = json.loads(save.read_text(encoding="utf-8"))
        before.pop("SavedAtUtc", None)
        after.pop("SavedAtUtc", None)
        if before != after:
            raise RuntimeError("Native saved campaign changed during paused load/recapture")
        evidence = folder.parent / (folder.name + "-native-preview.bmp")
        shutil.copy2(loaded_screenshot, evidence)
        fresh_evidence = folder.parent / (folder.name + "-native-preview-fresh.bmp")
        shutil.copy2(screenshot, fresh_evidence)
        return {"nativeClientRelocatedLaunch": True, "nativeClientRestrictedPath": True,
                "nativeClientIsolatedManualSave": True,
                "nativeClientPlayer17Reload": True,
                "renderer": "Vulkan", "capture": str(evidence), "freshCapture": str(fresh_evidence),
                "diagnostics": result.stdout.strip(), "loadDiagnostics": loaded.stdout.strip(),
                "graphicalParity": False}
