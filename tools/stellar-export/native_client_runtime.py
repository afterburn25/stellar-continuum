"""Pinned runtime packaging for the opt-in native galaxy preview."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


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
    # The application may import only its declared SDL dependency. SDL itself
    # must import only the separately reviewed Windows libraries.
    imports = inspect_dependencies(client, {"sdl3.dll"}, windows)
    if "sdl3.dll" not in {name.lower() for name in imports}:
        raise RuntimeError("Native client does not import its declared SDL runtime")
    library_imports = inspect_dependencies(library, set(), windows)
    files = {
        "stellar-continuum-native.exe": client,
        "SDL3.dll": library,
        "Licenses/SDL3-zlib.txt": license_file,
    }
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


def validate_native_client_export(folder: Path, env: dict[str, str]):
    # This is an explicit local GPU preview check, never part of a headless
    # preset. A working installed Vulkan graphics driver is required.
    with tempfile.TemporaryDirectory(prefix="stellar-native-client-") as temporary:
        work = Path(temporary)
        screenshot = work / "native-preview.bmp"
        save = work / "campaign.player17.json"
        clean_env = dict(env)
        system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
        clean_env["PATH"] = str(system_root / "System32") + os.pathsep + str(system_root)
        result = subprocess.run(
            [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
             "--save-path", str(save), "--smoke", str(screenshot)],
            cwd=work, env=clean_env, capture_output=True, text=True, timeout=60)
        if result.returncode != 0:
            raise RuntimeError(f"Relocated native client failed ({result.returncode}): {result.stderr}")
        if "gpu_driver=vulkan" not in result.stdout or "systems=500 " not in result.stdout:
            raise RuntimeError(f"Native client did not confirm its renderer/campaign: {result.stdout}")
        if not screenshot.is_file() or screenshot.stat().st_size < 54 or screenshot.read_bytes()[:2] != b"BM":
            raise RuntimeError("Native client did not capture its rendered frame")
        if not save.is_file() or save.stat().st_size == 0:
            raise RuntimeError("Native client did not complete its isolated manual save")
        before = json.loads(save.read_text(encoding="utf-8"))
        if before.get("FormatVersion") != 17 or len(before.get("Galaxy", {}).get("Systems", [])) != 500:
            raise RuntimeError("Native client save does not contain its Player17 500-system campaign")
        loaded = subprocess.run(
            [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
             "--save-path", str(save), "--load", "--smoke", str(screenshot)],
            cwd=work, env=clean_env, capture_output=True, text=True, timeout=60)
        if loaded.returncode != 0 or "gpu_driver=vulkan" not in loaded.stdout:
            raise RuntimeError(f"Relocated native saved campaign failed to load: {loaded.stderr}")
        after = json.loads(save.read_text(encoding="utf-8"))
        before.pop("SavedAtUtc", None)
        after.pop("SavedAtUtc", None)
        if before != after:
            raise RuntimeError("Native saved campaign changed during paused load/recapture")
        evidence = folder.parent / (folder.name + "-native-preview.bmp")
        shutil.copy2(screenshot, evidence)
        return {"nativeClientRelocatedLaunch": True, "nativeClientRestrictedPath": True,
                "nativeClientIsolatedManualSave": True,
                "nativeClientPlayer17Reload": True,
                "renderer": "Vulkan", "capture": str(evidence),
                "diagnostics": result.stdout.strip(), "loadDiagnostics": loaded.stdout.strip(),
                "graphicalParity": False}
