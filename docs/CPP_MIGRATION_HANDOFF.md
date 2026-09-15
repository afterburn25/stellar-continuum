# C++ migration handoff

Concise cross-agent notes. Full milestone history lives in `docs/engine/MIGRATION_STATUS.md`;
subsystem state lives in `docs/CPP_MIGRATION_STATUS.md`.

## Active branches

- `engine/stellar-engine-migration` — shared migration branch (head `ac45d958`, engine 0.1.57). Do not push directly; feed via reviewed PRs.
- `cpp/devin-swe2-native-conversion` — Devin/SWE-2 working branch; observed at `06b2b802`, with diplomacy presentation at `410753da` and sealed-export evidence. It is pending review/integration.
- `cpp/codex-native-architecture-integration` — Codex architecture/integration branch, based on `ac45d958`. Carries Devin's existing coordination documents forward; changes go through a PR to the shared migration branch.
- `work/stellar-engine-editor` — separate WPF editor tool (`editor/` only, 2 commits, non-conflicting).
- `work/voice-engine-tts` — fully merged ancestor of migration head.

## Interfaces added in 0.1.57 (candidate for review)

- `app/native_client/native_ship_art_assets.{hpp,cpp}` — `NativeShipArtAssets(asset_root)`:
  `image_for(optional<design_id>, FleetRole)` returns `shared_ptr<const RgbaImage>`,
  bounded 6 entries / 4 MiB, single decode per source, 224px box-average thumbnails.
  `ship_artwork_for()` maps design_id then falls back to role; unknown roles throw.
- `app/native_client/native_fleet_route_effects.{hpp,cpp}` — `append_fleet_route_effects(
  DrawList&, fleets, shipyard_view, systems_by_id, camera)` returns
  `FleetRouteEffectStats{routed_fleets, drawn_legs, chevron_segments, trail_strokes,
  position_circles}`. Pure draw-command appender, testable headless.
- `NativeOwnFleet` gained `std::optional<std::string> design_id` (presentation-only,
  copied from `FleetState.design_id` in `NativeFleetController::build`).

## Contract changes

- `NativeFleetWorkspace::render` and `NativeShipyardWorkspace::render` take an optional
  `const NativeShipArtAssets*` — pass `nullptr` for art-free rendering (existing tests do).
- Own-fleet routes draw through unsurveyed systems (matches `Main.VisualMap.cs`): own
  route geometry is player-authorized. Foreign fleet contacts remain unplaced;
  unsurveyed system labels remain redacted elsewhere.
- `copy_native_client_runtime` now requires `export/native-ship-art-assets.json` +
  the six declared files; mocks must stub them (see `test_native_client_runtime` setUp).

## Remaining blockers / next work

- Review and integrate Devin's existing diplomacy controller/workspace slice; do not re-port it. Current limits: no real-campaign or graphical diplomacy smoke (Player17 has no contacts), and no claims/border warnings, demand/trade composer, or grievance UI.
- Investigate cold-entry versus steady-state CPU update/scene construction spikes. Smoke timing separates bounded update, scene, and render-present phases, but render-present includes VSync wait and is not GPU-only; 60 FPS remains unproven.
- Surface colony visuals (buildings/roads), orbital structure rendering.
- Native audio — engine has no audio module at all; needs design before code.
- Frame pacing ~17–21 ms mean / ~33 ms p95 under smoke; 60 FPS unproven.
- `cleanMachineTest` still needs a separate machine/VM.
- `graphicalParity=false` stays until visual parity evidence exists.

## Systems intentionally not touched

- Godot/C# `src/` reference (kept as behavioral baseline until parity gates pass).
- `editor/` WPF tool (other workstream).
- Legacy research runtime — retained for save compatibility; Adaptive Research is
  authoritative.

## Coordination

- PR #325 (draft) + issue #324 remain the coordination hub. PR #332 is the current Codex candidate; do not merge either candidate or claim a shared-branch/full-suite/clean-machine result without the corresponding review and evidence.
- Sealed export = `tools/stellar-export/stellar.py export windows-native-preview`.
- Dev env: VS 2022 BuildTools `VsDevCmd` + `.tools/build-tools` venv (cmake/ninja/python).

## Codex checkpoint: native framing, renderer overhead, and reproducibility

- A fresh Windows checkout at `ac45d958` failed CMake's ship-art credits hash check: Git converted the reviewed LF file to CRLF. `.gitattributes` now pins all six hash-verified native text assets to their existing reviewed byte formats. No manifest hash or integrity check is weakened. A real Git checkout regression covers `core.autocrlf=true` and `false`.
- Soft-circle submission retains the same 20 triangles and ordered blending, using stack vertices and cached directions/indices instead of two heap allocations and 42 trigonometric evaluations per circle. Engine remains independent of Core.
- System framing uses measured body labels, disc/orbital/stellar envelopes, and a 12px presentation inset. Labels are collision-resolved with selected-body priority; initial travel activation frames exits once, while later travel refreshes retain the user's pan/zoom. Authoritative AU, transit, lane, and observer contracts remain unchanged.
- Final evidence: `native_system_travel`, `native_system_workspace`, `native_system_view`, `native_system_colony_entry`, and `native_settlement_workspace` CTests passed, alongside seven actual Vulkan galaxy/system/travel launches with exact paused Player17 reload and observer-secrecy validation. Final logs are `native-system-layout-tests.log` and `native-system-checkpoint.log`; JSON is `work/layout-{galaxy,system,travel}.json`; captures are `build-native/preview-*.bmp`.
- Smoke-only timing records bounded update/scene/render-present mean and p95 before JSON diagnostics. System/galaxy means were ~20.6–21.2 ms and p95 ~33.4–33.7 ms; render-present means ~16.4–16.6 ms include VSync wait. Treat this as investigation evidence, not a GPU-only metric or 60 FPS result.
- CI trigger coverage was proven green by workflow `34927971070` for `21ea21d8` (`21ea21d8338b75d5ec09731c5d71ad341857e57d`). The final layout/timing candidate needs CI at its exact head; this run does not cover uncommitted changes. These checks are not a release, shared merge, full suite, or clean-machine certification.
- The older 0.1.55 checkout/scratch work is preserved separately and must not be reapplied over the integrated 0.1.57 artwork.
