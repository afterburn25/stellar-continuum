# Tip export verification — 2026-10-02

**The authoritative packaged export is green on the delivered tip.** This
records the `windows-native-preview` export run whose artifact is stamped
`6b349fe7`, superseding the earlier same-day runs recorded below (which
verified `1874f39d`, `4990223d` and `d6da0498`) and the `217918b7` record
in `2026-09-30-tip-export.md`. It is a development-state handoff, not a
release certification; separate clean-machine/VM certification remains
required.

## Scope and Git evidence

Repository `afterburn25/stellar-continuum`, branch
`game/ui-visual-overhaul` (PR #338, merged 2026-10-01, targeting
`engine/space-strategy-simulation-specialization`; work continues on the
same branch). Artifact stamp: `6b349fe7`.

Scope since the `1874f39d` run: support bundles attach the newest
completed crash report as a bounded `crash-report.txt` entry
(`5f84cc7a`, fixed in `6b349fe7` to exclude the live session's
header-only report and to accept the optional entry in the export
validator), and the profiler span-retention threshold
(`b74433b1` — `set_span_retention_threshold` + shell MIN button).

The first attempt at this tip (`b74433b1`) failed the packaged support
validator for exactly the live-report bug `6b349fe7` fixes — the
validator caught the regression end-to-end, which is the intended
contract. This run's packaged `support-first.zip`/`support-second.zip`
carry a genuine `crash-report.txt` — a FATAL report written by the
voice-fault injection smoke earlier in the same flow — proving packaged
attachment of real crash evidence.

A note on the archive step: the export process was killed mid-`make_archive`
(the runner shell was reaped after validation completed); the directory,
`validation.json` and all evidence were already written and verified, so
the archive was rebuilt from the verified directory with the same
`shutil.make_archive` + `file_digest` code path and its SHA-256 recorded
below.

## Run

```bat
python tools\stellar-export\stellar.py export windows-native-preview
```

Artifact:
`Builds/Windows/StellarContinuum-0.1.14.2-dev-windows-native-preview-6b349fe7-20261002T143649814916Z`

## Results

- Fresh configure + full build (1921→1917 actions), green.
- CTest: 329/331 on the parallel pass; `engine_scale3d_25000` (240 s) and
  `native_stellar_eruptions` (180 s) hit their timeouts under parallel
  load and both passed green on the pipeline's solo retry (64 s and
  63 s) — same environmental-load pattern as the earlier
  `developer_qa_host` flake; no correctness failure.
- Every `tools/stellar-export/test_*.py` module green, including the
  extended `test_native_support_runtime` covering the optional
  `crash-report.txt` entry and empty/binary report rejection.
- Dependency audit, manifest packaging, restricted-PATH relocated smoke,
  and the full validator battery green — new-game/settings/audio/voice/
  controls round-trips, galaxy/system/travel/fleet/research/shipyard/
  support/developer captures all produced.
- **Support bundles ship real crash evidence**: the packaged
  `support-first.zip` contains `session.log`, `system.txt`,
  `campaign.player17.json` and `crash-report.txt` — the newest completed
  report, while the live session's header-only report is correctly
  excluded.
- `voice_check` inside the export reports the same all-true scorecard.
- **PDBs ship in the package**: `stellar-continuum-native.pdb` and
  `stellar-continuum.pdb` sit beside their executables.
- ZIP + SHA-256:
  `194b9a197032c93a6b9ea8f9daf007506f32eed21da03ccad75e3944c22aa5d7`
  (1.44 GB — evidence set differs from the prior run; PDBs remain
  staged beside both executables).
- `validation.json`: 107 top-level booleans, only `graphicalParity`
  false (intentional — same scorecard shape as prior runs).

## Prior same-day runs (superseded)

The `1874f39d` run (artifact
`StellarContinuum-0.1.14.2-dev-windows-native-preview-1874f39d-20261002T114350593508Z`,
SHA-256 `75122bc7197b32e4f4e43593d30124e4e4301c62f220be3867dd0768dc81fd5d`)
verified the diagnostics arc (symbolization, mini-trace, watchdog,
per-thread stacks) and PDB staging at 331/331.

The `4990223d` run (artifact
`StellarContinuum-0.1.14.2-dev-windows-native-preview-4990223d-20261002T100412139541Z`,
SHA-256 `d13be645b21eabf71af9d57a63bb8a8cf7b48b2eefb8d8a7c8023d3e87260e7a`)
verified the audio/lifecycle/editor stretch with 330/331 on the parallel
pass plus `developer_qa_host` green on solo retry (the flake that
motivated the timeout raise). The `d6da0498` run (SHA-256
`80b5b1af9cb77c07e6e1e447c37c7889a31d6ad73882131344cc5c95cffba5a1`)
verified the decoded-PCM budget work at 331/331. Two still-earlier
attempts failed inside the build step on transient host-resource errors
(`CreateProcess` flakes under concurrent builds, one `cl.exe` C1060
out-of-heap) — not on any compile or test defect.
