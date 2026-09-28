"""Resolve installed voice-pack resources independently of the caller's directory."""
import json
import os
from pathlib import Path


def load_pack(manifest):
    manifest = Path(manifest).resolve(strict=True)
    pack = json.loads(manifest.read_text(encoding="utf-8-sig"))
    defaults = {
        "pythonPath": "python/Scripts/python.exe" if os.name == "nt" else "python/bin/python",
        "workerPath": "kokoro_worker.py",
        "modelPath": "kokoro-v1.0.onnx",
        "voicesPath": "voices-v1.0.bin",
    }
    for key, relative in defaults.items():
        local = manifest.parent / relative
        configured = Path(pack[key])
        if not configured.is_absolute():
            configured = manifest.parent / configured
        # Match game discovery: prefer the installed pack over stale installer-host paths.
        pack[key] = str((local if local.is_file() else configured).resolve(strict=True))
    return pack
