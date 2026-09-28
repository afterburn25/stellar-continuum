"""Build fixed native scientist cues from an already installed local voice pack.

Run using the pack's Python interpreter. No downloads, runtime TTS dependency,
echo, communications filter, or automatic export-hash approval is performed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import wave

from pack_paths import load_pack

LINES = {
    "scientist-reconnaissance": "Long-range telemetry is incomplete. Dispatch a scout vessel to chart this system before approach.",
    "scientist-research-report": "Our research team has a new report. Review the findings before selecting our next objective.",
    "scientist-survey-complete": "System survey complete. The updated observations are ready for your review.",
}
EXPECTED_MODEL = "7d5df8ecf7d4b1878015a32686053fd0eebe2bc377234608764cc0ef3636a6c5"
EXPECTED_VOICES = "bca610b8308e8d99f32e6fe4197e7ec01679264efed0cac9140fe9c29f1fbf7d"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    pack = load_pack(args.pack)
    for key, expected in (("modelPath", EXPECTED_MODEL), ("voicesPath", EXPECTED_VOICES)):
        if digest(pack[key]) != expected:
            raise ValueError(f"Installed {key} differs from the reviewed Kokoro v1.0 pack")
    # The repository worker also disables network access and requires a locally
    # installed British G2P pipeline. No fallback voice is accepted.
    from kokoro_worker import Synthesizer
    synth = Synthesizer(pack["modelPath"], pack["voicesPath"])
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    records = []
    for key, text in LINES.items():
        path = output / (key + ".wav")
        metrics = synth.synthesize(text, "bf_emma", 1.0, str(path))
        with wave.open(str(path), "rb") as audio:
            seconds = audio.getnframes() / audio.getframerate()
            if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate()) != (1, 2, 24000):
                raise ValueError(f"Unexpected PCM format: {path}")
            if not 0.5 <= seconds <= 20 or not .001 < metrics["rms"] < .5:
                raise ValueError(f"Invalid or silent speech: {path}")
        records.append(dict(cue=key, file=path.name, text=text, sha256=digest(path),
                            durationSeconds=seconds, **metrics))
        print(f"Generated {path.name}: {seconds:.2f}s", flush=True)
    manifest = dict(schemaVersion=1, profile="human_female_chief_scientist",
                    locale="en-GB", voice="bf_emma", speed=1.0, synthetic=True,
                    model="Kokoro-82M-v1.0", modelSha256=EXPECTED_MODEL,
                    voicesSha256=EXPECTED_VOICES,
                    workerSha256=digest(Path(__file__).with_name("kokoro_worker.py")),
                    processing="Dry mono PCM; no reverb, echo or communications filter",
                    clips=records)
    (output / "scientist-cues.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
