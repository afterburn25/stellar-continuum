"""Offline Kokoro worker. JSONL protocol; import Synthesizer for audition tooling.

Direct ONNX input convention follows kokoro-onnx (MIT; see THIRD_PARTY.md).
No eSpeak, cloud service, shell, or runtime download is used.
"""
import argparse
import contextlib
import json
import math
import os
from pathlib import Path
import socket
import sys
import traceback
import wave


def deny_network(*args, **kwargs):
    raise RuntimeError("Speech inference is offline; install missing assets with setup_kokoro.py")


class Synthesizer:
    def __init__(self, model, voices):
        # Misaki can otherwise attempt a spaCy download if its model is absent.
        socket.socket.connect = deny_network
        socket.create_connection = deny_network
        import importlib.util
        if importlib.util.find_spec("en_core_web_sm") is None:
            raise RuntimeError("Missing local en_core_web_sm pronunciation model; rerun voice pack setup")
        import numpy as np
        import onnxruntime as ort
        from misaki import en
        self.np = np
        self.model_path = str(Path(model).resolve(strict=True))
        self.voices_path = str(Path(voices).resolve(strict=True))
        self.voices = np.load(self.voices_path, allow_pickle=False)
        self.vocab = json.loads(Path(__file__).with_name("kokoro-vocab.json").read_text(encoding="utf-8"))
        self.g2p = {False: en.G2P(trf=False, british=False, fallback=None),
                    True: en.G2P(trf=False, british=True, fallback=None)}
        pronunciations = json.loads(Path(__file__).with_name("pronunciations.json").read_text(encoding="utf-8"))
        for engine in self.g2p.values():
            engine.lexicon.golds.update(pronunciations)
        opts = ort.SessionOptions()
        opts.intra_op_num_threads = min(4, os.cpu_count() or 2)
        opts.inter_op_num_threads = 1
        self.session = ort.InferenceSession(self.model_path, opts, providers=["CPUExecutionProvider"])

    def synthesize(self, text, voice, speed, output_path):
        np = self.np
        if not isinstance(text, str) or not text.strip() or len(text) > 4000:
            raise ValueError("Speech text must contain 1 to 4000 characters")
        if not isinstance(voice, str) or voice not in self.voices or not voice.startswith(("af_", "am_", "bf_", "bm_")):
            raise ValueError(f"Unsupported English voice: {voice!r}")
        if isinstance(speed, bool) or not isinstance(speed, (int, float)) or not math.isfinite(speed) or not .7 <= speed <= 1.35:
            raise ValueError("Speech speed must be finite and between 0.7 and 1.35")
        if not isinstance(output_path, str) or not Path(output_path).is_absolute():
            raise ValueError("outputPath must be an absolute filesystem path")
        output = Path(output_path)
        if output.suffix.lower() != ".wav":
            raise ValueError("outputPath must end in .wav")
        # preprocess=False prevents dialogue being interpreted as Misaki markup.
        phonemes, words = self.g2p[voice.startswith("b")](text, preprocess=False)
        unknown = [w.text for w in words if w.phonemes is None or "❓" in w.phonemes]
        if unknown or "❓" in phonemes:
            raise ValueError("Unknown pronunciation for " + ", ".join(unknown) + "; add a voice pronunciation override")
        phonemes = " ".join(phonemes.split())
        unsupported = sorted(set(phonemes) - self.vocab.keys())
        if unsupported:
            raise ValueError(f"Unsupported pronunciation symbols: {unsupported!r}")
        if not phonemes.strip():
            raise ValueError("Text produced no pronounceable speech")
        batches = []
        while len(phonemes) > 480:
            stop = max(phonemes.rfind(mark, 0, 480) for mark in ".!?;")
            if stop < 80:
                stop = phonemes.rfind(" ", 0, 480)
            if stop < 0:
                raise ValueError("An individual spoken word exceeds the model window")
            batches.append(phonemes[:stop + 1].strip())
            phonemes = phonemes[stop + 1:].strip()
        if phonemes:
            batches.append(phonemes)
        audio = []
        for batch in batches:
            tokens = [self.vocab[c] for c in batch]
            style = self.voices[voice][len(tokens) - 1]
            result = self.session.run(None, {
                "tokens": np.array([[0, *tokens, 0]], dtype=np.int64),
                "style": np.asarray(style, dtype=np.float32),
                "speed": np.array([speed], dtype=np.float32),
            })[0].ravel()
            if len(result) == 0 or not np.isfinite(result).all():
                raise ValueError("Model produced empty or non-finite audio")
            audio.append(result)
        samples = np.concatenate(audio)
        peak = float(np.max(np.abs(samples)))
        if peak < .0001:
            raise ValueError("Model produced silent audio")
        samples = samples * min(1.0, .96 / peak)
        pcm = (samples * 32767).astype("<i2")
        output.parent.mkdir(parents=True, exist_ok=True)
        temporary = output.with_suffix(".wav.tmp")
        try:
            with wave.open(str(temporary), "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(24000)
                wav.writeframes(pcm.tobytes())
            os.replace(temporary, output)
        finally:
            temporary.unlink(missing_ok=True)
        return {"sampleRate": 24000, "samples": len(samples), "peak": float(np.max(np.abs(samples))),
                "rms": float(np.sqrt(np.mean(samples * samples))), "chunks": len(batches)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True)
    parser.add_argument("--voices", required=True)
    args = parser.parse_args()
    sys.stdin.reconfigure(encoding="utf-8-sig")
    sys.stdout.reconfigure(encoding="utf-8")
    wire = sys.stdout
    def send(payload):
        wire.write(json.dumps(payload, ensure_ascii=True) + "\n")
        wire.flush()
    with contextlib.redirect_stdout(sys.stderr):
        synth = Synthesizer(args.model, args.voices)
    send({"ready": True})
    while True:
        line = sys.stdin.readline(65537)
        if not line:
            break
        request = {}
        if len(line) > 65536:
            raise ValueError("Speech protocol line exceeds 64 KiB")
        try:
            request = json.loads(line)
            if not isinstance(request, dict) or not isinstance(request.get("id"), str):
                raise ValueError("Speech request must be an object with a string id")
            with contextlib.redirect_stdout(sys.stderr):
                result = synth.synthesize(request["text"], request["voice"], request["speed"], request["outputPath"])
            send({"id": request["id"], "ok": True, **result})
        except Exception:
            detail = traceback.format_exc() + f"\nCWD: {os.getcwd()}\nModel: {args.model}\nVoices: {args.voices}\nOutput: {request.get('outputPath') if isinstance(request, dict) else None}"
            send({"id": request.get("id") if isinstance(request, dict) else None, "ok": False, "error": detail})


if __name__ == "__main__":
    try:
        main()
    except Exception:
        traceback.print_exc(file=sys.stderr)
        print(f"Worker startup/protocol failure; cwd={os.getcwd()}; arguments={sys.argv[1:]}", file=sys.stderr)
        sys.exit(1)
