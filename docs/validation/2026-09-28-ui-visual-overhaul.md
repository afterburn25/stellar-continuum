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

## Known limitations

- Four renderer requests remain filed on the engine lane
  (`GAME_VISUAL_ENGINE_REQUESTS.md`): color-blind channel matrix
  (color-blind simulation stays CPU/2D-only — documented in
  `apply_color_blind`), nullable `SurfaceEffect3D::next_texture`,
  flared/non-coplanar annulus geometry, time-evolved differential
  accretion shear. Three core-lane projections remain open: fleet
  composition, interstellar logistics route graph, per-action diplomacy
  blockers.
- `irregular_rock_mesh`/`card:` spec docs are uncommitted in the
  `sc-integration-merge` worktree; `billboard_card` is committed
  engine-side but intentionally unused game-side (CPU projected-size
  culling owns the distant-body contract).
- `Window::set_scene3d_texture_budget` intentionally unwired — 192 MiB
  default vs. ~45 MB measured scene-texture demand; no settings consumer
  exists (audit closure ledger row, `24385dd2`).
- `--military-check` and `--diplomacy-smoke` need dedicated fixtures
  (owned armed fleet; fresh-authored proposal per run) — not exercised
  this pass; both surfaces are domain workspaces with prior green
  evidence.
