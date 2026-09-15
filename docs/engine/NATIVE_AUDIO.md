# Native audio

The Windows native client has an SDL3 playback backend and uses Windows Media
Foundation (MF) to decode the reviewed MP3 and WAV assets. It does not ship a
third-party decoder DLL. The executable imports Windows `MFPlat.dll` and
`MFReadWrite.dll`, and uses COM; both MF libraries must be installed for the
executable to start. `mfuuid` supplies link-time identifiers, not a runtime DLL.
Runtime codec/device failures disable audio with one terminal diagnostic.

The decoder requests normalized 48 kHz stereo float PCM from MF. Source files
must be non-empty and at most 16 MiB. Each decoded clip is limited to 96 MiB.
The director accepts the seven required music/SFX clips only when their combined decoded
size is at most 104 MiB: the supplied `claimed-by-the-void-loop.mp3` score and
six SFX (`ui-hover`, `ui-confirm`, `discovery-reveal`,
`construction-complete`, `ship-launch`, and `strategic-alert`). The complete
music/SFX provenance remains in `NATIVE_AUDIO_SOURCES.md`. The three added
scientist clips have a separate 16 MiB budget and credits as documented below;
`export/native-audio-assets.json` pins the complete runtime whitelist.

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

Remaining work includes full character/species casting, dynamic speech and
playback-device loss/recovery. The native fixed human scientist channel is
documented below; the older optional TTS worker belongs to C#/Godot.

## Volume settings

The startup and pause menus expose Settings, currently containing Master, Music,
Effects, mute, Defaults, Cancel and Save. Slider changes are a live preview.
Cancel or Escape restores the last saved preferences. Mute sets the effective
master gain to zero while retaining the chosen sliders. Save atomically writes
`%LOCALAPPDATA%/Stellar Continuum/NativePreview/audio-settings.json`; preferences
apply at construction before the menu starts the score. They are separate from
Player17 and survive campaign changes. Invalid or inaccessible settings fall
back to defaults with a visible notice and one full path/cause diagnostic.

The exact version-1 schema is `schemaVersion`, `master`, `music`, `effects`, and
`muted`. Reads are bounded to 4 KiB, gains must be finite numbers from 0 through
1, mute is boolean, and duplicate/unknown fields are rejected. Corrupt input is
preserved until the player explicitly saves. Failed writes leave the overlay
open and retain the previously saved preferences.

For local hardware validation, call `validate_native_new_game_export` with both
`audio_check=True` and `audio_settings_check=True`. These options stay off for
ordinary export smoke runs. The latter adds `--audio-settings-check` to the two
maintained native launches and isolates preferences beside the temporary save
anchor. It requires complete menu-input evidence, 720p/1080p screenshots,
25%/50%/75% persistence across processes, unchanged settings bytes on reload and
full Player17 equality. Missing or malformed evidence fails validation. Six
focused CTests and 97 Python tests pass; the actual Vulkan startup/reload pair
passed again after the final text contrast correction. Windows CI runs the
settings CTest alongside the two audio tests with dummy output; it does not
claim hardware playback or speaker verification.

## Fixed scientist speech and campaign feedback

Three packaged dry PCM lines use `bf_emma` (en-GB) from the existing human
scientist profile: reconnaissance guidance, a research report and a completed
survey. Native playback requires no Python/model/.NET runtime. The source and
exact synthesis/output hashes are recorded in `NATIVE_SCIENTIST_VOICE.md` and
`assets/audio/voice/scientist-cues.json`; export verifies all 14 audio/notice files.

A dedicated finite voice stream shares the SDL device. Clips are limited to
8 MiB decoded each (16 MiB total), streaming stays bounded to 288000 bytes,
speech uses Master x Effects, and music ducks to 55% until the line drains or
is stopped. One line plays at a time; at most three unique cues wait, and an
8-second per-cue cooldown coalesces repeated requests. Missing voice data emits
one diagnostic while leaving required music/SFX operational. Alien players
retain visual guidance; their voices are not replaced by the human scientist.

The actual `CampaignFrameResult` feeds an observer-filtered, category-only
presentation summary. Owned research reports, construction, ship launches,
surveys, contacts, settlements and involved combat produce at most seven
coalesced notices; the newest three appear in the map's upper center and expire
after six real seconds. At most one prioritized event sound plays per two
seconds. Research failures are described as reports; outpost completion is a
settlement. No foreign names, messages, system IDs or hidden state enter the
notice summary. Campaign activation resets notices and pending speech.

For actual window/arrow validation, call `validate_native_system_travel_export`
with `voice_check=True`. This enables `--audio-check --voice-check` only on the
moving and paused-reload runs. Tests click an actual connected unknown arrow,
repeat that input during speech, prove that only one voice starts, bound its
queue, stop it before teardown and compare the complete paused Player17 state.
The strict exporter rejects missing/duplicate/malformed evidence. Ordinary
export runs stay silent; the opted-in run opens a visible Vulkan window.
