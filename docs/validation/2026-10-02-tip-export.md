# Tip export verification — 2026-10-02

**The authoritative packaged export is green on the delivered tip.** This
records the `windows-native-preview` export run whose artifact is stamped
`1874f39d`, superseding the earlier same-day runs recorded below (which
verified `4990223d` and `d6da0498`) and the `217918b7` record in
`2026-09-30-tip-export.md`. It is a development-state handoff, not a
release certification; separate clean-machine/VM certification remains
required.

## Scope and Git evidence

Repository `afterburn25/stellar-continuum`, branch
`game/ui-visual-overhaul` (PR #338, merged 2026-10-01, targeting
`engine/space-strategy-simulation-specialization`; work continues on the
same branch). Artifact stamp: `1874f39d`.

Scope since the `4990223d` run: the shipbuilding→construction executor
dependency fix (`6c285d9e`), crash-report fault-site symbolization
(`ac2241e8` — `describe_address` resolves `module+0xOFF func+0xD
file(line)`), the bounded symbolized `stack:` mini-trace in fault and
terminate reports (`adb97a26`), the `developer_qa_host` 120→240 s load
tolerance (`281fb74a`), PDB shipping in the preview package
(`69b87c83`), the heartbeat-armed hang watchdog with `hang_site=` +
loop-thread trace + minidump (`810a4243`), and per-thread stacks for
deadlock attribution in hang reports (`1874f39d`), plus documentation.

An earlier attempt in this window died at 1613/1917 inside the build
step: the preview tree linked `stellar_runtime_diagnostics_tests.exe`
after its source had compiled with `heartbeat()` but before
`stellar_engine` recompiled with the definition — mid-run source edits
produced inconsistent objects. This run started after all diagnostics
commits landed, so the tree built uniformly.

## Run

```bat
python tools\stellar-export\stellar.py export windows-native-preview
```

Artifact:
`Builds/Windows/StellarContinuum-0.1.14.2-dev-windows-native-preview-1874f39d-20261002T114350593508Z`

## Results

- Fresh configure + full build (1923→1917 actions), green.
- CTest: **331/331 first pass** — `developer_qa_host` now carries the
  240 s timeout and passed under parallel load (no solo retry needed).
- Every `tools/stellar-export/test_*.py` module green.
- Dependency audit, manifest packaging, restricted-PATH relocated smoke,
  and the full validator battery green — new-game/settings/audio/voice/
  controls round-trips, galaxy/system/travel/fleet/research/shipyard/
  support/developer captures all produced.
- `voice_check` inside the export reports
  `{"available":true,"played":2,"streamed":true,"unknown_denied":true,"overlap_prevented":true,"queue_bounded":true,"stopped":true}`.
- **PDBs ship in the package**: `stellar-continuum-native.pdb` and
  `stellar-continuum.pdb` sit beside their executables — packaged crash
  reports resolve `fault_site=`/`stack:`/`hang_site=` frames to
  file+line on the shipped binary, not just module+offset.
- ZIP + SHA-256:
  `75122bc7197b32e4f4e43593d30124e4e4301c62f220be3867dd0768dc81fd5d`
  (3.75 GB — the two PDBs account for the ~1.4 GB growth vs the prior
  symbol-free artifact).
- `validation.json`: 107 top-level booleans, only `graphicalParity`
  false (intentional — same scorecard shape as prior runs).

## Prior same-day runs (superseded)

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
