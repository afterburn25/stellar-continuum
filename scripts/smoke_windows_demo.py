#!/usr/bin/env python3
"""Hosted Windows CI only: verify the ZIP, run its real game once, then publish it."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tempfile
import zipfile

from package_windows_demo import EXE, sha256, validate_export
from validate_godot_smoke import validate_log

STARTUP_ARGUMENTS = ["--headless", "--audio-driver", "Dummy", "--", "--stellar-startup-smoke"]


def verify_archive(archive_path: Path) -> tuple[str, dict]:
    expected_digest = archive_path.with_suffix(".zip.sha256").read_text(encoding="utf-8").split()[0]
    if sha256(archive_path) != expected_digest:
        raise ValueError("Downloaded ZIP does not match its SHA-256 sidecar.")
    with zipfile.ZipFile(archive_path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError("Duplicate ZIP entries are not allowed.")
        for name in names:
            path = PurePosixPath(name)
            if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
                raise ValueError("Unsafe ZIP member path.")
        manifests = [name for name in names if name.endswith("/BUILD.json")]
        if len(manifests) != 1:
            raise ValueError("Expected exactly one build manifest.")
        manifest_name = manifests[0]
        prefix = manifest_name.removesuffix("BUILD.json")
        manifest = json.loads(archive.read(manifest_name))
        if manifest.get("entry_point") != EXE or manifest.get("platform") != "Windows x86_64":
            raise ValueError("Manifest must identify the canonical Windows game executable.")
        expected = {prefix + relative for relative in manifest["files"]} | {manifest_name}
        if set(names) != expected:
            raise ValueError("ZIP contents differ from the build manifest.")
        for relative, info in manifest["files"].items():
            content = archive.read(prefix + relative)
            if len(content) != info["bytes"] or hashlib.sha256(content).hexdigest() != info["sha256"]:
                raise ValueError(f"Downloaded file differs from the manifest: {relative}")
        return prefix, manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact_directory", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "win32" or os.environ.get("GITHUB_ACTIONS") != "true":
        print("Windows executable smoke is restricted to hosted Windows GitHub Actions.", file=sys.stderr)
        return 1
    args.log.parent.mkdir(parents=True, exist_ok=True)
    try:
        archives = list(args.artifact_directory.glob("*.zip"))
        if len(archives) != 1:
            raise ValueError("Expected exactly one Windows candidate ZIP.")
        archive_path = archives[0]
        prefix, manifest = verify_archive(archive_path)
        if manifest["git_commit"] != os.environ.get("GITHUB_SHA"):
            raise ValueError("Downloaded candidate does not match this workflow's exact commit.")
        with tempfile.TemporaryDirectory(prefix="stellar-demo-") as temp:
            extracted = Path(temp) / "package"
            with zipfile.ZipFile(archive_path) as archive:
                archive.extractall(extracted)
            package = extracted / prefix
            validate_export(package)
            environment = os.environ.copy()
            # Keep this CI smoke's saves/logs separate from the checkout and the candidate files.
            environment["APPDATA"] = str(Path(temp) / "profile")
            Path(environment["APPDATA"]).mkdir()
            command = [str((package / EXE).resolve()), *STARTUP_ARGUMENTS]
            process = subprocess.Popen(command, cwd=package, env=environment,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       text=True, encoding="utf-8", errors="replace",
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                output, _ = process.communicate(timeout=60)
            except subprocess.TimeoutExpired:
                process.kill()  # Only the exact child this smoke started; no process-name matching.
                output, _ = process.communicate()
                args.log.write_text(output + "\nERROR: Windows smoke exceeded 60 seconds.\n", encoding="utf-8")
                raise ValueError("Exported Windows startup timed out.")
            args.log.write_text(output, encoding="utf-8")
            failures = validate_log(output, require_runtime_ready=True)
            if process.returncode != 0 or failures:
                raise ValueError(f"Windows startup failed (exit {process.returncode}): {'; '.join(failures)}")

        manifest["validation"]["windows_binary_execution"] = "passed: actual exported x64 game in hosted Windows CI"
        manifest["validation"]["windows_startup_log_sha256"] = sha256(args.log)
        manifest["validation"]["windows_startup_command"] = [manifest["entry_point"], *STARTUP_ARGUMENTS]
        args.output.mkdir(parents=True, exist_ok=True)
        final_path = args.output / archive_path.name
        with zipfile.ZipFile(archive_path) as source, zipfile.ZipFile(
                final_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as final:
            for name in source.namelist():
                content = (json.dumps(manifest, indent=2) + "\n").encode("utf-8") if name == prefix + "BUILD.json" else source.read(name)
                final.writestr(name, content)
        final_path.with_suffix(".zip.sha256").write_text(
            f"{sha256(final_path)}  {final_path.name}\n", encoding="utf-8")
        verify_archive(final_path)
        print(f"Windows exported startup passed; final demo: {final_path}")
        return 0
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        message = f"Windows demo smoke failed: {error}"
        with args.log.open("a", encoding="utf-8") as log:
            log.write(message + "\n")
        print(message, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
