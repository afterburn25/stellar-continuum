# C++ migration handoff

Concise cross-agent notes. Full milestone history lives in `docs/engine/MIGRATION_STATUS.md`;
subsystem state lives in `docs/CPP_MIGRATION_STATUS.md`.

## Active branches

- `engine/stellar-engine-migration` — shared migration branch (head `ac45d958`, engine 0.1.57). Do not push directly; feed via reviewed PRs.
- `cpp/devin-swe2-native-conversion` — Devin/SWE-2 working branch; observed at `06b2b802`, with diplomacy presentation at `410753da` integrated here as `c9338699`. Combined focused and graphical validation passed; the candidate remains under review.
- `cpp/codex-native-architecture-integration` — Codex architecture/integration branch, based on `ac45d958`. Carries Devin's existing coordination documents forward; changes go through a PR to the shared migration branch.
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
  never bump it. Review hardened the signature with length-prefixed strings, classic
  locale/max-digits precision, collection counts and full proposal/terms fields.
  Owner-thread pinned like other controllers.
- `app/native_client/native_diplomacy_workspace.{hpp,cpp}` — fullscreen RELATIONS
  workspace. `handle` emits `SelectContact`, `Action`, `ProposalAction`,
  `FocusSystem`, `Close`; `set_view` preserves selection by stable `contact_id`
  across contact reordering, including unidentified contacts. Main immediately
  re-projects `SelectContact`, even while paused.
- `DiplomacyWorkspaceCommand` now carries `campaign_generation` and
  `diplomacy_revision`. Action and confirmation commands retain the quote shown
  to the player; main passes these to `execute`, not the latest view revision.
  Refreshed generation/revision dismisses an obsolete modal with an explanation.
- `NativeUiLayout` gained `UiAction::Diplomacy` + `diplomacy` rect (top bar, RELATIONS).
- `NativeDiplomacyWorkspace::render` takes an optional `PortraitProvider`
  (`string_view relative_asset_path -> shared_ptr<const RgbaImage>`); `nullptr` draws
  the signal-waveform fallback. `main.cpp` resolves
  `assets/visual/species/<id>-communications-v2.png` (underscores→hyphens).
  The exact four reviewed PNGs are now declared/copied by CMake and the exporter.
  The lazy cache is limited to four entries/32 MiB (24 MiB actual decoded RGBA);
  declared art failures include the path and cause. Unknown species use waveform.
- Scroll drawing and hit rectangles are intersected with their visible region.
  Intelligence cards/focus controls use a non-overlapping stack. Filter widths
  give long labels enough room at 720p without shrinking their text.

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

## Validated diplomacy integration

- Diplomacy smoke now covers 1280×720 and 1920×1080 Vulkan launches, contact
  selection, acceptance, scrolling, reload, communications PNG display, waveform
  fallback, and observer secrecy. Player17 starts with reciprocal known contacts;
  the isolated copy adds an unknown contact, a second known contact, and a pending
  incoming research-exchange proposal. Four communications PNGs use a bounded
  32 MiB cache (about 24 MiB decoded RGBA).
- `native_diplomacy_runtime.py` authors only an isolated test save. Baseline
  canonicalization follows `player_campaign_json_tests.cpp`: omit fixture-only
  `Control`, convert numeric X/Y to float32; contacts are authored in the native
  snapshot order. No general numeric tolerance or simulation changes are used.
  Only the intended proposal, new agreement, two events and counters may change.
  Reload compares the entire saved payload except `SavedAtUtc` exactly.
- Final combined check: seven CTests (diplomacy controller/workspace, native input,
  system workspace/travel, diplomacy observer-command parity, Player17 JSON parity),
  18 diplomacy-validator and 45 package/checkout Python tests, six actual Vulkan
  launches (diplomacy/system/galaxy, 720p and 1080p). Evidence:
  `native-diplomacy-final.log`, `work/native-diplomacy-final-{diplomacy,system,galaxy}.json`,
  and `build-native/preview-*.bmp`. Known/unknown portrait captures were inspected.
- Final diplomacy frame means 17.790–17.791 ms, p95 28.367–32.663 ms; system/galaxy
  means 20.748–21.215 ms, p95 33.458–33.937 ms. These short VSync-inclusive samples
  do not establish sustained 60 FPS. This is Engine 0.1.58 candidate validation,
  not a sealed release or clean-machine result.

## Async manual-save checkpoint

- `PlayerCampaignSaveController::begin_manual(runtime, options)` captures the immutable
  Player17 DTO on the simulation owner thread and submits encoding/atomic IO to the
  existing single writer. `nullopt` means admitted, not durably saved; an immediate
  capture/submission error is returned. `complete()` reports the actual outcome.
  Synchronous `save_manual()` remains for explicit durable shutdown/reference use.
- Native manual-save requests coalesce behind an active writer. Completion is polled
  through `advance()` or `service()` (also while minimized); a failed completion
  cancels queued save/exit requests so an immediate retry cannot conceal the error.
  Explicit load/exit still drain synchronously and in order. Existing autosave retry
  scheduling, backup protection, thread ownership and Player17 schema remain intact.
- Manual-save update maxima, before → after: system 130.901/130.302 → 7.272/2.610 ms;
  galaxy 130.953/129.773 → 5.080/2.955 ms; travel 32.392–35.940 → 1.008–1.157 ms.
  These are CPU update measurements. Cold system scene construction remains
  ~340–346 ms. Frame/presentation measurements include VSync and still show a
  ~33 ms p95; this does not establish sustained 60 FPS.
- Four focused save/session/JSON/recovery CTests and nine actual Vulkan launches passed: seven
  galaxy/system/travel and two diplomacy, covering 720p/1080p, ordered transit,
  observer secrecy, durable writes and exact paused reload. Native test details
  are recorded in `native-save-background-tests.log`.
  Other evidence: `native-save-background-{runtime,diplomacy}.log`,
  `work/save-baseline-{system,galaxy,travel}.json`,
  `work/layout-{system,galaxy,travel}.json`, `work/save-background-diplomacy.json`.
- Diplomacy head `0e0835ae104be5582cae1061a36bc96e04f77286` passed native CI
  `34934085259`. The subsequent save candidate needs its own exact-head CI.

## Cold celestial preparation checkpoint

- `native_celestial_appearance.cpp` no longer evaluates sunspots/granulation for
  halo pixels with zero photosphere coverage. It also skips corona math beneath
  the fully opaque disc and outside the corona's zero-coverage boundary. Original
  surface/limb/corona equations, 1024px resources, intermittent flares, cache limits,
  spectral colors and blend order remain unchanged.
- Existing repository tests now accept diagnostic captures:
  `stellar_celestial_tests --capture <directory>` and
  `stellar_native_planet_disc_assets_tests --capture <directory> <Sol asset root>`.
  They write raw RGBA plus dimensions/timing profiles after running their assertions;
  default CTest behavior is unchanged. No production helper or new runtime is added.
- Baseline `b110e223` renderer versus candidate: all 19,398,656 bytes in eight
  captures match exactly (three star colors/seeds, black hole, both ring halves,
  Mercury and Neptune). Star generation fell from 207.7–210.4 to 79.8–80.3 ms.
  Actual first system scene fell from 340–346 to 209.7–213.0 ms. Galaxy-to-system
  capture transitions measured 209.5–209.9 ms. These include remaining planet
  preparation and text work; they are not a claim of stall-free entry.
- Strict build and five CTests passed: celestial appearance, planet discs, system
  workspace, system colony entry and settlement workspace. Four final Vulkan
  system/galaxy launches passed at 720p/1080p, including paused save/reload and
  observer checks. Evidence: `native-celestial-cold-tests.log`,
  `native-celestial-cold-final-runtime.log`, `work/celestial-{before,after}/`,
  `work/celestial-pixel-comparison.json`, `work/cold-{before,after}-{system,galaxy}.json`.
- The saved-game fix at `b110e223` passed the Windows export job of native CI
  `34936942630`. The subsequent celestial candidate needs its own exact-head CI.

## Steady-frame profiling checkpoint

- Native map smoke accepts `--profile-frames N` (120–3600) with an isolated
  `--system-smoke` or `--galaxy-art-smoke`. The original warm-up/save frames remain;
  N extra steady frames precede the original screenshot/transition sequence.
  Normal runs and existing smoke defaults remain unchanged. Minimize/restore
  interruption cannot silently count a discarded interval as a valid sample.
- Engine `Window::draw` takes an optional `FrameTiming*`, reset per draw, reporting
  CPU submission, screenshot readback/write, fallback throttle and present times.
  No per-phase clocks or timing history are added to normal rendering. Presentation
  includes driver/display waiting and is not a GPU execution measurement.
- `steady_profile` reports exact sample count and mean/p50/p95/p99/max for interval,
  update, scene, submission, readback, throttle and present. The maintained system
  and galaxy export validators accept `profile_frames=600`, validate the complete
  diagnostic and still verify artwork, observer secrecy, saves and exact reload.
  No fixed hardware-independent FPS pass threshold is imposed.
- Four actual Vulkan runs, each 600 extra frames, passed at 720p and 1080p. Paused
  500-system campaign interval means were 16.716–16.723 ms; p95 16.834–17.025 ms;
  p99 17.284–19.705 ms. Update/scene/submission means were respectively
  0.026–0.033 / 0.053–0.175 / 0.258–0.323 ms. Present means 16.158–16.355 ms
  dominate; readback and fallback throttle were zero in all steady samples.
  This supports roughly 60 FPS after warm-up on this host, not busy campaigns,
  every resolution/hardware, stall-free entry or full visual parity.
- Strict native build, real Vulkan platform timing/reset/pixel checks, 34 focused
  Python tests, and four invalid native CLI cases passed. Two default-frame
  diplomacy Vulkan launches also passed UI/portrait/secrecy and exact reload
  checks (`native-steady-default-runtime.log`, `work/steady-default-diplomacy.json`). Evidence:
  `native-steady-profile-{build,tests}.log`, `native-steady-baseline-runtime.log`,
  `work/steady-baseline-{system,galaxy}.json` and `build-native/preview-*.bmp`.
- The previous celestial optimization `c373aed4` passed native CI `34939226242`.
  Profiling head `b491783c` also passed native CI `34942125331`; no shared merge.

## Background system-art preparation checkpoint

- Engine `ImagePreparationQueue` wraps one existing `JobSystem` worker. Owner-thread
  admission returns a move-only ticket or no capacity; polling never waits for
  rasterization. Defaults: 16 jobs / 32 MiB reserved output, including ready but
  uncollected results. Decoder scratch is separate (existing WIC bound, one decode
  at a time). Ticket cancellation drops queued/running results without waiting;
  explicit queue destruction joins the worker. Ready failures rethrow on `take()`.
- `NativePlanetDiscAssets::request_image` and celestial
  `use_background_preparation` opt into this queue. The existing synchronous path
  remains available for tools and exact pixel comparison. Pure workers own copied
  appearance keys/root paths, never Core, SDL, GPU or UI references. Only eligible
  observer-filtered data can request a body asset. Campaign discard cancels old
  requests; all ready results drain before admitting more work, even after moving
  away from the body that requested them. Existing cache budgets remain intact.
- `NativeSystemWorkspace::artwork_ready()` describes the last rendered frame.
  Temporary discs/progress text keep navigation usable while preparation runs.
  Smoke capture and multi-screen transitions wait for the actual final artwork,
  with a bounded failure, without counting extra wait frames in `steady_profile`.
  Diagnostics expose pending frames, preparation wall time and capture wait frames.
- Strict native build and six focused CTests passed; the added readiness/visible
  progress/mouse-navigation regression subsequently passed, as did 42 Python
  system/galaxy/travel export checks. Celestial and planet tests compare complete
  final RGBA output with synchronous generation and exercise duplicates, saturation,
  abandoned requests, generation cancellation, missing assets and worker errors.
- Seven actual Vulkan launches passed: system/galaxy at 720p and 1080p with 600
  steady frames each, plus three travel/save/reload runs. System cold scene CPU:
  211.452/211.567 -> 2.936/2.807 ms. Galaxy capture-transition scene maximum:
  207.769/209.287 -> 19.779/19.577 ms (remaining regional scenery preparation).
  Artwork completes asynchronously in 213–258 ms; galaxy captures waited 15/16
  frames and their completed Sol images were visually inspected. Warm interval
  mean 16.717–16.722 ms, p95 16.913–17.026 ms, p99 17.487–18.950 ms.
- Evidence: `native-background-art-tests.log`,
  `native-background-art-capture-tests.log`, `native-background-art-owner-tests.log`,
  `native-background-art-runtime.log`,
  `work/background-art-{system,galaxy,travel}.json`, and `build-native/preview-*.bmp`.
  Baselines remain `work/steady-baseline-{system,galaxy}.json`. Native CI for this
  new checkpoint is still required; `b491783c` passed `34942125331`. No release,
  shared merge, full-suite run or clean-machine certification is claimed.

## Remaining blockers / next work

- Diplomacy presentation gaps vs C#: no claims/border-warnings UI, no demand/trade
  proposal composer (terms list covers non-aggression/access/peace/ceasefire only),
  no grievance display.
- Initial galaxy scenery still costs 45–46 ms, regional scenery about 20 ms;
  upload/presentation tails also remain. System CPU rasterization is now staged
  as above. Next inspect these measured remaining stalls, then profile busy
  campaigns separately before making a general 60 FPS claim. Keep Core access
  and GPU/window work on the owner thread, preserve final pixels and capture gates.
- Surface colony visuals (buildings/roads), orbital structure rendering.
- Native audio — engine has no audio module at all; needs design before code.
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
- Layout/timing head `66c56b897ad04a99d2e662a2fb089cdcc160186b` passed GitHub workflow `34929897092`; diplomacy head `0e0835ae` subsequently passed `34934085259`. Neither covers the subsequent async-save changes. No shared merge or clean-machine certification is claimed.
- The older 0.1.55 checkout/scratch work is preserved separately and must not be reapplied over the integrated 0.1.57 artwork.
