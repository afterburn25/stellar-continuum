# Tip export verification — 2026-10-02

**The authoritative packaged export is green on the delivered tip.** This
records the `windows-native-preview` export run on `d6da0498`, superseding
the `217918b7` record in `2026-09-30-tip-export.md`. It is a
development-state handoff, not a release certification; separate
clean-machine/VM certification remains required.

## Scope and Git evidence

Repository `afterburn25/stellar-continuum`, branch
`game/ui-visual-overhaul`, PR #338 targeting
`engine/space-strategy-simulation-specialization`. Verified tip:
`d6da0498` — includes the decoded-PCM budget work (`344fef63`), its
director-level budget-denial containment coverage (`d6da0498`), and the
documentation/known-issues updates (`789ac79a`).

Scope since the `217918b7` export: the shared `stellar_native_client_logic`
build-topology deduplication, SDL_GPU submit-fence queue telemetry, the
CI `game/**` push-trigger fix, streamed scientist voice cues through
pull decoders, voice-scoped fault containment, the `--voice-check`
packaged-cue streaming assertion, and the process-wide decoded-PCM
budget with atomic reservation, `MemoryTracker` visibility and
voice-scoped denial containment.

## Run

```bat
python tools\stellar-export\stellar.py export windows-native-preview
```

Artifact:
`Builds/Windows/StellarContinuum-0.1.14.2-dev-windows-native-preview-d6da0498-20261002T074409894463Z`

Two earlier same-day attempts failed inside the build step on transient
host-resource errors — `CreateProcess` failures on `cmake -E` utility
commands (concurrent local builds) and one `cl.exe` C1060 out-of-heap
error — not on any C++ compile or test defect. This run executed with
the host otherwise idle and required no code change.

## Results

- Fresh configure + full build (1917 actions — lower than the prior
  2219 because the shared-logic-library topology removed the repeated
  app-source compilations).
- CTest: **331/331 green on the first pass**.
- Every `tools/stellar-export/test_*.py` module green.
- Dependency audit, manifest packaging, restricted-PATH relocated smoke,
  and the full validator battery green — new-game/settings/audio/voice/
  controls round-trips, galaxy/system/travel/ship-art/diplomacy/developer
  captures all produced.
- Audio evidence inside the artifact: `nativeNewGameAudioCheck`,
  `nativeAudioSettingsCheck` and `nativeVoiceSettingsCheck` all true;
  the bounded music queue reports exactly `288000` queued bytes on both
  fresh and reload legs with clean stop.
- ZIP + SHA-256:
  `80b5b1af9cb77c07e6e1e447c37c7889a31d6ad73882131344cc5c95cffba5a1`
- `validation.json`: 106 top-level booleans, only `graphicalParity` false
  (intentional — identical scorecard to the `217918b7` run).

Separately, the opt-in `--voice-check` path was verified against the
development binary via
`validate_native_system_travel_export(..., voice_check=True)`: both moved
and paused-reload legs report
`{"played":2,"streamed":true,"unknown_denied":true,"overlap_prevented":true,"queue_bounded":true,"stopped":true}`
— the packaged scientist cue genuinely streams through
`AudioStreamDecoder` alongside the routed synthesized-voice leg.
