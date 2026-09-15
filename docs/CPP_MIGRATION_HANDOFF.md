# C++ migration handoff

Concise cross-agent notes. Full milestone history lives in `docs/engine/MIGRATION_STATUS.md`;
subsystem state lives in `docs/CPP_MIGRATION_STATUS.md`.

## Active branches

- `engine/stellar-engine-migration` — shared migration branch (head `ac45d958`, engine 0.1.57). Do not push directly; feed via reviewed PRs.
- `cpp/devin-swe2-native-conversion` — Devin/SWE-2 working branch; observed at `8c48a6c7` (coordination documents atop migration head).
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

- Codex's next renderer investigation: separate CPU scene construction/submission time from presentation wait. Recent actual galaxy checks remain around 21 ms mean and 33–37 ms p95; the circle optimization alone does not establish 60 FPS.
- Visual review of the native 720p Sol view still shows a very small fitted orbital scene and overlapping planet labels. Improve camera framing/label placement through presentation-only changes; do not rescale authoritative AU positions.
- Diplomacy native workspace (sim ported, no UI) — highest-value presentation slice.
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

- PR #325 (draft) + issue #324 are the communication hub; update, don't close.
- Sealed export = `tools/stellar-export/stellar.py export windows-native-preview`.
- Dev env: VS 2022 BuildTools `VsDevCmd` + `.tools/build-tools` venv (cmake/ninja/python).

## Codex checkpoint: checkout reproducibility and renderer overhead

- A fresh Windows checkout at `ac45d958` failed CMake's ship-art credits hash check: Git converted the reviewed LF file to CRLF. `.gitattributes` now pins all six hash-verified native text assets to their existing reviewed byte formats. No manifest hash or integrity check is weakened. A real Git checkout regression covers `core.autocrlf=true` and `false`.
- Soft-circle submission retains the same 20 triangles and ordered blending, using stack vertices and cached directions/indices instead of two heap allocations and 42 trigonometric evaluations per circle. Engine remains independent of Core.
- Validation: native C++ client build; 43 Python dependency checks; 2 focused Vulkan/text CTests; complete GPU pixel comparison with the former circle algorithm; malformed geometry diagnostics. Four actual native launches exercise 720p/1080p galaxy-to-system views and ship art/routes with exact paused Player17 reload checks. Galaxy captures and the 720p Sol view were inspected.
- Local evidence in the Codex checkout: `native-build.log`, `native-dependency-tests.log`, `native-platform-tests.log`, `work/native-galaxy-validation.json`, `work/native-ship-validation.json`, and `build-native/preview-*.bmp`. These are development checks, not a sealed release, full-suite rerun or clean-machine certification. The last validated application was still Engine 0.1.57; no release version bump or shared-branch merge occurred.
- The older 0.1.55 checkout/scratch work is preserved separately and must not be reapplied over the integrated 0.1.57 artwork.
