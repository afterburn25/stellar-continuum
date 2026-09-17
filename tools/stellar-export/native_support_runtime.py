"""Runtime packaging proof for native support-bundle export."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

from native_client_runtime import _validate_capture


_PROOF_KEYS = {"mode", "menu_started", "key_started", "settings_blocked",
               "canonical_unchanged", "save_unchanged", "paused", "first",
               "second", "error"}
_BOOL_KEYS = {"menu_started", "key_started", "settings_blocked",
              "canonical_unchanged", "save_unchanged", "paused"}


def _no_duplicate_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate support proof key: {key}")
        result[key] = value
    return result


def support_proof(stdout: str, expected_mode: str) -> dict:
    markers = re.findall(r"^support=([^\r\n]*)\r?$", stdout, re.MULTILINE)
    if len(markers) != 1:
        raise RuntimeError("Native support proof is missing or duplicated")
    try:
        proof = json.loads(markers[0], object_pairs_hook=_no_duplicate_object)
    except (ValueError, TypeError) as error:
        raise RuntimeError("Native support proof is malformed") from error
    if (not isinstance(proof, dict) or set(proof) != _PROOF_KEYS or
            proof.get("mode") != expected_mode or
            any(type(proof.get(key)) is not bool or proof[key] is not True
                for key in _BOOL_KEYS) or
            any(type(proof.get(key)) is not str for key in ("first", "second", "error"))):
        raise RuntimeError("Native support replay did not satisfy its state contract")
    if expected_mode == "success":
        if not proof["first"] or not proof["second"] or proof["first"] == proof["second"] or proof["error"]:
            raise RuntimeError("Native support success proof has invalid bundle paths or error")
    elif expected_mode == "failure":
        if proof["first"] or proof["second"] or not proof["error"]:
            raise RuntimeError("Native support failure proof did not remain a clean failure")
    else:
        raise RuntimeError("Native support validator received an invalid expected mode")
    return proof


def _clean_environment(env: dict[str, str]) -> dict[str, str]:
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    return dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))


def _paused_payload(save: Path) -> dict:
    if not save.is_file():
        raise RuntimeError("Native support replay did not produce its final save")
    try:
        payload = json.loads(save.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise RuntimeError("Native support final save is not valid Player17 JSON") from error
    if (not isinstance(payload, dict) or payload.get("FormatVersion") != 17 or
            not payload.get("SavedAtUtc") or not isinstance(payload.get("Galaxy"), dict) or
            len(payload["Galaxy"].get("Systems", [])) != 500):
        raise RuntimeError("Native support final save is not a 500-system Player17 campaign")
    payload.pop("SavedAtUtc", None)
    return payload


def _contained_bundle(path_text: str, support_root: Path) -> Path:
    path = Path(path_text).resolve()
    root = support_root.resolve()
    try:
        relative = path.relative_to(root)
    except ValueError as error:
        raise RuntimeError("Native support bundle path escaped its isolated support directory") from error
    if len(relative.parts) != 2 or relative.name != "support.zip" or not path.is_file():
        raise RuntimeError("Native support bundle path has an invalid destination shape")
    return path


def _validate_bundle(path: Path, saved_bytes: bytes, width: int, height: int) -> None:
    try:
        with zipfile.ZipFile(path) as archive:
            infos = archive.infolist()
            names = [entry.filename for entry in infos]
            if names != ["session.log", "system.txt", "campaign.player17.json"]:
                raise RuntimeError("Native support bundle does not have the fixed diagnostic entries")
            if len({entry.filename for entry in infos}) != 3:
                raise RuntimeError("Native support bundle has duplicate entries")
            if any(entry.file_size > (64 * 1024 * 1024 if entry.filename == "campaign.player17.json"
                                      else 256 * 1024) for entry in infos):
                raise RuntimeError("Native support bundle exceeds diagnostic size bounds")
            content = {entry.filename: archive.read(entry) for entry in infos}
    except (OSError, zipfile.BadZipFile, RuntimeError) as error:
        raise RuntimeError("Native support bundle ZIP or CRC validation failed") from error
    if content["campaign.player17.json"] != saved_bytes:
        raise RuntimeError("Native support bundle save differs from the final save")
    metadata = content["system.txt"].decode("utf-8", errors="strict").splitlines()
    required = ("SaveIncluded=yes", "Runtime=native-c++23", "RendererBackend=vulkan",
                f"Viewport={width}x{height}")
    if any(marker not in metadata for marker in required):
        raise RuntimeError("Native support bundle metadata is incomplete")
    if b"Local diagnostic export requested." not in content["session.log"]:
        raise RuntimeError("Native support bundle lacks recent request log evidence")


def _run(folder: Path, work: Path, save: Path, capture: Path, width: int,
         height: int, mode_flag: str, env: dict[str, str]) -> subprocess.CompletedProcess:
    args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
            "--save-path", str(save), "--width", str(width), "--height", str(height),
            "--smoke", str(capture), mode_flag]
    if mode_flag == "--support-failure-check":
        args.append("--load")
    result = subprocess.run(args, cwd=work, env=env, capture_output=True, text=True,
                            timeout=90)
    if result.returncode:
        raise RuntimeError(f"Native support replay failed ({result.returncode}): {result.stderr}")
    if any(marker not in result.stdout for marker in ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")):
        raise RuntimeError("Native support replay did not confirm renderer, campaign and save")
    return result


def validate_native_support_export(folder: Path, env: dict[str, str]):
    folder = folder.resolve()
    with tempfile.TemporaryDirectory(prefix="stellar-native-support-") as temporary:
        work = Path(temporary)
        clean_env = _clean_environment(env)
        fresh_save, fresh_capture = work / "campaign.player17.json", work / "support-fresh.bmp"
        success = _run(folder, work, fresh_save, fresh_capture, 1280, 720,
                       "--support-check", clean_env)
        proof = support_proof(success.stdout, "success")
        _validate_capture(fresh_capture, 1280, 720)
        fresh_sidecar = fresh_capture.with_name(fresh_capture.stem + "-support.bmp")
        _validate_capture(fresh_sidecar, 1280, 720)
        first_payload = _paused_payload(fresh_save)
        saved_bytes = fresh_save.read_bytes()
        support_root = work / "support"
        first, second = (_contained_bundle(proof[key], support_root) for key in ("first", "second"))
        bundles = sorted(support_root.glob("*/support.zip"))
        if set(bundles) != {first, second} or len(bundles) != 2 or list(support_root.rglob("*.partial")):
            raise RuntimeError("Native support success left an incomplete or unexpected bundle")
        for bundle in bundles:
            _validate_bundle(bundle, saved_bytes, 1280, 720)

        reload_root = work / "reload"
        reload_root.mkdir()
        reload_save = reload_root / fresh_save.name
        shutil.copy2(fresh_save, reload_save)
        failure_support = reload_root / "support"
        failure_support.write_bytes(b"blocker")
        failure_capture = work / "support-failure.bmp"
        failure = _run(folder, work, reload_save, failure_capture, 1920, 1080,
                       "--support-failure-check", clean_env)
        support_proof(failure.stdout, "failure")
        _validate_capture(failure_capture, 1920, 1080)
        failure_sidecar = failure_capture.with_name(failure_capture.stem + "-support.bmp")
        _validate_capture(failure_sidecar, 1920, 1080)
        if (failure_support.read_bytes() != b"blocker" or list(reload_root.rglob("*.zip")) or
                list(reload_root.rglob("*.partial"))):
            raise RuntimeError("Native support failure modified its blocker or emitted a bundle")
        if _paused_payload(reload_save) != first_payload:
            raise RuntimeError("Native support paused reload changed the campaign")

        evidence_root = folder.parent
        prefix = folder.name + "-support"
        evidence = {
            "supportFreshCapture": evidence_root / (prefix + "-fresh.bmp"),
            "supportFreshPanel": evidence_root / (prefix + "-fresh-panel.bmp"),
            "supportFailureCapture": evidence_root / (prefix + "-failure.bmp"),
            "supportFailurePanel": evidence_root / (prefix + "-failure-panel.bmp"),
            "supportFirstBundle": evidence_root / (prefix + "-first.zip"),
            "supportSecondBundle": evidence_root / (prefix + "-second.zip"),
            "supportSuccessDiagnostics": evidence_root / (prefix + "-success.stdout.txt"),
            "supportFailureDiagnostics": evidence_root / (prefix + "-failure.stdout.txt"),
        }
        for source, key in ((fresh_capture, "supportFreshCapture"), (fresh_sidecar, "supportFreshPanel"),
                            (failure_capture, "supportFailureCapture"), (failure_sidecar, "supportFailurePanel"),
                            (first, "supportFirstBundle"), (second, "supportSecondBundle")):
            shutil.copy2(source, evidence[key])
        evidence["supportSuccessDiagnostics"].write_text(success.stdout, encoding="utf-8")
        evidence["supportFailureDiagnostics"].write_text(failure.stdout, encoding="utf-8")
        return {"nativeSupportExport": True, "nativeSupportFailure": True,
                "supportEvidence": {key: str(path) for key, path in evidence.items()},
                "supportDiagnostics": {"success": success.stdout.strip(), "failure": failure.stdout.strip()}}
