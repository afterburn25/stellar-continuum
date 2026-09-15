# C++ migration handoff

Concise cross-agent notes. Full milestone history lives in `docs/engine/MIGRATION_STATUS.md`;
subsystem state lives in `docs/CPP_MIGRATION_STATUS.md`.

## Active branches

- `engine/stellar-engine-migration` — shared migration branch (head `ac45d958`, engine 0.1.57). Do not push directly; feed via reviewed PRs.
- `cpp/devin-swe2-native-conversion` — Devin/SWE-2 working branch: docs commit `8c48a6c7` + diplomacy presentation slice (engine 0.1.58), pending PR into the migration branch.
- `work/stellar-engine-editor` — separate WPF editor tool (`editor/` only, 2 commits, non-conflicting).
- `work/voice-engine-tts` — fully merged ancestor of migration head.

## Interfaces added in 0.1.58 (candidate for review)

- `app/native_client/native_diplomacy_controller.{hpp,cpp}` — `NativeDiplomacyController`:
  `build(frame, generation, contact_index)` returns `NativeDiplomacyView` (contacts,
  selected details, proposals, agreements, history + `diplomacy_revision`);
  `execute(frame, generation, revision, action, target, proposal_id)` revalidates the
  quoted revision against a fresh signature before calling
  `ObserverDiplomacyCommandService`. Signature covers contact awareness/identity,
  relationship metrics, access, agreements, proposals and event ids — selection moves
  never bump it. Owner-thread pinned like other controllers.
- `app/native_client/native_diplomacy_workspace.{hpp,cpp}` — fullscreen RELATIONS
  workspace. `handle` emits `SelectContact`, `Action`, `ProposalAction`,
  `FocusSystem`, `Close`; `set_view` preserves selection by civilization id across
  contact reordering (main re-projects once if the index shifted).
- `NativeUiLayout` gained `UiAction::Diplomacy` + `diplomacy` rect (top bar, RELATIONS).
- `NativeDiplomacyWorkspace::render` takes an optional `PortraitProvider`
  (`string_view relative_asset_path -> shared_ptr<const RgbaImage>`); `nullptr` draws
  the signal-waveform fallback. The workspace resolves
  `assets/visual/species/<species>-communications-v2.png` (underscores→hyphens).
- `stellar-continuum-native.exe --diplomacy-smoke <bmp>` (requires `--load`):
  opens RELATIONS, selects the identified channel contact, drives the
  negotiation modal to send a transit-access request, captures the workspace
  plus a `-proposals` sidecar, prints `diplomacy={...}` (contacts, redaction,
  channels, agreements, history, portrait, command outcome, proposal ids).
- `tools/stellar-export/native_diplomacy_runtime.py` —
  `validate_native_diplomacy_export(folder, env, player17_fixture)` authors an
  unresolved-signal contact + pending incoming access petition onto the
  fixture (tick = SimulationDays × 1000), runs the smoke twice (fresh +
  paused reload), and verifies redaction, portrait, command acceptance,
  proposal persistence and capture variance.

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

- Diplomacy presentation gaps vs C#: no claims/border-warnings UI, no demand/trade
  proposal composer (terms list covers non-aggression/access/peace/ceasefire only),
  no grievance display.
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
