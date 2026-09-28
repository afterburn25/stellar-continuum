# Native scientist voice assets

These three fixed English lines use the existing human chief scientist casting:
Kokoro `bf_emma`, British English (`en-GB`), speed 1.0. They are synthetic speech,
not recordings of a human actor. The native client plays packaged PCM audio;
it does not run Python, ONNX, .NET, SAPI or download a voice model.

The source model is [hexgrad/Kokoro-82M v1.0](https://huggingface.co/hexgrad/Kokoro-82M),
using the [kokoro-onnx v1.0 export](https://github.com/thewh1teagle/kokoro-onnx/releases/tag/model-files-v1.0).
The repository's existing local voice pack supplied the model and voice embeddings.
The model's Apache-2.0 notice accompanies these assets as `Licenses/Scientist-Kokoro-Apache-2.0.txt`.
No model, Python distribution or inference dependencies are included in the game export.
See `tools/voice/THIRD_PARTY.md` for upstream provenance and the optional legacy pack's dependency notices.

`assets/audio/voice/scientist-cues.json` records the exact text, casting, source
model/voice/worker hashes, duration, PCM format, output hash, peak and RMS for
each cue. Generation uses `tools/voice/build_native_scientist.py` with an already
installed pack; it verifies the pinned input hashes and cannot fetch dependencies.
Generated output is dry: no echo, reverb, pitch filter or communications effect.
Export hashes must be reviewed separately after regenerating any line.

The human scientist advises on unavailable reconnaissance, research reports and
completed surveys. Another species does not silently receive the human casting.
Research notices say a report is available because the authoritative research
outcome can include failure. Repeated requests coalesce with an eight-second
cooldown; speech plays one line at a time and ducks the music temporarily.
Save activation clears pending voice/notice state. These are presentation assets;
they do not alter research outcomes, survey knowledge or saved campaigns.

The cast follows the selected UK voice profile. Listening acceptance and full
species-specific production casting remain separate from automated PCM/playback checks.
