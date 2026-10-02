# Tip export verification — 2026-10-02

**The authoritative packaged export is green on the delivered tip.** This
records the `windows-native-preview` export run whose artifact is stamped
`4990223d`, superseding the earlier same-day run recorded below (which
verified `d6da0498`) and the `217918b7` record in
`2026-09-30-tip-export.md`. It is a development-state handoff, not a
release certification; separate clean-machine/VM certification remains
required.

## Scope and Git evidence

Repository `afterburn25/stellar-continuum`, branch
`game/ui-visual-overhaul` (PR #338, merged 2026-10-01, targeting
`engine/space-strategy-simulation-specialization`; work continues on the
same branch). Artifact stamp: `4990223d`.

Scope since the `d6da0498` run: the export pipeline's `voice_check` gate
(`3554b30c`), the editor recency-ordered MRU (`8af27f8d`), the
construction-controller test use-after-free fix (`b03a079d`), the
Save-As folder picker (`ffa075a4`), galaxy/system/body transform drags
(`785c07e1`, `2e04db3c` — including the system-view pick/render
position-source fix), the editor and tools-host unsaved-document
lifecycles with SAVE/DISCARD/CANCEL overlays (`73712220`, `8c3efe5c`,
incl. the Ctrl+O menu-index fix), and the engine-side
`spatial_effect_placement` view-space pan/attenuation helper adopted by
the RuntimeHost bounce cue (`79fc1d58`), plus documentation.

Commits `79fc1d58`–`4990223d` were pushed while this run was in flight;
their source edits were already on disk before the affected objects
compiled (verified by object timestamps and symbol presence in the
shipped `stellar_native_audio.lib`), so the binary content is
tip-equivalent. The remaining in-flight commits are documentation-only.

## Run

```bat
python tools\stellar-export\stellar.py export windows-native-preview
```

Artifact:
`Builds/Windows/StellarContinuum-0.1.14.2-dev-windows-native-preview-4990223d-20261002T100412139541Z`

## Results

- Fresh configure + full build (1923→1917 actions), green.
- CTest: 330/331 on the parallel pass; `developer_qa_host` hit its
  timeout under full-load scheduling and passed on the pipeline's solo
  retry (29.2 s) — a scheduling flake, not a code defect; effective
  331/331.
- Every `tools/stellar-export/test_*.py` module green.
- Dependency audit, manifest packaging, restricted-PATH relocated smoke,
  and the full validator battery green — new-game/settings/audio/voice/
  controls round-trips, galaxy/system/travel/fleet/research/shipyard/
  support/developer captures all produced.
- Audio evidence inside the artifact: `nativeNewGameAudioCheck`,
  `nativeAudioSettingsCheck` and `nativeVoiceSettingsCheck` all true;
  `voice_check` now runs as part of the export itself and reports
  `{"available":true,"played":2,"streamed":true,"unknown_denied":true,"overlap_prevented":true,"queue_bounded":true,"stopped":true}`
  — the packaged scientist cue streams through `AudioStreamDecoder` on
  the shipped binary (routed SAPI leg + direct cue leg, as designed).
- ZIP + SHA-256:
  `d13be645b21eabf71af9d57a63bb8a8cf7b48b2eefb8d8a7c8023d3e87260e7a`
- `validation.json`: only `graphicalParity` remains false
  (intentional — same scorecard as prior runs).

## Prior same-day run (superseded)

An earlier run on `d6da0498` (artifact
`StellarContinuum-0.1.14.2-dev-windows-native-preview-d6da0498-20261002T074409894463Z`,
SHA-256 `80b5b1af9cb77c07e6e1e447c37c7889a31d6ad73882131344cc5c95cffba5a1`)
verified the decoded-PCM budget work with a clean 331/331 first pass.
Two still-earlier attempts that day failed inside the build step on
transient host-resource errors — `CreateProcess` failures on `cmake -E`
utility commands (concurrent local builds) and one `cl.exe` C1060
out-of-heap error — not on any compile or test defect.
