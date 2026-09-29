# UI/visual-overhaul integration verification — 2026-09-28

**Build succeeds; the full native test gate passes (327/327).** This is a
development-state handoff for the graphics-integration branch, not a release
certification. Current architecture: custom Stellar Engine, C++23 engine and
C++23 game.

## Scope and Git evidence

Repository `afterburn25/stellar-continuum`, branch `game/ui-visual-overhaul`,
PR #338 targeting `engine/space-strategy-simulation-specialization` (base tip
`e894747e`, branch contains it — no refresh needed). Verified tip:
`188fd1ae`. Engine source consumed through `engine/space-graphics-overhaul`,
tip `7c819ea7` — fully merged; all four historical visual work branches
(`work/cohesive-visual-polish`, `work/combat-system-visuals`,
`work/fun-visual-vertical-slice`, `work/galaxy-star-system-visuals`) report
zero commits ahead.

Scope adopted since `473d133a`: Scene3D in system/planetary/battle/fleet
surfaces; stellar photospheres (three-term limb law), accretion discs
(spiral waves, flow-regime differentiation, animated), relativistic jets,
protostar debris discs; phenomena IBL + the local-nebula emission volume;
small-body host-star lighting + screen-door LOD fade; quality propagation to
every `Scene3DView`; high-contrast/reduce-motion/reduce-flashing reaching 3D;
DebugView3D diagnostics + renderer-stats readout.

## Build and tools

Windows x64, VS 2022 Build Tools, MSVC 19.44, C++23, Ninja, tree
`build-native/devin`, `BUILD_TESTING=ON`.

```bat
cmake --build build-native\devin --parallel 8
ctest --test-dir build-native\devin -j8 --output-on-failure
```

## Results

- `ctest -j8`: 326/327 in the parallel pass; `campaign_phase_cadence_oracle`
  timed out under load and passed standalone in 78.4s — machine contention,
  not a regression (same signature as the 2026-09-24 receipt notes).
- Live smokes on the tip binary, all `save=ok` with FoW/canonical flags
  green: `--developer-smoke` (all sections incl. black hole, relativistic
  jets, `local_nebula=emission_volume_submitted_passed`), `--battle-smoke`/
  `--battle-reload-smoke`, `--planetary-smoke`/`-reload`, `--system-smoke`,
  `--system-travel-smoke`/`-reload` (canonical_moved/rendered_moved and
  paused-stable both verified), `--fleet-smoke`, `--ship-art-smoke`,
  `--galaxy-art-smoke`, `--eruption-smoke`, `--quick-find-smoke`,
  `--navigation-smoke`, `--inspection-check`, `--economy-check`,
  `--logistics-check`, `--general-settings-check`, `--video-settings-check`.
- Video settings chain live at all four quality tiers (Low/Medium/High/
  Ultra) and density extremes (0 and 2); `texture_uploads` tracked the tier
  ladder (302/305/308/299), frame pacing vsync-flat.
- Per-commit record and the adopted-vs-declined engine-API inventory:
  `docs/UI_UX_OVERHAUL_PLAN.md` (commit hashes backfilled through
  `24385dd2`).
- Packaged-validator audit and the 3D render-target budget gate are
  recorded in the follow-up section below.

## Follow-up: packaged validators and render-target budget gate

- `validate_native_diplomacy_export` caught up to shipped features:
  `chronicle` proof field, retained notification feed across save/load,
  strictly-additive save migrations (`EventHistory`,
  `Galaxy.StellarActivityDay`, `SmallBodyFields`, moon backfill rows,
  `PlanetAppearance`). Unit suite 38/38. `b130f624`.
- Dead validators removed — `--notification-smoke` and
  `--logistics-smoke` were dropped from the client parser in merge
  `0f6637a1`; their Python wrappers could never run. Coverage lives in
  the diplomacy smokes and `--logistics-check`. `d10bd992`, `dec0be89`.
- Live sweep of the remaining packaged validators green (client,
  economy, inspection, supply, colony, navigation, research, support,
  galaxy). `test_galaxy_asset_import` remains the sole tools failure —
  requires un-vendored `assets/source/galaxies-16x9/` source PNGs.
- `native_planetary_runtime` exposed a real player-facing overflow: at
  2560×1440 HDR (~59 MB per fullscreen target), the system dome + local
  nebula volume + workspace views + overlay globes (e.g. the colony
  globe) exceeded the renderer's 128 MiB
  `maximum_scene3d_target_bytes` cap — `--system-smoke` and
  `--planetary-reload-smoke` threw at drawable sizes ≥2560.
- Fix: the system backdrop (dome + local nebula volume) stages in its
  own `DrawList` and the 3D-vs-2D decision defers to just before
  `scene_content` returns, after every overlay workspace has emitted.
  Over budget, the dome re-emits as a flat authored `Image` (crop, roll,
  mirror, blend, exposure/tint preserved; tangent warp and aniso
  dropped) and the nebula uses its existing 2D composite path
  (`append_system(...,volumetric=false)`). The battle environment gates
  inline since it returns early. Under budget the full 3D path is
  unchanged.
- Verified: `--system-smoke` 2560×1440 `scene_render_target_bytes`
  88.1 MB < 128 MiB; `validate_native_planetary_export` green across
  1280×720 fresh and 1920×1080/2560×1440 reload legs, 9 captures;
  `stellar_system_background_tests` green including new flat-path
  checks for both the dome and the nebula volume.
- Note: `system_background_tests` exercises the fallback paths; the
  frame-level budget arithmetic in `scene_content` is covered live by
  the smokes above, not by a unit test (needs a real draw list).
- Running `--developer-smoke` at >=1920x1080 surfaced a second,
  unrelated defect: a completed-but-uncollected galaxy core-fog ticket
  held 4 MiB of queue reservation while off-map, and the sky plate's
  quality>=2 request reserves the entire default 32 MiB queue budget —
  `system_background_.ready()` could never settle. Fixed by a per-frame
  `NativeGalaxyBackdropAssets::poll()` drain (same contract as the sky's
  own poll) plus a 64 MiB queue cap so one full-size request coexists
  with in-flight composites. Dev smoke's nebula assertion is now
  budget-aware (accepts the flat composite only when a 3D backdrop could
  not fit). `native_galaxy_backdrop` gained a stranded-ticket test
  (10/10). `--developer-smoke` green at 1280/1920/2560.
- Follow-up (`bfbc64b3`): the same straggler hole was closed in every
  remaining queue consumer — `stellar_art` (whose `pending_count()`
  gates `artwork_ready()` unconditionally, so a stranded photosphere
  ticket would have wedged settling outright), `planet_discs`, and
  `small_body_assets` each gained the `poll()` convention, wired
  per-frame in `scene()`. `eruption_art` already drained via
  `begin_frame()`; `celestial_appearance` tickets cancel on workspace
  close. Regression tests pin `poll()`-only draining for planet-disc
  and stellar-art tickets.
- Verification closure: `--system-travel-smoke` and
  `--system-travel-reload-smoke` green at 2560×1440 on
  `work/de-travel-r.json`; `--military-check` green at 2560×1440 on
  `work/mil-fixture.json`; the platform chain (`--support-check`,
  `--audio-check`, `--audio-settings-check`, `--general-settings-check`,
  `--video-settings-check`) green at 2560×1440 and `--voice-check` at
  1920×1080. Smoke hardening: `military_smoke` refreshes the fleet view
  instead of dereferencing a disengaged optional and dumps row state on
  fixture mismatch (`19e0ffb7`); the settings checks open the pause
  menu themselves when the parent smoke left it closed (`85a28274`),
  making the travel+audio-settings+voice combination runnable.
- Fixture hazard recorded for future runs: stale `.bak`/`.integrity`
  sidecars next to `--save-path` make the loader silently recover the
  previous save — clean sidecars when swapping fixture paths.
- Final coverage sweep (tip `497e219e` binary): every native smoke and
  check flag in the client is now exercised green. Newly verified:
  `--fleet-smoke` and `--ship-art-smoke` on `work/de-ship.json`;
  `--galaxy-art-smoke` at 2560×1440; `--fresh-progression-smoke` and
  `--fresh-progression-reload-smoke` at seed 115501 (6568.5 simulated
  days, save roundtrip); `--navigation-smoke` (workspace switches,
  keyboard playback, canonical unchanged); `--settlement-smoke`,
  `--settlement-reload-smoke`, `--settlement-completion-smoke` and
  `--settlement-founded-smoke` (authorized→founded colony chain on a
  `tools/author_settlement_save.py` fixture grafted from
  `populated-vessel.player17.json`); `--settlement-preparation-smoke` on
  `work/en-settle-prep.json`; `--colony-smoke` on
  `work/founded.player17.json`; `--new-game-smoke` and
  `--restart-smoke` on fresh seed-115501 campaigns;
  `--diplomacy-reload-smoke` on `work/de-diplo-r6.json`;
  `--first-exploration-smoke`/`--first-exploration-paused-smoke`/
  `--first-exploration-resume-smoke` (depart→resume chained through the
  smoke autosave; `work/fed-run.json`, `work/expl-paused.json`);
  `--first-survey-smoke`/`--first-survey-paused-smoke`/
  `--first-survey-resume-smoke` (`work/survey-depart.json`,
  `work/svr-run.json`); `--research-smoke`, `--shipyard-smoke`,
  `--construction-smoke`, `--quick-find-smoke` on `work/de-ship.json`;
  `--inspection-check`, `--logistics-check`, `--economy-check` under
  `--smoke`; and `--campaign-profile --profile-frames 120`
  (16.9 ms steady-state frame mean at 1920×1080; 16.68 ms at
  2560×1440 — update 0.47 ms, scene 0.19 ms, effectively vsync-bound;
  a 3600-frame sustained run at 2560×1440 shows no drift: update
  0.42 ms / scene 0.20 ms steady, p99 frame 23.4 ms, 27 total image
  uploads — no per-frame re-upload churn or residency growth).
- Visual capture audit: marquee render paths confirmed by eye, not only
  by assertion — galaxy backdrop art (`nav.bmp`), Sol photosphere
  granulation at 2054× zoom (`gal-star-system-maximum.bmp`), Earth PBR
  globe with terminator, night-side city lights and atmosphere rim
  (`pl-planetary-1.bmp`), Jupiter amber storm bands and Saturn's full
  ring system (`work/fexp-{jupiter,saturn}-globe-front.bmp`), the
  supermassive black hole's lensed accretion disc with photon ring and
  doppler asymmetry on the galaxy map
  (`work/fexp-central-black-hole.bmp`), a solar prominence arcing off
  Sol's limb (`work/fexp-eruption-system.bmp`), and emissive ship
  engine flames in the battle view (`bt.bmp`). `--planetary-smoke`
  green at 2560×1440 (review read-only, modal isolation, slot
  reservation, timed).
- Flag audit closed: after enumerating every `--*-smoke`/`--*-check`/
  modifier flag in the option parser, the last unexercised entries were
  run green — `--restart-cancel-smoke` and `--restart-exit-smoke`
  (ReturnToCampaign/Exit paths), `--new-game-restart-smoke` (restart
  into seed 115502), `--eruption-smoke` under `--dev-game` (live
  map/system eruption continuity and campaign payload),
  `--support-failure-check` (a `support` blocker file beside the save
  forces the Failed path; both triggers fail cleanly, blocker and
  campaign untouched), `--smoke-galaxy-card`/`--smoke-system-count`
  (new-game automation honors both — generated a 1000-system card-3
  galaxy), and `--smoke-full-exploration` under `--developer-smoke`
  (250 systems fully surveyed, `unexplored:0`). `--battle-reload-smoke`
  green at 2560×1440 on `work/battle-run.json` (reload inspection:
  `canonical_unchanged`, `day_unchanged`, read-only order rail).
  Every flag-legal path in the client binary is now exercised green.
- Record/replay audit: `--record`/`--replay`/`--replay-info` exercised
  on `work/de-travel-r.json`. Found and fixed a real bug
  (`85a27563`): saves authored before `GenerationMetadata` carry the
  key as null, and the replay observer's unconditional
  `meta->erase("CreatedAtUtc")` threw `type_error.307`, aborting the
  smoke save under `--record`. Both erase sites now guard with
  `is_object()`. Post-fix: recording produces a journal, replay
  verifies 42/42 checkpoints with `diverged:false`, `--replay-info`
  inspects the file (seed, build id, ordered commands/checkpoints),
  `--replay-until 20` dumps the canonical tick-20 document to
  `replay-until-20.json`, and `--replay-exit` ends at the verdict with
  `replay_verified` printed. Negative path also verified: replaying
  `rec.rep` against a save whose fleet-0 `LocalTransitPositionX` was
  mutated fails fast with `Replay divergence ... at tick 42293
  (save:Galaxy)`, dumps the actual document to
  `replay-divergence-42293.json`, and leaf-diffs to
  `Galaxy.Fleets[0].LocalTransitPositionX` in the `.diff.txt` —
  the section-localizing contract works end-to-end.
- Suite re-run on the post-replay-fix binary (`85a27563`): parallel
  `-j8` pass produced 325/327 with `campaign_phase_cadence_oracle`
  (timeout) and `native_planetary_screen` (SEGFAULT under load); both
  pass serially. Effective 327/327 — same load-contention signature as
  earlier sweeps; no regression.
- The `native_planetary_screen` instability was root-caused
  (`d910cd6b`): the test held a `Text*` into `draw.overlay` across a
  `draw={}` reassignment, then read `chip->at` for the issue-chip click
  — a use-after-free that intermittently clicked a stale coordinate
  (~1/20 standalone failure rate: "Issue chip did not select the
  affected structure"). The screen's hit dispatch is sound; the defect
  was test-side. Fixed by capturing the point before the re-render;
  60/60 clean runs after the fix (P(flake surviving unseen) ~4.6% at
  the old rate).
- The `campaign_phase_cadence_oracle` timeout was budget starvation,
  not a defect: the oracle runs many seeded 250-system traces with
  per-step payload digests (~82 s standalone) against a 120 s timeout —
  ~1.5x headroom, exceeded under `-j8` scheduling contention. Timeout
  raised to 300 s in `CMakeLists.txt`. With both anomalies resolved,
  the first fully clean parallel pass on this binary: **327/327 under
  `ctest -j8`** (597.5 s wall), no serial fallback needed.
- Timeout-headroom audit (CTestCostData vs. registered TIMEOUT):
  tightest remaining margins are `campaign_phase_cadence_oracle` ~3.6x
  (post-raise) and `galaxy_configuration` ~6.9x; all other tests sit
  above ~7x. The historical `developer_qa_host`/`native_research_
  controller` timeouts were extreme-contention phenomena (2.6 s and
  0.6 s standalone vs. 120 s/90 s budgets); neither reproduced in the
  clean `-j8` pass or standalone reruns.

- Flared annulus delivered (fourth renderer request): `flared_annulus_mesh`
  in `native_geometry3d.hpp` + `flared_annulus` loader spec; protostar
  debris and both BH disc regimes swapped in `native_system_workspace.cpp`.
  Geometry tests (mirrored sheets, radial height progression, winding,
  malformed-input rejection), loader-spec resolution in
  `engine_world_tests`, and the `accretion-flare` GPU silhouette check
  (flared rim extends past the flat disc's, inner gap stays open) all
  green; full suite re-run recorded in the ledger.
- Render-scale delivered (fifth and final renderer request):
  `Scene3DView::render_scale` ([0.25,1], validated in `prepare()`)
  allocates the view target at `ceil(destination*scale)` while
  `composite()` upscales the full destination rect with the existing
  linear filtering. Shared accounting:
  `scene3d_view_target_extent`/`scene3d_view_target_bytes` +
  `Window::scene3d_target_bytes` use the device's real bytes-per-pixel;
  `Scene3DStatistics::renderer_active` fixes the pre-first-3D-draw bpp
  estimate that under-counted at 8 B/px (root cause of a 2560×1440
  dev-smoke budget throw). Game gates scale backdrops before the 2D
  fallback — `fit_backdrop_within_budget` iterates past ceil-rounding
  overshoots and attaches a floored backdrop whenever the frame could
  fit at .25, reserving the flat path for frames whose
  `scene3d_min_target_bytes` floor still exceeds the cap; `scene()` runs
  a final multiplicative clamp so late developer-panel views cannot push
  a frame over the cap. Verified:
  extent/bytes unit tests, half-scale GPU allocation + estimator parity
  + destination-clip assertions, out-of-range/NaN/inf rejection;
  `--developer-smoke`, `--system-smoke`, `--battle-smoke` at 2560×1440
  and `--developer-smoke` at 1920×1080 all green with no budget
  exception — `local_nebula=emission_volume_submitted_passed` at 2560
  (the scaled volume stays raymarched; the flat-layer smoke assertion
  now accepts a composite only when quarter-scale could not fit);
  suite re-run recorded in the ledger.
- Quality-settings follow-up: STARFIELD QUALITY now reaches the 3D
  renderers — `NativeCampaign` pushed `RenderQuality3D` while
  `video_settings_` was still null and no Apply/Revert path re-pushed
  it; `sync_scene3d_quality` re-pushes all six consumers whenever the
  live setting drifts, and the constructor seeds it from the saved
  tier. `Window::set_scene3d_texture_budget` is now a live consumer
  (Low 48 / Medium 96 / High+ 192 MiB streamer budget) and persists
  through the lazily created renderer instead of silently dropping
  pre-first-3D calls. `Scene3DStatistics::texture_budget_bytes` reports
  the live budget so the memory overlay attributes texture residency
  against the active tier rather than the hard 192 MiB cap. Verified:
  `native_scene3d_gpu` pre-renderer budget test + live-budget stat,
  `--video-settings-check` (Apply/preview/Revert/Keep),
  dev smoke re-run in the ledger.
- Settings-smoke extension: `check_video_settings` now cycles every
  non-display choice row off its saved value — V-Sync, Frame Cap,
  EDGE SMOOTHING + SCENE RESOLUTION (exercising the `set_scene_quality`
  scene-target reallocation, previously applied only with unchanged
  values), STARFIELD QUALITY and STARFIELD DENSITY — and restores them
  through the same Apply/preview/Escape/Keep path. Green at both
  harness locations at 2560×1440: `startup` — the tiered budget lands
  on `Window::Storage` before the lazily created renderer exists — and
  `pause`, where the capture lambda's `campaign.scene()` call runs
  `sync_scene3d_quality` against a live campaign. Persisted
  `video-settings.json` verified restored after each run.
- Voice-settings check: `--voice-settings-check` +
  `check_voice_settings` close the last settings-panel gap — the hub's
  Voice category (index 3) previously had no smoke coverage. The check
  previews every control (all five toggles, all three sliders, both
  dropdowns), exercises Replay/Stop/Defaults, verifies Cancel restores
  the saved values byte-identically, saves and reloads through a fresh
  `NativeVoiceSettings`, then restores the original preferences (slider
  fractions assert the .01 tolerance used by the audio check — a
  fraction of exactly 0/1 lands on the track's half-open bounds and is
  clamped). Green at both harness locations at 2560×1440 and 1920×1080
  (`pause` under `--load --smoke`, `startup` under `--new-game-smoke`).
- Controls-settings check: `--controls-settings-check` +
  `check_controls_settings` cover the hub's in-view rebind list — the
  last settings surface without end-to-end coverage. The pause location
  drives the live list (the mapper binds to the hub once the campaign
  exists); under `--new-game-smoke` the same flag runs
  `check_controls_help_card`, which opens the mapperless Controls
  category, asserts the static help card exposes zero row hitboxes,
  renders it, and backs out to the category list.
  The check snapshots every binding in both contexts, arms capture on a
  row, verifies Escape cancels without changing bindings, installs an
  unbound probe key as the primary binding, presses a sibling's bound
  key to exercise the steal (victim binding verified removed, and the
  "reassigned from" notice is peeked in the a11y announcer — `update`
  drains `take_notice()` into pending Status announcements that the
  check observes through `NativeCampaign::announcer()`),
  then restores every snapshot through `rebind()` + persist and proves
  the file round-trips through a fresh `InputMapper`. The first live
  run caught a real defect: the controls view's render loop indexed
  `control_row_rects` past its clipped size whenever the action list
  exceeded the visible page (UB — a non-finite row rect tripped the
  text-bounds guard); the loop now iterates the clipped page.
  `NativeSettingsHub::control_row_bounds` exposes row hitboxes for the
  harness. Green under `--load --smoke` at 640×360, 1280×720, and
  2560×1440 on both fresh-file and pre-existing `galaxy-controls.json`
  paths — and under `locale:de` (full rebind/steal/scroll/axis/pin/
  notice/trigger matrix green with German chrome; the notice assertions
  are locale-agnostic by design — presence and difference, not
  literals).
- General-settings check startup leg: `--general-settings-check`
  validated under `--new-game-smoke` but only the pause block ran it —
  a silently no-op invocation. The startup automation now opens the
  hub's General category, captures the overlay, and Cancel-closes it
  (`general_settings_screenshot` field; emit gains `"location"`). Every
  hub category now has coverage at both harness locations.
  Follow-up: the page clip also made the three `GALAXY_PAD` axis rows
  unreachable — the shipped map's 15 rows exceed the ~12-row page at
  every viewport, and no scroll path existed. The list now scrolls on
  the shared `VirtualizedList` (`controls_scroll_`): wheel ticks page
  the rows (intersect-clipped slivers keep partially visible rows
  clickable), Tab/arrow/Home/End focus-follows via `ensure_visible`,
  and capture is untouched (wheel stays an axis-capture trigger while
  armed). Offsets are intentionally unsnapped — row-snapping the
  scroll offset would strand the tail row under the fractional
  viewport remainder. Scroll resets on open/close and category entry.
  The smoke's scroll leg verifies the tail row is hitbox-free until
  paged in — and, since the tail rows are the pad-axis rows, it feeds a
  synthetic `GamepadAxis` deflection (unbound axis code, 0.9 > dead
  zone) through `campaign.update`, asserting the live capture lands a
  `GamepadAxis` binding, plus a cross-row axis steal. The focused row
  then exercises the D-key pad-pin cycle live (any → pad 1 → pad 2 →
  wrap to any, asserted through `InputBinding::device`). The notices
  are peeked through `NativeCampaign::announcer()` — `update` drains
  `take_notice()` into `AccessibilityAnnouncer` as Status items that
  stay pending until `scene()`'s caption pass, so the check asserts a
  non-empty announcement follows the steal and a *different* one
  follows each pin step (content stays locale-agnostic). The remaining
  trigger kinds are covered too: right-click lands MouseButton:3, a
  `GamepadPressed` event lands GamepadButton, left-click disarms
  without touching bindings, a wheel scroll captures on an armed axis
  row, and a discrete keypress on an axis row is swallowed while
  capture stays armed. That sweep caught a real defect: an armed
  capture survived `PointerCancelled` (window focus loss), so the next
  keypress after refocus would be eaten by a stale capture — pointer
  loss now disarms, matching every other surface's cancel-pending
  convention (pinned in `native_startup_workspace` and live-verified).
  Pad-axis capture, device pinning, the a11y notice path, and every
  capture/cancel trigger kind are now exercised end-to-end, not just
  unit-tested.
- `VirtualizedList::sync_rows` tail-pin fix: the per-frame
  floor-to-row-edge snap made every row-snapped consumer strand up to
  one row-height of its final entry whenever `max_scroll` had a
  fractional remainder (the `1e-3` tolerance only rescued
  boundary-adjacent remainders). An offset pinned at `max_scroll` now
  stays unsnapped — mid-list offsets still snap — so the diagnostics
  panels' absolute `i*stride - offset` rows can fully reach the tail.
  Fixed-slot consumers (celestial index, empire monitor, save-slot
  list) are unaffected: their `first` index is floored either way.
  `batcher_ui` pins both cases (general remainder keeps `max_scroll`
  and exposes row 15 fully; mid-list offsets still snap), and the
  header contract documents the behavior.

## Known limitations

- All five renderer requests are delivered
  (`GAME_VISUAL_ENGINE_REQUESTS.md` — nothing open on the renderer
  lane).
  Delivered: nullable `SurfaceEffect3D::next_texture` (validation
  requires it only when `blend > 0`; the nebula volume's double-bind is
  removed; `native_scene3d_tests` covers both branches), the
  color-blind channel matrix (`RenderOptions3D::color_matrix` — a
  column-major 3×3 post-tonemap remap gated on non-identity finite
  values, `PostUniform` a..e/80 B, applied in `tonemap.frag`;
  `apply_color_blind` composes its identical linear map onto every
  `Scene3DView`; `post_gpu` channel-swap + settings-view assertions
  green; `--system-smoke` under `colorBlind:2` remapped 82% of lit
  3D-region pixels vs baseline), and Keplerian accretion shear
  (`Material3D::shear_rate`/`shear_ratio` scroll the azimuthal V at
  `rho^(-3/2)`; `fract()` keeps the V-periodic bake seamless under
  clamped sampling; `accretion-shear` GPU test asserts inner-band
  motion exceeds outer; zero-rate is bit-identical over scene time),
  the flared annulus primitive (`flared_annulus_mesh` mirrored curved
  sheets + `flared_annulus` loader spec; protostar debris and BH
  discs consume it; geometry/loader/GPU-silhouette tests green), and
  Scene3DView render-scale (`render_scale` [0.25,1] shrinks the
  offscreen target, `composite()` upscales linearly; shared
  extent/bytes accounting incl. the real HDR bpp; backdrop gates
  scale before the 2D fallback and `scene()` applies a final
  multiplicative clamp).
  Three core-lane
  projections remain open: fleet composition, interstellar logistics
  route graph, per-action diplomacy blockers.
- `irregular_rock_mesh`/`card:` spec docs are uncommitted in the
  `sc-integration-merge` worktree; `billboard_card` is committed
  engine-side but intentionally unused game-side (CPU projected-size
  culling owns the distant-body contract).
- `Window::set_scene3d_texture_budget` is live: the video-settings apply
  callback retunes the streamer budget per STARFIELD QUALITY tier
  (Low 48 / Medium 96 / High+ 192 MiB) and a pre-renderer request persists
  on `Window::Storage` until the renderer is lazily created
  (`371efc07`); `Scene3DStatistics::texture_budget_bytes` reports the
  effective live budget for memory attribution (`a839c4fc`).
- `--diplomacy-smoke` authors its proposal fixture per run inside the
  validator (green above). `test_galaxy_asset_import` still needs the
  un-vendored `assets/source/galaxies-16x9/` PNGs (pre-existing gap).
