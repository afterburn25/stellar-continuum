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
  Baselines remain `work/steady-baseline-{system,galaxy}.json`. System-art head
  `96b83092` passed native CI `34946960470`; `b491783c` passed `34942125331`. No release,
  shared merge, full-suite run or clean-machine certification is claimed.

## Background galaxy-art preparation checkpoint

- `NativeGalaxyBackdropAssets` now offers `request_deep_field`,
  `request_galaxy_layer`, `request_regional_nebula`, `pending_count` and
  `cancel_preparation`, using the same Engine queue as system artwork. Sources
  use absolute paths; workers own copied paths/labels only. Owner-thread polling
  drains all ready results, including abandoned-view requests. Queue saturation
  returns pending and is retried; campaign discard cancels outstanding tickets.
  The synchronous API remains for tools and exact comparisons.
- Each source is decoded and validated locally before cache assignment, with an
  8 MiB per-image limit and three cache slots (24 MiB maximum). Errors preserve
  source label, full path and underlying cause. Failed/oversized results never
  populate the cache. Existing decoder scratch bounds remain separate.
- `NativeGalaxyBackdrop::artwork_ready()` covers the last appended frame. Missing
  layers wait independently; the central secrecy fog remains drawn throughout.
  Main uses the existing status region for preparation feedback and applies the
  existing bounded final-art capture gate to galaxy views too. Finished artwork,
  geometry, colors and rendering order are unchanged.
- Strict native build, three focused CTests and 46 Python export checks passed.
  Tests cover blocked/saturated queues, no duplicate work, view changes, discard,
  owner guards, missing/oversized sources and exact final RGBA equality. The
  maintained backdrop test has a 45-second timeout and cleans only its own
  verified temporary fixture directory.
- Six actual Vulkan launches passed at 720p/1080p: galaxy and Sol with 600 steady
  frames each, plus default diplomacy checks. Existing artwork, input, secrecy,
  save and exact paused Player17 reload gates remain intact. Galaxy first-scene
  CPU maxima: 45.432/46.248 -> 3.163/2.695 ms; regional capture transitions:
  19.779/19.577 -> 1.027/0.833 ms. Sol remains 2.907/2.867 ms. All four completed
  overview/regional BMP files match `96b83092` byte for byte.
- Steady interval means were 16.716–16.721 ms, p95 16.935–17.087 ms and p99
  17.338–19.141 ms. Cold render/present still reaches ~67 ms at frame 7; its
  submission/driver/display contribution is not yet isolated. No broad 60 FPS
  certification, visual-parity, sealed-release or shared-merge claim is made.
- Evidence: `native-galaxy-background-tests.log`,
  `native-galaxy-background-runtime.log`, `work/galaxy-background-{galaxy,system,diplomacy}.json`,
  `work/galaxy-background-pixel-comparison.json`, and `build-native/preview-*.bmp`.
  Previous system-art head `96b83092` passed native CI `34946960470`; this new
  checkpoint needs its own run.

## Cold rendering diagnostic checkpoint

- `--profile-frames` now also emits `cold_profile={"rows":[...]}` for exactly the
  first ten rendered frames. Rows contain frame number, update/scene/submission/
  readback/throttle/present/render-present milliseconds and image-upload counters
  before/after the draw. This is a fixed-size diagnostic; normal play adds no
  history, per-phase clocks or upload-counter queries. Engine behavior is unchanged.
- The system/galaxy exporter returns `systemColdProfiles` / `galaxyColdProfiles`
  only when profiling is requested. Parsing rejects missing/duplicate tokens or
  JSON members, incomplete/out-of-order frames, nonfinite/negative timings,
  screenshot readback, backwards upload counters and subphases exceeding their
  enclosing draw (with three-decimal rounding allowance).
- Strict native build and 57 Python checks passed. Four actual 600-frame Vulkan
  galaxy/system profiles at 720p/1080p passed the existing artwork, UI, observer
  and exact paused save/reload gates. Two default-frame diplomacy Vulkan launches
  passed after restoring the unchanged Engine configuration.
- Frame 7 contains 65.471–66.460 ms of present time. Sol uploads no images on that
  frame and queues drawing in 0.236/0.258 ms; galaxy submission is 1.563/1.687 ms.
  Present includes SDL's deferred command flush and GPU/driver/display waits,
  not exclusively display waiting or GPU execution. The exact lower-level cause
  remains unresolved. Paused steady means remain 16.717–16.722 ms.
- A local one-frame-in-flight experiment was tested in four 120-frame map profiles.
  It did not change the ~66–67 ms cold tail and was fully reverted before final
  build/validation. Do not repeat that setting change without new evidence.
- Evidence: `native-cold-render-runtime.log`, `work/cold-render-{galaxy,system}.json`,
  `native-cold-profile-final-validation.log`, `work/cold-profile-default-diplomacy.json`.
  Discarded experiment: `native-cold-queue-experiment-{build,runtime}.log` and
  `work/cold-queue-one-{system,galaxy}.json`. Galaxy head `63d62d31` has native CI
  `34950285134` in progress; the new diagnostic needs its own run. No shared merge.

## Running-campaign performance checkpoint

- New opt-in `--campaign-profile <capture.bmp>` requires an isolated `--save-path`
  and `--profile-frames 120..3600`; it is exclusive with other graphical modes.
  Optional `--load` resumes an existing Player17 campaign. Normal smoke defaults
  and normal gameplay are unchanged; no simulation rules or fake fleets are added.
- Warm-up is paused through frame 119. UI speed clicks select Maximum (8X),
  then UI resume/pause clicks bracket the exact requested active frames. Real
  elapsed time goes through normal Player CampaignFrame policy. A manual save
  is requested midway; completion and subsequent time advancement must both be
  observed. Final UI pause freezes the day while the last save and artwork finish.
  Session/save failures fail the check instead of being retried.
- `native_campaign_profile.py` runs active 720p fresh and active 1080p reload,
  each followed by an independent paused reload/resave. Entire Player17 payloads
  must match except SavedAtUtc, including the frozen day; the active source copy
  must remain unchanged. Strict parsing rejects wrong speed/count/flags,
  unordered/nonfinite days and malformed/duplicate metrics. See the validation
  document for invocation and explicit early-campaign scope.
- Evidence: strict native build; four CTests (native_campaign_session,
  native_client_input, campaign_frame_parity, strategic_clock_parity); 88 Python
  checks; four invalid CLI cases; four active/paused Vulkan launches and two
  unchanged default system launches. The first CTest attempt found two unbuilt
  test executables; building their canonical targets resolved it and all four
  passed. No scratch checker or .NET process is involved.
- 600 measured frames per active run: day 0 -> 80.2483424 -> 160.514568.
  Interval mean/p95/p99: 720p 16.718/16.917/18.181 ms; 1080p
  16.722/17.113/18.817 ms. Update means 0.305/0.359 ms, maxima 5.941/4.434 ms;
  readback and fallback throttle are zero. This does not prove late-game workload
  or every frame/hardware configuration at 60 FPS.
- Logs: `native-active-campaign-{build,unit,runtime,validation}.log`;
  JSON: `work/active-campaign-baseline.json`,
  `work/active-profile-default-system.json`; screenshots:
  `build-native/preview-active-{1280x720,1920x1080}.bmp`. The 1080p capture was
  visually inspected: completed galaxy artwork, secrecy fog, final pause/day/save.
- Previous cold diagnostic `86445dde` passed native CI `34952179710`.
  This checkpoint needs its own CI; Engine 0.1.58 remains a candidate in PR #332.
- Read-only audit found known-system vector/set rebuilding every frame and fleet
  projection every 0.1 seconds even when its workspace is hidden. Early active
  timings do not justify speculative changes. First reproduce a developed
  campaign workload with real fleets/colonies using existing progression or an
  explicitly documented fixture, then optimize measured costs with observer and
  whole save/reload checks. Keep required simulation steps authoritative.

## Developed campaign save repair and fleet workload

- A real 24-ship campaign written by the native game failed explicit loading at
  research restore (82%): `Format v17 Adaptive Research state could not be decoded.`
  `AdaptiveResearch.Civilizations[0].Research.Outcomes.RecentRecords[0].Outcome`
  contained `"hypothesisSupported"`; the canonical ordered reader requires Int32.
  The existing empty-outcome fresh fixture could not expose this writer defect.
- Player17 now writes typed numeric values only at schema-defined paths: Outcome,
  tacit ScopeKind/AssimilationStage, and four foreign-assessment axes. Standalone
  Adaptive Research codecs, strict reader, C# fixtures and simulation stay unchanged.
  The populated enum regression preserves ordinary text. Negative seed 115500 and
  positive 115501 additionally restore/activate/recapture every payload field and
  array element; JSON dictionary member order is intentionally ignored (Leadership
  is reordered on restore), without excluding any campaign fields.
- The maintained fresh-progression executable can export `--profile-save` to an
  absolute nonexistent path. It uses actual controllers and paid research/builds:
  12 scouts + 12 science vessels, day 10154, treasury 346.233, nine total colonies
  (three owned). All ships receive ordinary routes and enter transit before save.
  Offline Developer stepping is disclosed; no funds, capabilities or fleets are
  injected. An initial 32-ship attempt exhausted the real budget after 25 ships;
  the reproducible benchmark uses 24 rather than bypassing that limit.
- `validate_native_campaign_profile` accepts `initial_save` and
  `minimum_moving_fleets=24`. It copies the source to an isolated slot, requires
  actual motion by stable fleet ID, performs two 600-frame Player-policy runs at
  8X and two independent paused reloads, and verifies the original source bytes.
  All 24 move in both intervals; all remain in transit after 720p but have arrived
  by the end of 1080p. This is not a claim of 24 transiting ships on every frame.
- Eight focused CTests, 17 Python checks, strict MSVC build, four active/paused
  Vulkan launches and two default system runs pass. Interval mean/p95/p99:
  720p 16.713/17.401/18.516 ms; 1080p 16.714/17.604/18.560 ms. Update mean/max:
  0.476/6.154 and 0.385/3.586 ms. Days 10154 -> 10234.2218632 -> 10314.4471072.
  The 1080p capture was inspected for finished art, fleet outliner and final pause.
- Source `work/developed-fleet-24-fixed.player17.json` SHA-256:
  `b5a57777a4eacc57458ee92c5ebc74cc77aaea5409732e86d665e7151915a17f`.
  Results `work/developed-fleet-24-profile.json` and
  `work/developed-fleet-fixed-system.json`; logs
  `native-developed-fleet-fixed-{validation,runtime}.log` and
  `native-developed-fleet-final-unit.log`. The older active fixture and runtime
  failure log remain local generated evidence; do not hand-edit them into passing.
- Prior `370079d0` passed native CI `34955639654`. This repair requires its own run.
  Engine 0.1.58 is still an unmerged candidate in PR #332, not a sealed release.

## Native audio checkpoint (2026-09-15, candidate)

- Engine `native_audio` adds immutable 48 kHz stereo clips, Windows Media
  Foundation decoding and SDL3 output. No Core dependency or new decoder DLL.
  One music stream queues at most 288000 bytes; eight finite SFX voices are each
  capped at 1 MiB. Decode runs on one Engine worker; window/output/Core stay on
  the owner thread. The director closes audio before Window's SDL teardown.
- `NativeAudioDirector` decodes the existing user score and six WAV cues once.
  Startup waits for staging or a reported audio failure, stays silent during
  boot, then starts music once at menu admission. Campaign entry does not restart
  it. Startup actions and campaign navigation confirm with the existing cue.
  Hover/event APIs exist; settings, voice and full event hooks are still pending.
- Exact-hash CMake/export manifests package only seven clips and their credits.
  Credits use pinned CRLF bytes; both Git checkout modes pass. Application-only
  MFPlat/MFReadWrite imports are reviewed without broadening SDL's allowlist.
- Five focused CTests and 92 Python checks pass. Relocated Vulkan new-game 720p
  and paused reload 1080p pass with `audio_check=True`, restricted PATH, Unicode
  isolated save paths, independent new slot and complete Player17 equality.
  Each starts music once, queues 288000 bytes and stops cleanly. Fresh startup
  records 38 silent boot services and one confirmation; reload records zero for
  both. Real default output is used; these diagnostics do not verify speakers
  audibly or voice quality. An unavailable SDL audio driver fails the opt-in
  check cleanly with exit 1 after reaching campaign, one disable diagnostic and
  no retry; the source anchor remains unchanged.
- Evidence: `work/native-audio-runtime-evidence.json`,
  `native-audio-actual-runtime.log`, `native-audio-unavailable-device-final.log`,
  `native-audio-final-validation.log`, `native-audio-python-validation.log`.
  Final validation includes finite-effect flush/drain, plus 12 exporter integrity
  tests (17 unrelated headless executable tests skipped without their opt-in).
  Windows CI now explicitly builds/runs both audio targets on SDL's dummy driver;
  it does not need a GPU or sound device. Hardware-backed runtime evidence is local.
  The relocated folder is a local validation fixture, not a sealed release.
  See `docs/engine/NATIVE_AUDIO.md` for limits and remaining work.
- Previous exact head `a56351f8` passed native CI `34960835811`; the audio
  checkpoint requires its own run. PR #332 remains unmerged.

## Remaining blockers / next work

- Diplomacy presentation gaps vs C#: no claims/border-warnings UI, no demand/trade
  proposal composer (terms list covers non-aggression/access/peace/ceasefire only),
  no grievance display.
- System and galaxy CPU imagery preparation is staged; early and developed
  24-ship campaigns now have measured baselines. Prioritize the missing native
  presentation/audio below. Revisit timing for new failures or substantially
  larger fleet/combat workloads, not repeated unchanged maps or speculative
  renderer settings. Keep Core access and GPU/window work on the owner thread.
- Surface colony visuals (buildings/roads), orbital structure rendering.
- Native audio settings/voice and full event wiring; the playback foundation,
  main score and navigation confirmation are now integrated on this candidate.
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
