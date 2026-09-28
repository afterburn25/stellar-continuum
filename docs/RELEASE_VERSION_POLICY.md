<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> Godot/C#/.NET references below are legacy implementation or fixture provenance,
> not the current runtime or instructions to restore it.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Release version policy

The current locally validated native alpha is **0.1.13 Alpha** (engine 0.1.62).
See [its validation report](releases/0.1.13-alpha-validation.md) for the package,
checksums, tested flows and remaining limits.
Native versions come from `export/runtime-config.json`, which CMake embeds in the
executables and the exporter records in package names and manifests. Each native
alpha increments manually and adds `docs/releases/<version>.md`. A local package
may truthfully record `sourceDirty: true`; it is not an exact clean-commit build.
Full parity release presets remain blocked until their separate gates pass.

The preserved Godot reference uses `VERSION` and `GameVersion`; those values are
independent and are not changed when packaging a native alpha.

## Historical Godot policy

The initial published release target was **0.1.0 Alpha**.

- `VERSION` and `GameVersion.Current` use technical SemVer (`0.1.0-alpha`). This value is written
  to save metadata, manifests and Windows package filenames.
- `GameVersion.Display` is the friendly in-game label (`0.1.0 Alpha`). It may be shown in menus,
  diagnostics and the title header without changing the technical identifier.
- Each future published build increments the version manually and records a short changelog entry:
  `0.1.1-alpha`, `0.1.2-alpha`, and so on. Development-only commits do not bump the published
  version automatically.

## 0.1.1 Alpha (local validation)

The combat-system-visuals stream adds observer-safe 3D system combat presentation,
bounded representative rendering, live-fire observation, and 100,000-fleet validation.
Release build and maintained combat validation pass locally; native and hosted receipts
remain the acceptance gate for this development version.

## 0.1.0 Alpha

Initial tracked Alpha release containing the fullscreen research workspace, stellar and gate
presentation, perimeter/territory and colorful navigation work, and save/window lifecycle
hardening. Build, runtime, and native visual checks remain release-gate evidence rather than a
claim that every final acceptance lane is complete.

Current milestone additions include the galaxy-v4 catalog with a separate non-routable central
SMBH, research workspace interaction and UTF-8/input/wrapping repairs, shared gate geometry and
labels, Sun spectral/eruption presentation, repaired territory contours, and cosmic-only core
accretion artwork. Core and Simulation maintained receipts are 81/81 and 71/71; territory
quality is 20/20. Native full-capture rerun, ordinary Player journey, Windows package,
save/autosave, voice-audibility, and final visual review remain open, so this changelog does not
promote the build to release completion or claim a new download.
