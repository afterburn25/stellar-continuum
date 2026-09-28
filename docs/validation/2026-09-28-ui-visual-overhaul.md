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
