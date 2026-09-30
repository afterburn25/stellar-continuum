# Tip export verification — 2026-09-30

**The authoritative packaged export is green on the delivered tip.** This
records the `windows-native-preview` export run on `217918b7`, superseding
the `79ed3047` and `412e683d` records in `AGENT_HANDOFF.md`. It is a
development-state handoff, not a release certification; separate
clean-machine/VM certification remains required.

## Scope and Git evidence

Repository `afterburn25/stellar-continuum`, branch
`game/ui-visual-overhaul`, PR #338 targeting
`engine/space-strategy-simulation-specialization`. Verified tip:
`217918b7` — includes the `atomic_file_write`
`ERROR_UNABLE_TO_REMOVE_REPLACED` retry that removes the transient
destination-lock flake class.

Scope since the `412e683d` export: the three core-lane projections
(`DiplomacyActionBlocker`, `CivilizationLogisticsCoverage::external_links`,
`fleet_composition`), fleet rail + chronicle hover explainers, and the
warning-hygiene sweep (CMP0175, STL4021 `u8path`, C4127, C4244, C4456) —
the tree compiles warning-free at /W4.

## Run

```bat
python tools\stellar-export\stellar.py export windows-native-preview
```

Artifact:
`Builds/Windows/StellarContinuum-0.1.14.2-dev-windows-native-preview-217918b7-20260930T053810915133Z`

## Results

- Fresh configure + full build (2219 actions), zero warnings.
- CTest: **328/328 green on the first pass** — no `--rerun-failed`
  needed; the prior run's transient destination-lock flake is now
  absorbed by the `atomic_file_write` retry.
- Every `tools/stellar-export/test_*.py` module green
  (`test_galaxy_asset_import` self-skips: `assets/source/` provenance is
  not vendored locally).
- Dependency audit, manifest packaging, restricted-PATH relocated smoke,
  and the full validator battery green — new-game/settings/audio/voice/
  controls round-trips, galaxy/system/travel/ship-art/diplomacy/developer
  captures all produced.
- ZIP + SHA-256:
  `e8ad75b9598864d90775c67c5bf93cddb563f1bcb40c74aaa21a67e8c581f06f`
- `validation.json`: 106 top-level booleans, only `graphicalParity` false
  (intentional — identical scorecard to the `412e683d`/`79ed3047` runs).

The same pipeline also ran green at `79ed3047` earlier the same day
(SHA `e35af2ccba25a8ee89766f4271f0818fe3f65beebee5b4c656c61ca1612a28cb`);
that artifact's one transient `engine_windows_maintenance` file-lock
flake (auto-retried green) motivated the `atomic_file_write` retry now
verified inside this artifact.
