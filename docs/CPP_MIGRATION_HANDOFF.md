# C++ migration handoff

Concise cross-agent notes. Full milestone history lives in `docs/engine/MIGRATION_STATUS.md`;
subsystem state lives in `docs/CPP_MIGRATION_STATUS.md`.

## Active branches

- `engine/stellar-engine-migration` — shared migration branch (head `ac45d958`, engine 0.1.57). Do not push directly; feed via reviewed PRs.
- `cpp/devin-swe2-native-conversion` — Devin/SWE-2 working branch: diplomacy, territory, orbital, surface, audio, tactical battle, audio-settings, keyboard-parity and notification-feed slices (engine 0.1.58), pending PR into the migration branch.
- `work/stellar-engine-editor` — separate WPF editor tool (`editor/` only, 2 commits, non-conflicting).
- `work/voice-engine-tts` — fully merged ancestor of migration head.

## Battle presentation slice (`357872e8`, candidate for review)

- `app/native_client/native_battle_workspace.{hpp,cpp}` — `NativeBattleWorkspace`:
  full-screen tactical overlay ported from `MassiveCombatView`. Renders only the
  observer-filtered `MassiveCombatSnapshot` it is handed; owns no battle state.
  `handle` emits `IssueOrder`, `TogglePause`, `CycleSpeed`, `Fit`, `Menu`;
  selection, box-select, targeting pick, pan/zoom and the event feed are
  internal. `project()` is exposed for tests/smoke diagnostics.
- `CampaignFrame`: `begin_tactical(fleet_id)`, `issue_tactical_order(order)`,
  `tactical_snapshot()`; `CampaignMassiveCombat` gained the order forward.
- `NativeFleetWorkspace` details panel: ENGAGE button on armed fleets →
  `FleetWorkspaceCommand::Engage` → `begin_tactical`.
- `main.cpp`: the update loop detects `world.active_combat_encounter`, opens
  the workspace with the player observer id, refreshes it every 0.1 s of real
  time, routes `BattleWorkspaceCommand`s via `execute_battle`, and renders the
  battle last so it overlays other workspaces.
- `NativeCampaignSession`: manual saves are now allowed after tactical frames
  (`manual_capture_ready_` covers `CampaignFrameRoute::Tactical`), matching the
  reference which captures mid-battle state directly. Autosave scheduling stays
  strategic-only inside `PlayerCampaignSaveController::after_frame`.
- `stellar-continuum-native.exe --battle-smoke <bmp>` (requires `--load` with an
  active-encounter save): selects the owned formation, issues Hold, resumes the
  tactical clock, saves mid-battle, prints `battle={formations, own, foreign,
  redacted, vessels_hidden, own_inexact, selected, tokens, events, salvos,
  tick, order_accepted}`.
- `tools/author_battle_save.py` — dev helper authoring a two-front encounter
  onto the Player17 row (war contact basis + at-war relationship + bound fleets
  + unengaged foreign picket). `tools/stellar-export/native_battle_runtime.py`
  carries the same authoring for the sealed validator.

## Notification feed slice (candidate for review)

- `app/native_client/native_notifications.{hpp,cpp}` — `NativeNotificationFeed`
  (bounded 32 items, `publish(category, date, message, contact_id)`,
  `unread_count(last_read)`, sequences survive `clear()`) and
  `NativeNotificationView` (RECENT EVENTS panel; `handle` returns
  `None`/`Close`/`OpenDiplomaticContact` commands and captures only input that
  lands on the panel — reference `ContainPointerInput` behavior, no
  outside-click dismiss). `NotificationLayout`/`notification_layout_for` are
  public for tests and smoke drivers.
- `NativeCampaignSession::notifications()` + `publish_notification(...)`;
  `advance()` harvests the same step-event kinds the reference publishes plus
  observer-filtered `recent_events` diplomacy bulletins (audience-gated;
  contact id attached only for identified contacts). `seed_notification_history`
  marks retained history seen on activation/load so stale events never
  republish — the feed is session-scoped like the reference.
- `NativeUiLayout` gained `UiAction::Notifications` + `notifications` rect;
  the button shows the unread badge (gold when >0, "99+" cap). New items play
  `NativeSfx::ui_confirm`-routed event audio via `play_event(category)`.
- `NativeDiplomacyWorkspace::select_contact_civilization(int)` — the native
  `UiOpenDiplomaticContact` equivalent.
- `stellar-continuum-native.exe --notification-smoke <bmp>` (requires `--load`
  with a diplomacy-bearing fixture): submits a proposal through RELATIONS,
  opens the panel, captures it plus a `-contact` sidecar after OPEN RELATIONS
  focuses the counterpart, prints `notifications={panel, items, unread,
  diplomacy, contact, focused_civ}`.
- `tools/stellar-export/native_notification_runtime.py` —
  `validate_native_notification_export(folder, env, player17_fixture)` reuses
  the diplomacy source authoring and asserts panel/unread/contact focus,
  distinct captures, payload integrity and no retained-history flood.

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
  proposal persistence and capture variance. Sealed in export
  `StellarContinuum-windows-native-preview-7a04c0bc-20260915T124212249481Z`
  (118 files, all four diplomacy flags true, `portrait=1`).
- The communications portraits are now declared+packaged: nine entries in
  `NATIVE_SPECIES_SOURCES`/`cmake/NativeSpeciesAssets.cmake` (four base JPGs,
  four `*-communications-v2.png`, credits doc), all exact-hash.

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

- Diplomacy presentation: audited against the C# reference — the workspace has no
  grievance display, demand/trade composer, or claims panel; claims render as dashed
  arcs on the strategic map (ported in `ef7a4007`) and the native workspace covers
  every section the reference renders. No further diplomacy port is currently owed.
- Surface colony visuals — hub/buildings/roads/ghosts now render as rasterized
  sprites (`58aaf475`); the remaining gap is the reference's free camera orbit
  and terrain relief, not building art.
- Voice-duck hooks — mixer/playback/persistence/settings UI shipped
  (`dbf07f81`, `d108435a`); the remaining work is wiring the duck ramp to real
  voice playback once voice lands.
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
