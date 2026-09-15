# Native audio

The Windows native client has an SDL3 playback backend and uses Windows Media
Foundation (MF) to decode the reviewed MP3 and WAV assets. It does not ship a
third-party decoder DLL. The executable imports Windows `MFPlat.dll` and
`MFReadWrite.dll`, and uses COM; both MF libraries must be installed for the
executable to start. `mfuuid` supplies link-time identifiers, not a runtime DLL.
Runtime codec/device failures disable audio with one terminal diagnostic.

The decoder requests normalized 48 kHz stereo float PCM from MF. Source files
must be non-empty and at most 16 MiB. Each decoded clip is limited to 96 MiB.
The director accepts the seven approved clips only when their combined decoded
size is at most 104 MiB: the supplied `claimed-by-the-void-loop.mp3` score and
six SFX (`ui-hover`, `ui-confirm`, `discovery-reveal`,
`construction-complete`, `ship-launch`, and `strategic-alert`). The complete
runtime asset whitelist and source provenance remain in
`NATIVE_AUDIO_SOURCES.md`; this document does not alter that credited payload.

Playback is owner-thread-only. A single background job decodes the clips while
the UI owner thread continues startup, then the owner thread collects the
finished clips and services SDL output once per frame. The music stream queues
at most 288,000 bytes (three quarters of a second of 48 kHz stereo F32) and
loops the selected score. Effects use eight voices, each limited to 1 MiB; an
occupied oldest voice is replaced when all eight are active. Finite effects
flush their converter tail after queueing; continuous music does not flush.
The director
stops every stream, clears queued audio, closes the SDL device, and waits for
the decode job before its owner-side lifetime ends. Audio errors disable this
presentation service without entering simulation state.

Boot remains silent. The startup action marks the menu ready only after the
startup flow has reached that state, then starts the main score once; the same
selected clip continues into the campaign. The current concrete hooks are the
startup action and campaign navigation/action confirmation. The hover and
event-cue API exists, but not every simulation event is wired to it yet.

The native client exposes `--audio-check` for an opt-in smoke proof. It emits
one separate `audio_check` JSON line before the normal diagnostic, recording
that assets loaded, music started exactly once, an effect confirmation count,
bounded queued music bytes, startup service count, and clean stop. The export
validator runs this option for both a freshly created game and a paused reload:
the fresh case requires startup servicing and a confirmation; direct reload is
allowed to report both as zero. It rejects missing, duplicate, malformed, or
out-of-range evidence.

Five focused CTests and 92 Python checks passed, including real MP3/WAV decode,
resampling, queue drain/refill, malformed input, owner-thread and shutdown
checks, packaging rejection, and both Git checkout line-ending settings.

Two actual relocated Vulkan runs passed with the default audio output: fresh
250-system setup at 720p and its paused reload at 1080p, with restricted PATH and
Unicode isolated save paths. Each reports one music start, 288000 queued bytes
and clean stop. Fresh startup reports 38 silent boot services and one action
confirmation. The original anchor remains unchanged and the complete generated
Player17 payload matches its paused reload. Initial frame intervals averaged
16.696 and 16.659 ms on this host; this short run is not a sustained performance
claim. Both launches were repeated successfully after the effect-tail fix.
An intentionally unavailable SDL audio driver produced a single disable
diagnostic, reached the campaign, then failed `--audio-check` with exit 1 and
the device error, without a retry or changed anchor. An initial negative-test
invocation lacked its required save anchor; the final run supplied the canonical
fixture. Logs are recorded in `CPP_MIGRATION_HANDOFF.md`. These checks establish
lifecycle and diagnostic evidence, not audible speaker verification. Windows
CI explicitly builds and runs both audio test targets with SDL's dummy output,
independent of the headless export build. Twelve exporter integrity tests pass;
17 unrelated headless runtime tests were skipped without their opt-in binary.

Remaining work includes native audio settings persistence and UI, the
UK-female scientist voice path, complete event wiring, and playback-device
loss/recovery.
