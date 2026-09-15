# C++ migration handoff

Concise cross-agent notes. Full milestone history lives in `docs/engine/MIGRATION_STATUS.md`;
subsystem state lives in `docs/CPP_MIGRATION_STATUS.md`.

## Active branches

- `engine/stellar-engine-migration` — shared migration branch (head `ac45d958`, engine 0.1.57). Do not push directly; feed via reviewed PRs.
- `cpp/devin-swe2-native-conversion` — Devin/SWE-2 working branch: diplomacy, territory, orbital, surface, audio, tactical battle, audio-settings, keyboard-parity, notification-feed and support-bundle slices (engine 0.1.58), pending PR into the migration branch.
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

## Candidate-shortcut slice (candidate for review)

- `main.cpp` `KeyPressed` dispatch gains T/R/C/B alongside Space, 1–4 and F6 —
  T cycles the startable research candidates, R starts the current candidate,
  C/B do the same for construction projects. Candidate sets come from each
  controller's `primary_action.enabled`/`start.enabled` projection (view
  ordering; the reference's plan-ranked ordering is not reproduced) and the
  construction active-project guard matches the reference wording.
- `NativeCampaignSession::publish_status` adds `SessionNoticeKind::Status` —
  the reference `SetStatus` equivalent — used by the shortcut paths for
  candidate names, acceptances and rejections.
- `--research-smoke` sends T then R after the canonical button start and emits
  `shortcut=1` when a status notice surfaced; `--construction-smoke` sends C
  then B (status-only on both launches). `native_research_runtime.py` and the
  construction path in `native_production_runtime.py` now require the flag,
  with negative mock tests in `test_native_client_runtime.py` /
  `test_native_production_runtime.py`.
- N / NEW GAME is now ported (see below). F8 is covered by the
  support-bundle slice below.

## Mid-session New Game slice (candidate for review)

- `N` and a new pause-menu NEW GAME row port `UiNewCampaign`: the live
  campaign saves first (`request_new_game` → `request_save`; the outer
  campaign-lifetime loop in `main` only exits the frame loop once the Saved
  notice lands, and aborts the request on a Failure notice). The full startup
  sandbox (`run_native_startup_entry`) then runs again — species/size/seed →
  Create → generation — and a committed session replaces the campaign in
  place. Cancelling setup calls `release_session` and resumes the saved
  campaign, matching the reference's mode-select cancel.
- `native_ui_layout` gained the 7th menu button (panel 378→425px, hit-tested).
- `--new-game-restart-smoke` loads an anchor save, sends a real `N`
  `KeyPressed` at frame 55, lets the save gate the transition, runs automated
  sandbox setup (seed+1), and saves the second campaign — emitting a
  `new_game_restart={…}` diagnostic (saved_previous, restarted, full startup
  evidence, species/seed/system count, isolated slot path).
- `native_new_game_runtime.py` now launches the restart smoke after the
  fresh/reload proofs: it validates the diagnostic schema, the re-saved prior
  campaign, the `…-native-N` slot with seed 143251, and the three restart
  captures. `test_native_new_game_runtime.py` adds 7 negative tests (39
  total).

## Support bundle slice (candidate for review)

- `app/native_client/native_support.{hpp,cpp}` — `NativeSupportLog` ports
  `SupportLogger`: a 12-hex-char session id, `logs/game-<id>.log` +
  `logs/system-<id>.txt` under `<save-dir>/`, and `export_bundle(save)` →
  `support/support-<id>-<yyyymmdd-hhmmss>.zip` as a store-format (method 0)
  ZIP with UTF-8 name flags — log, system info and save, matching the
  reference's three entries. Verified by `native_support` tests (CRC32,
  central-directory parse, content round-trip) and Python's `zipfile`.
- `main.cpp`: `enable_support_log` is enabled per campaign after window
  creation (system info from SDL: platform, CPU cores, RAM, GPU driver).
  F8 and the new pause-menu SUPPORT BUNDLE button call
  `export_support_bundle`, reporting the path via `publish_status`.
- `--audio-smoke` now clicks SUPPORT BUNDLE, closes the menu and presses F8,
  then emits `support=1` when a non-trivial `.zip` exists; the audio export
  validator requires the flag and validates the bundle with `zipfile`. The
  smoke lands a save before exporting so the bundle always carries its save
  entry (this ordering fixed the earlier two-entry-ZIP export failure).

## Voice slice (candidate for review)

- `app/native_client/native_voice.{hpp,cpp}` — `NativeVoiceProfileRegistry`,
  `NativeCharacterVoiceResolver` and `NativeVoiceRouter` port the reference
  `VoiceEventRouter`/`CharacterVoiceResolver` over the reviewed
  `Data/voice_profiles/{events,human,roles}.json` catalogue.
- `app/native_client/native_voice_playback.{hpp,cpp}` —
  `NativeVoicePlayback` ports `VoicePlaybackController` (queue/dedupe/
  interrupt/expire/caption semantics); `NativeVoiceCache` validates hashed
  PCM WAVs under a bounded byte budget; `native_voice_sapi.cpp` is the
  Windows SAPI 5 backend (STA worker, 22.05 kHz 16-bit mono WAV, voice
  selection by preferred id/description then gender/culture, 30 s timeout
  and generation-based cancellation).
- `app/native_client/native_voice_bridge.{hpp,cpp}` —
  `NativeGameplayVoiceBridge` ports `GameplayVoiceEventBridge`/`Main.Voice`:
  research/construction/ship/exploration/contact/combat/diplomacy/economy/
  logistics routing, opening line, milestone tracking and baselines. It reads
  only observer-authorized state (`build_view_for`, own events, own fleets).
- `native_audio` gained the dedicated dialogue voice: `play_dialogue`,
  `stop_dialogue`, `set_dialogue_volume`; the duck ramp is driven live by
  `NativeVoicePlayback::ducking()`.
- `main.cpp` `initialize_voice()` loads the catalogue, attaches the SAPI
  backend, binds decode/play/stop to the mixer and wires the bridge into
  `advance()`; captions render bottom-center and hide while the menu or
  relations workspace is open. Missing catalogue files disable voice without
  failing the campaign. `--audio-smoke` reports `voice_pipeline`,
  `voice_backend`, `voice_lines`.
- `native-tests/native_voice_tests.cpp` covers routing authorization,
  dedupe/once/cooldown/frequency, deterministic variants, template
  rejection, resolver species safety, settings round-trip, WAV validation,
  and playback queue/subtitle behavior with a fake backend.
- `export/native-voice-assets.json` + `cmake/NativeVoiceAssets.cmake` gate
  the three catalogue files by exact path and SHA-256.

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
- Voice — the full presentation pipeline landed (catalogue, router, playback,
  SAPI backend, WAV cache, captions, mixer dialogue voice + ducking, gameplay
  bridge). Remaining: the reference's offline-neural backend and the
  voice-settings window.
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
