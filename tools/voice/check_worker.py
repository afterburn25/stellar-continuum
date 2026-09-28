"""Real offline worker regression checks; run with the installed pack interpreter."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import wave
from pack_paths import load_pack


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pack", type=Path, required=True)
    args = parser.parse_args()
    pack = load_pack(args.pack)
    with tempfile.TemporaryDirectory(prefix="stellar-voice-checks-") as directory:
        root = Path(directory)
        base = {"text": "Our fleet is ready. The journey begins when you give the order.",
                "voice": "af_heart", "speed": 1.0, "outputPath": str(root / "good.wav")}
        requests = [
            {**base, "id": "normal"},
            {**base, "id": "unknown-word", "text": "qzxqzvblorgg"},
            {**base, "id": "unknown-voice", "voice": "does_not_exist"},
            {**base, "id": "invalid-speed", "speed": 9},
            {**base, "id": "relative-output", "outputPath": "relative.wav"},
            {**base, "id": "length", "text": "x" * 4001},
            {**base, "id": "long", "text": ("Our ships are ready to begin their journey. The science team awaits your orders. " * 12), "outputPath": str(root / "long.wav")},
            {**base, "id": "recovery", "outputPath": str(root / "recovery.wav")},
        ]
        command = [pack["pythonPath"], "-u", pack["workerPath"], "--model", pack["modelPath"], "--voices", pack["voicesPath"]]
        run = subprocess.run(command, input="\n".join(json.dumps(r) for r in requests)+"\n", text=True, encoding="utf-8", capture_output=True, timeout=90, cwd=root)
        assert run.returncode == 0, run.stderr
        responses = [json.loads(line) for line in run.stdout.splitlines()]
        assert responses.pop(0)["ready"] is True
        assert len(responses) == len(requests)
        for request, response in zip(requests, responses):
            assert response["id"] == request["id"]
            expected = request["id"] in ("normal", "long", "recovery")
            assert response["ok"] is expected, response
            if not expected:
                assert "Traceback" in response["error"] and "CWD:" in response["error"] and "Model:" in response["error"]
        assert responses[-2]["chunks"] > 1, responses[-2]
        for name in ("good", "long", "recovery"):
            with wave.open(str(root / f"{name}.wav"), "rb") as wav:
                assert wav.getframerate() == 24000 and wav.getnchannels() == 1 and wav.getsampwidth() == 2
                assert wav.getnframes() > 24000
        assert not list(root.glob("*.tmp"))
        failed = subprocess.run([*command[:4], str(root / "missing.onnx"), *command[5:]], capture_output=True, text=True, encoding="utf-8", timeout=20)
        assert failed.returncode != 0 and "Traceback" in failed.stderr and "cwd=" in failed.stderr, failed
        print("PASS: normal speech, unknown-word/voice, speed/path/length errors, multi-window speech, recovery, PCM, temp cleanup and missing-model terminal failure")


if __name__ == "__main__":
    main()
