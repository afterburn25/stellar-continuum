# Stellar Continuum Voice Engine

Game text becomes local offline speech through an optional Kokoro pack or Windows SAPI fallback, then plays through Godot with profile processing and sentence subtitles. The engine is presentation-only. Simulation does not await speech, depend on a provider, change its rules, or serialize an audio queue.

Gameplay integration, current character resolution, the canonical event registry and the expanded Developer event tester are documented in [VOICE_EVENT_INTEGRATION.md](VOICE_EVENT_INTEGRATION.md). Announcement frequency now offers Minimal, Normal and Frequent while retaining old chatter-setting compatibility.

## Optional local Kokoro pack

Kokoro is an optional, manifest-gated local Python worker. Setup is explicit: `py -3.12 tools/voice/setup_kokoro.py`. Runtime reads a nonblank `STELLAR_VOICE_PACK` or `%LOCALAPPDATA%\StellarContinuum\voice-packs\kokoro-v1\pack.json`, validates absolute paths/checksums, and performs network-free JSONL inference. Blank/whitespace environment overrides use default discovery; explicitly supplied invalid paths still fail clearly. It never downloads a model or invokes a shell. Missing packs fall back to SAPI/captions; a neural failure is never cached as SAPI output.

Setup creates a local Python 3.12 environment with version-pinned dependencies and downloads a SHA-256-pinned 325 MB model plus 28 MB voices. The worker uses English Misaki/spaCy only, not eSpeak, Torch, or phonemizer. Unknown pronunciations fail cleanly with subtitles/errors intact; use curated pronunciation data. The pack is local setup data, not portable product content; retain its license inventory and local LGPL `num2words` source. Worker, pronunciation, vocabulary and dependency-lock fingerprints participate in the installed pack version used by the speech cache.

Provisional neural timbres preserve SAPI fallback: commander `af_kore`, scientist `af_heart`, diplomat `af_bella`, narrator `bf_emma`, male commander `am_fenrir`, governor `bm_george`, computer `af_nova`, operations `am_michael`, and Grey translator `af_nicole`. They are synthesized timbres, not actor/casting claims. Twenty-four local auditions are review artifacts only. Listening/casting approval, emotional acting, formant/spatial work, and a portable pack remain pending.

Generate the audition player with the installed pack's Python: `python tools/voice/build_auditions.py --pack /absolute/path/pack.json --output /absolute/path/auditions`. This writes an offline HTML audio player, 24 matched A/B WAVs, the four-voice comparison reel and a provenance/measurement manifest. Normal, technical and urgent lines use the same text and speed for each role's two candidates. Urgent cadence is not a trained emotional style. No game DSP is baked into auditions.

On 2026-09-10, the real installed pack passed all 11 voice-core groups, including four distinct female PCM outputs with second-request cache hits, cancel-during-startup followed by successful recovery, malformed protocol, stderr flooding, timeout, disposal and manifest validation. The Python worker also passed normal/long multi-window speech, invalid inputs, error recovery, temporary cleanup and missing-model terminal failure. Twenty-four distinct audition outputs contain 228.28 seconds of audio generated in 51.37 seconds on the local CPU; minimum RMS .0412, peak at most .960001. These are objective signal checks, not subjective listening approval.

Native Godot 720p validation in `work/voice-neural-runtime-fixed` exited 0 with empty stderr and 21 checks. It verified bf_emma opening audio, the four-voice cast, real af_heart research speech, live ship-computer/Grey playback, cache replay, mute/cancellation recovery, captions, timed construction/ship launch, right-click fleet travel and reset. Voice bus captures were non-silent (narrator RMS .1194; Grey RMS .1112); no worker remained after shutdown. This run caught and fixed a real bug where caller cancellation during startup permanently disabled the neural provider. Core Runtime remains 70/70. The manual `Offline voice validation` workflow can download the optional pack and rerun real neural checks with its `neural` input; normal PR jobs do not download models.

## Flow and ownership

`Main.Voice.cs` and the existing completion handlers consume already player-scoped events. `VoiceEventRouter` selects finite authored templates from `data/voice_profiles/events.json`. A `SpeechRequest` passes through profile resolution, pronunciation/normalization, prerecorded/cache lookup, a replaceable `IVoiceSpeechBackend`, WAV validation, and `VoicePlaybackController`. Only the controller accesses Godot audio and UI objects.

Construction, orbital shipyard, ship launch and colony announcements are driven by completion events, not command acceptance or message keywords. Adaptive Research announces newly established knowledge from the player's authoritative node state; starting or pausing research does not mean completion. Own fleet transitions are observed after each simulation step, including Developer time advances. Discovery/activity/contact events respect the existing observer filter. Critical hull reports read own fleet status; incoming translated transmissions read the same scoped diplomatic proposal summaries visible in the UI. Merely charting additional stars does not announce an unidentified vessel.

Opening, research, construction, shipyard, launch, departure, arrival, discovery, colony, unknown activity, alien transmission and critical hull cues are wired. Survey, first contact and operating shortfall are also covered. Voice reset clears queues and comparison state; loading a save establishes a baseline and does not repeat the opening or old completions. No save-format change is needed.

## Profiles, pronunciation and requests

| Profile ID | Role | Present implementation |
| --- | --- | --- |
| human_female_fleet_commander | Commander Elena Voss | Brisk military cadence, communications EQ |
| human_female_chief_scientist | Dr. Amara Chen | Measured technical delivery, distinct af_heart timbre |
| human_female_diplomat | Ambassador Mara Okafor | Slower, composed delivery |
| human_female_narrator | Narrator | Slow, lower-register cinematic delivery |
| human_male_fleet_commander | Commander Idris Kane | Brisk military delivery, lower register |
| human_male_governor | Governor Elias Ward | Deliberate administrative delivery |
| human_operations_officer | Operations Officer | Concise communications delivery |
| ship_computer | Ship Computer | Faster synthetic cadence and chorus |
| grey_diplomat | Grey Envoy | Slow cadence, restrained doubling, resonance, synthetic undertone |

Profiles are JSON records with identity/role/presentation, age/accent/style metadata, rate, pitch, radio/synthetic flags, resonance/chorus/reverb, preferred backend/model/voice, culture, subtitle name, portrait, fallback and enabled status. `Dsp` reserves additional alien-processing metadata. Identity, age, emotional tone and portrait fields are authoring metadata; the current backend does not turn every descriptor into a trained voice.

Pronunciation precedence is global → civilization → species → profile → character → request. Keys are matched at word boundaries in a single pass so an earlier replacement cannot destroy a later override. Scoped maps are `CivilizationPronunciations`, `SpeciesPronunciations`, `Pronunciations`, and `CharacterPronunciations`. English normalization covers ship IDs, astronomy names/units, percentages, comma-space coordinate pairs, clock times, ISO dates and common Roman designators. It preserves the original visible subtitle and ordinary “I” pronouns/thousands separators. Other languages require language-specific normalizers.

Requests carry profile/text/subtitle, priority, category, expiry/cooldown, event/localization key, interruptibility/queue behavior, cache policy, culture, emotion/urgency, pronunciation overrides, communications and spatial flags. Use the controller/router rather than calling SAPI from a screen.

## Offline provider and cache

`IVoiceSpeechBackend` exposes availability, voices, languages, offline/streaming/style support, output format, rate and hardware/latency metadata. The factory selects a manifest-validated installed Kokoro pack and otherwise Windows SAPI. The worker starts lazily; a genuine startup failure retains subtitles and diagnostics. No cloud provider, credentials, paid service, runtime model download, or GPU requirement is configured. Offline Only blocks synthesis through a backend reporting itself online. Unsupported platforms retain captions and valid prerecorded/cached files.

The SAPI adapter discovers installed voices, resolves preferred ID/name then gender/culture, converts SAPI language LCIDs, and confines COM calls to STA workers. Discovery is bounded; speech is asynchronous on one synthesis worker with cancellation/purge and a 30-second synthesis timeout. It writes 22,050 Hz, 16-bit mono PCM to a short temporary WAV, closes/validates it, then moves it into the user cache. This avoids SpFileStream's failure on deeply nested Windows paths. Temporary files are removed in success/failure cleanup. Text is explicitly non-XML.

Runtime storage is `user://voice-cache/v1`, normally beneath Godot's application user-data directory. The 256 MiB cache validates PCM RIFF chunks and keys normalized text, effective profile/rate/pitch/DSP, backend/model/version, actual selected voice, culture and processing version. Corrupt entries are rejected and least-recently-used entries pruned. Refresh regenerates; NoCache bypasses lookup and writes a temporary playback file subject to the same storage bound.

A request or event cue can supply `PrerecordedPath`; a valid PCM WAV is preferred to synthesis. Use an absolute external path or `user://` path. `res://` resolves to a filesystem path: exported prerecorded files must be shipped beside the executable (for example under the existing externally copied `data/` directory), rather than only inside the PCK. No prerecorded speech is bundled in this change.

When an installed voice disappears, the original voice-specific key remains intact. Only an unavailable backend may use `fallback-index.json` to recover a previously generated matching line without knowing the missing voice ID. The atomic index accepts only validated 64-hex WAV filenames inside the cache, caps itself at 1 MiB/4096 entries, and removes stale aliases. NoCache output is never indexed.

## Playback, subtitles and settings

The Godot controller has a separate bounded playback queue of eight lines, two per category, short-lived deduplication and a 40-second stale-line limit. Higher priorities sort first. Explicit interruption/replacement may interrupt lower, interruptible lines; plain enqueue does not. Cinematic opening narration is non-interruptible. No Interruptions preserves the active line. Cancel/reset clears pending generation and playback so stale callbacks cannot start audio in another campaign.

The Voice bus applies communications EQ, resonance and mild pitch adjustment, followed by a transparent peak limiter to keep pitch-shifted neural speech within PCM headroom. Dialogue remains one dry, non-spatial source: profile chorus and reverb metadata are not played as delayed wet copies, because they made local neural speech sound doubled and enclosed. Profile communications processing can be bypassed for direct narration. A Communications bus is reserved for future routing. Music fades to 55% during dialogue and restores smoothly; SFX and authoritative timing continue normally.

Subtitles use speaker labels and original text, with duration based on audio length and a reading-time floor. They work when voice is muted, synthesis is unavailable or a request fails. Muting during pending synthesis cancels that work and preserves a caption. Gameplay's existing notifications remain available independently of voice.

Open **Voice & subtitles** from the campaign menu for voice enable/volume, subtitle enable/size/background/labels, communications intensity, important-only chatter, no interruptions, replay and stop. Settings persist in `user://voice-settings.json`; corrupt data safely falls back to defaults. Unsupported backend/quality controls are hidden. In Developer mode, **Developer Voice Lab** adds profile choice, editable sample text, restrained emotion/cadence preview, synthesis/play, stop/replay and diagnostics. Long token identifiers wrap at 720p. Reset closes the Lab.

Diagnostics distinguish synthesized, cached, prerecorded and subtitle-only outcomes, selected voice/profile, length and pending work. Backend errors include support-log detail; player notifications do not expose backend internals. No audio exception is allowed to escape the engine's work queue or controller's result handling.

## Licensing and privacy

| Item | Source / rights boundary | Distribution in this change |
| --- | --- | --- |
| Windows SAPI / installed voices | Existing Microsoft Windows or third-party installation; use remains subject to that component/provider's terms | Referenced through OS COM interfaces; no voice DLL, model, installer or OS component redistributed |
| New C# engine, profile JSON and authored dialogue | Project-authored source/data | Included in this branch |
| Godot and .NET | Existing project runtimes and existing export licensing notices | Existing packaging remains responsible for runtime notices |
| Optional Kokoro model and voice embeddings | Apache-2.0 model; source, hashes and dependency notices in `tools/voice/THIRD_PARTY.md` | Downloaded only by explicit local setup; no model, voice binaries or cloud SDK bundled |

The [Microsoft SpVoice reference](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms723602(v=vs.85)) and [GetVoices reference](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee125639(v=vs.85)) document the OS interface. This change does not grant redistribution rights to installed provider voices. Before bundling any future model, actor recording or generated content library, record that specific source, commercial-use/redistribution terms, attribution and voice restrictions. There are no unverified bundled voice assets in this PR.

This implementation sends no text/audio to a network service. Cache files and support logs can contain player-visible dialogue and locally entered Voice Lab text; cache filenames hash their identity but the audio itself is not encrypted. Users can remove the voice-cache folder with the game closed. A future cloud backend must be explicit opt-in and must not send hidden simulation state or log secrets.

## Quality limits and next work

The installed optional pack provides distinct provisional neural timbres for the authored profiles; the 24 audition WAVs are all nonzero and unique, with eight distinct voice embeddings (228.28 seconds total synthesis, 51.37 seconds CPU; minimum RMS .0412 and peak at or below .960001). This proves local output diversity and basic signal health, not actor-quality casting approval. SAPI fallback still has one installed female base voice (Zira), so its four female roles differ only through cadence/processing; missing packs or voices degrade honestly to SAPI or captions.

Grey is an audio/translation preset, not an invented canonical civilization. Current alien sound combines cadence, pitch and resonance without delayed doubling or reverb. Independent formant shifting, whisper layers, neural emotions and true multilingual translation are not implemented. Limited emotion choices apply small rate adjustments; they do not claim neural style control. Spatial requests reserve a future interface and currently play as non-spatial UI speech. Thalori, untranslated/partially translated dialogue and mixed original/translator layers need dedicated content/provider work.

## Validation

Run from the repository root:

```powershell
dotnet build Game.sln --configuration Release
dotnet run --project tests/VoiceCoreChecks/VoiceCoreChecks.csproj --configuration Release
dotnet run --project tests/Game.CoreRuntime.Validation/Game.CoreRuntime.Validation.csproj --configuration Release
```

Core checks cover normalization, scoped overrides, profiles/fallbacks, malformed settings, valid/corrupt WAV and cache identity/pruning, priority/expiry/duplicates/cancel, offline policy, cache modes, authored routing/variation and real installed SAPI synthesis/cache reuse. Set `STELLAR_REQUIRE_SAPI=1` to require installed voice proof; unsupported environments explicitly report a skip rather than fake successful synthesis. The voice workflow runs these checks on Ubuntu and Windows.

Native Godot evidence: set `STELLAR_CAPTURE_FOCUS=voice`, a fresh writable user profile and `STELLAR_SCREENSHOT_DIR`, then run `res://tools/ScreenshotCapture.tscn` at 1280×720 with the pinned Godot 4.7.2 .NET editor. The maintained entry point catches failures and exits nonzero. Focused captures deliberately cannot substitute for the full screenshot release gate.

On September 10, 2026, the local Windows candidate passed 19 focused runtime checks, five visually inspected 720p screenshots, real timed research/shipyard/shipbuild and mouse-directed departure/arrival. Narrator and Grey live processed-bus WAVs contained nonzero audio (narrator RMS .1257; Grey RMS .0823). Failure fallback, pending mute, captions, cache replay, no interruptions and reset/Lab cleanup passed. Debug/Release builds were clean and Core Runtime regressions passed 70/70. The other event hooks have data/structural validation; the focused live walkthrough does not claim to have naturally encountered every rare colony/contact/combat event.
