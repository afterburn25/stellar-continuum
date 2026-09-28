"""Capture the real diplomacy workspace in an isolated, visible Godot game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

VSYNC_WARNING = "WARNING: Could not set V-Sync mode, as changing V-Sync mode is not supported by the graphics driver."


def classify_native_stderr(stderr: str) -> tuple[str, int]:
    """Return fatal stderr and the count of documented benign driver warnings."""
    lines = stderr.splitlines()
    accepted = sum(line.strip() == VSYNC_WARNING for line in lines)
    fatal = []
    for line in lines:
        if line.strip() == VSYNC_WARNING:
            continue
        # Godot's follow-up source-location line belongs to the accepted warning only.
        if accepted and line.strip() == "at: set_use_vsync (platform/linuxbsd/x11/gl_manager_x11.cpp:372)":
            accepted -= 1
            continue
        fatal.append(line)
    return "\n".join(fatal).strip(), sum(line.strip() == VSYNC_WARNING for line in lines)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--godot", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--resolution", choices=["1280x720", "1920x1080"], default="1920x1080")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    proof = Path(args.output).resolve()
    if proof.exists() and (proof / "stdout.log").exists():
        raise ValueError("Choose a fresh output directory; previous evidence is retained.")
    dotnet = shutil.which("dotnet")
    if not dotnet:
        raise RuntimeError(".NET is missing; no game was launched.")
    runtime = subprocess.run([dotnet, "--list-runtimes"], capture_output=True, text=True, timeout=15)
    if runtime.returncode or "Microsoft.NETCore.App 8." not in runtime.stdout:
        raise RuntimeError(".NET 8 runtime preflight failed; no game was launched.")
    for part in ["Roaming", "Local"]:
        (proof / "profile" / part).mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["APPDATA"] = str(proof / "profile" / "Roaming")
    env["LOCALAPPDATA"] = str(proof / "profile" / "Local")
    env["XDG_DATA_HOME"] = str(proof / "profile" / "xdg")
    env["STELLAR_SCREENSHOT_DIR"] = str(proof)
    env["STELLAR_CAPTURE_SHA"] = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    env["STELLAR_CAPTURE_RESOLUTION"] = args.resolution
    diff = subprocess.check_output(["git", "diff", "HEAD", "--", "src", "tools", "assets", "project.godot"], cwd=repo)
    (proof / "source-diff.patch").write_bytes(diff)
    command = [args.godot, "--path", str(repo), "--windowed", "--resolution", args.resolution, "--position", "70,70",
               "--rendering-method", "gl_compatibility", "--audio-driver", "Dummy", "res://tools/DiplomacyCapture.tscn"]
    with (proof / "stdout.log").open("w", encoding="utf-8") as out, (proof / "stderr.log").open("w", encoding="utf-8") as err:
        result = subprocess.run(command, cwd=repo, env=env, stdout=out, stderr=err, timeout=180,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    (proof / "exit-code.txt").write_text(str(result.returncode), encoding="utf-8")
    if result.returncode:
        print((proof / "stderr.log").read_text(encoding="utf-8", errors="replace")[-5000:])
        return result.returncode
    raw_stderr = (proof / "stderr.log").read_text(encoding="utf-8", errors="replace")
    stderr, accepted_warning_count = classify_native_stderr(raw_stderr)
    if stderr:
        raise RuntimeError("Native stderr contains unhandled output: " + stderr[:1500])
    manifest = json.loads((proof / "manifest.json").read_text(encoding="utf-8"))
    expected = tuple(map(int, args.resolution.split("x")))
    if len(manifest["captures"]) != 23:
        raise RuntimeError("Expected 19 flow screenshots and four species framing screenshots.")
    for capture in manifest["captures"]:
        if (capture["width"], capture["height"]) != expected:
            raise RuntimeError(f"{capture['name']}: captured {capture['width']}x{capture['height']}, "
                               f"expected {expected[0]}x{expected[1]} native pixels.")
        data = (proof / (capture["name"] + ".png")).read_bytes()
        if hashlib.sha256(data).hexdigest().upper() != capture["sha256"]:
            raise RuntimeError("Screenshot digest mismatch.")
    print(f"PASS {args.resolution}: 23 captures, {len(manifest['checks'])} checks, exit 0, "
          f"raw stderr retained; accepted documented V-Sync warnings={accepted_warning_count}.")
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"{type(error).__name__}: {error}", file=sys.stderr)
        raise SystemExit(1)
