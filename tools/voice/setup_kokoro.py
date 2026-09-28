"""Explicit, repeatable installation of an optional local voice pack (Python 3.12).

Network access is used only here. The game never invokes this installer.
Run: py -3.12 tools/voice/setup_kokoro.py
"""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import traceback
import urllib.request
import venv

MODELS = {
    "kokoro-v1.0.onnx": "7d5df8ecf7d4b1878015a32686053fd0eebe2bc377234608764cc0ef3636a6c5",
    "voices-v1.0.bin": "bca610b8308e8d99f32e6fe4197e7ec01679264efed0cac9140fe9c29f1fbf7d",
}
RELEASE = "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.0/"


def digest(path):
    with open(path, "rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def fetch(url, target, sha256=None):
    if target.exists() and sha256 and digest(target) == sha256:
        return
    temporary = target.with_suffix(target.suffix + ".download")
    try:
        with urllib.request.urlopen(url, timeout=120) as response, temporary.open("wb") as output:
            shutil.copyfileobj(response, output)
        if sha256 and digest(temporary) != sha256:
            raise ValueError(f"Checksum mismatch: {url}")
        os.replace(temporary, target)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser()
    default = Path(os.environ.get("LOCALAPPDATA", Path.home() / ".local/share")) / "StellarContinuum/voice-packs/kokoro-v1"
    parser.add_argument("--target", type=Path, default=default)
    args = parser.parse_args()
    if sys.version_info[:2] != (3, 12):
        raise RuntimeError("This voice pack is validated on Python 3.12; invoke setup with that interpreter")
    target = args.target.resolve()
    target.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parent
    python = target / ("python/Scripts/python.exe" if os.name == "nt" else "python/bin/python")
    if not python.exists():
        venv.create(target / "python", with_pip=True)
    subprocess.run([str(python), "-m", "pip", "install", "--disable-pip-version-check", "-r", str(source / "requirements-lock.txt")], check=True)
    for name, checksum in MODELS.items():
        print(f"Verifying {name}", flush=True)
        fetch(RELEASE + name, target / name, checksum)
    for name in ("kokoro_worker.py", "kokoro-vocab.json", "pronunciations.json", "THIRD_PARTY.md", "requirements-lock.txt"):
        shutil.copy2(source / name, target / name)
    notices = target / "notices"
    notices.mkdir(exist_ok=True)
    shutil.copytree(source / "licenses", notices / "model", dirs_exist_ok=True)
    # Preserve installed dependency notices and full pure-Python LGPL library source.
    subprocess.run([str(python), str(source / "collect_notices.py"), str(notices)], check=True)
    voices = ["af_heart", "af_bella", "af_kore", "af_sarah", "af_aoede", "af_nova", "af_nicole", "bf_emma", "bf_isabella", "am_fenrir", "am_michael", "am_puck", "bm_george"]
    processing_hash = hashlib.sha256("".join(digest(target / name) for name in
        ("kokoro_worker.py", "kokoro-vocab.json", "pronunciations.json", "requirements-lock.txt")).encode()).hexdigest()[:16]
    manifest = {"schemaVersion": 1,
                "pythonPath": "python/Scripts/python.exe" if os.name == "nt" else "python/bin/python",
                "workerPath": "kokoro_worker.py", "modelPath": "kokoro-v1.0.onnx", "voicesPath": "voices-v1.0.bin",
                "modelSha256": MODELS["kokoro-v1.0.onnx"], "voicesSha256": MODELS["voices-v1.0.bin"],
                "version": "kokoro-1.0-fp32-misaki-0.9.4-stellar-1-" + processing_hash, "voices": voices}
    # A real smoke synthesis must succeed before publishing a discoverable manifest.
    request = {"id": "setup", "text": "Voice pack ready. Our ships are prepared to begin their journey.",
               "voice": "af_heart", "speed": 1.0, "outputPath": str(target / "setup-proof.wav")}
    run = subprocess.run([str(python), "-u", str(target / "kokoro_worker.py"), "--model", manifest["modelPath"], "--voices", manifest["voicesPath"]],
                         input=json.dumps(request) + "\n", text=True, encoding="utf-8", capture_output=True, timeout=90, cwd=target)
    responses = [json.loads(line) for line in run.stdout.splitlines()]
    if run.returncode or len(responses) != 2 or not responses[-1].get("ok"):
        raise RuntimeError(f"Voice smoke test failed: {run.stdout}\n{run.stderr}")
    temporary = target / "pack.json.tmp"
    temporary.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    os.replace(temporary, target / "pack.json")
    print(f"Installed and verified offline voice pack: {target / 'pack.json'}")
    print("This installation uses your Python 3.12 runtime. It is not a portable game redistributable.")


if __name__ == "__main__":
    try:
        main()
    except Exception:
        traceback.print_exc()
        print(f"Voice setup failed; cwd={os.getcwd()}", file=sys.stderr)
        sys.exit(1)
