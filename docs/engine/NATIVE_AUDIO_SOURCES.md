# Native audio sources

The native client packages only the audio resources already required by the
Godot `AudioDirector`: the user-supplied main score
`assets/audio/music/claimed-by-the-void-loop.mp3` and these project-authored,
deterministic additive-synthesis WAV cues:

- `assets/audio/sfx/ui-hover.wav`
- `assets/audio/sfx/ui-confirm.wav`
- `assets/audio/sfx/discovery-reveal.wav`
- `assets/audio/sfx/construction-complete.wav`
- `assets/audio/sfx/ship-launch.wav`
- `assets/audio/sfx/strategic-alert.wav`

The main score is copied without transcoding. The supplied score's provenance
and SHA-256 are recorded in `docs/ASSET_MANIFEST.md`; no creator, license, or
rights claim is inferred. The SFX provenance is also recorded there: they are
original project assets with no sampled music, sound-library recordings, or
third-party material.

`menu-continuum.wav` and `deep-space-operations.wav` are superseded historical
music assets. They are deliberately excluded from the native runtime package.
`export/native-audio-assets.json` is the complete whitelist: packaging verifies
every declared source path, runtime path, and SHA-256 fingerprint before copy.
