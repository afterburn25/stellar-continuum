# Game Visual Engine Requests

Requests from the **game/UI workstream** (`game/ui-visual-overhaul`) for reusable
renderer or engine capabilities that the game-facing client needs but must not
build itself. The concurrent graphics session (`engine/space-graphics-overhaul`)
owns `engine/`, `engine/shaders/`, generic rendering, and engine visual APIs.

Rules:

- Record a request here instead of building a parallel renderer or duplicating
  engine architecture in `app/native_client/`.
- Keep working on other UI/UX items while a request is pending; when a capability
  lands in `engine/space-strategy-simulation-specialization`, merge the shared
  branch into `game/ui-visual-overhaul` and consume it deliberately through the
  public engine interface.
- Do not request game-specific behavior in engine APIs; describe the reusable
  capability and the game's consumption point.
- When a request is fulfilled, move the entry to **Delivered** with the landing
  commit/PR and the consuming change.

## Entry template

```
### REQUEST: <short name>
Status:        OPEN | IN-PROGRESS (engine lane) | DELIVERED | DECLINED
Requested:    <date>
WHY NEEDED:
  <the player-facing problem or visual quality gap>
CURRENT GAME SCREEN:
  <module/surface that would consume it, e.g. app/native_client/native_scene3d.cpp galaxy view>
DESIRED PUBLIC API:
  <the engine-facing capability, e.g. "Scene3D: per-mesh atmosphere toggle" —
  describe behavior, not implementation>
PERFORMANCE CONSTRAINT:
  <frame budget / memory bound / determinism or save impact>
FALLBACK IF NOT AVAILABLE:
  <what the game will do meanwhile>
```

## Open requests

### REQUEST: Per-vessel fleet composition projection
Status:        OPEN
Requested:    2026-09-25
WHY NEEDED:
  The fleet workspace and controlled-assets navigator can only describe a
  fleet as one vessel (design name, embarked population, hull integrity).
  Players cannot see per-ship breakdowns inside battle groups, so fleet UI
  cannot answer "what is in this fleet" beyond the lead vessel.
CURRENT GAME SCREEN:
  app/native_client/native_fleet_workspace.cpp detail card;
  app/native_client/native_controlled_assets.cpp fleet rows.
DESIRED PUBLIC API:
  Core projection: `FleetComposition` — a per-fleet list of member vessels
  (design id/name, hull integrity, embarked population/cargo) exposed
  through the existing fleet view-model, FoW/observer-sealed.
PERFORMANCE CONSTRAINT:
  Deterministic; save-load compatible; constant per-fleet size.
FALLBACK IF NOT AVAILABLE:
  Detail card keeps showing lead-vessel design + embarked totals only.

### REQUEST: Interstellar logistics route graph
Status:        OPEN
Requested:    2026-09-25
WHY NEEDED:
  `CivilizationLogisticsCoverage` reports that a corridor to an external
  system exists, plus demand/import/gap scalars — but exposes no route
  edges. The supply workspace can list coverage rows yet cannot draw the
  actual interstellar links or explain corridor quality per hop.
CURRENT GAME SCREEN:
  app/native_client/native_logistics_workspace.cpp INTERSTELLAR COVERAGE
  section (per-system rows without link geometry).
DESIRED PUBLIC API:
  Extend `CivilizationLogisticsCoverage` (or a sibling projection) with
  sealed external `LinkRow`-style edges: endpoint systems, capacity,
  transit days, enabled/bidirectional — mirroring the home-network
  `HomeSystemLogisticsNetwork` link shape the workspace already renders.
PERFORMANCE CONSTRAINT:
  Deterministic; sealed/observer-validated like existing link rows.
FALLBACK IF NOT AVAILABLE:
  Coverage section renders condition/capacity/demand scalars per system
  with the unrepresented-demand callout (shipped in 87b4a468).

### REQUEST: Per-action diplomacy blocker reasons
Status:        OPEN
Requested:    2026-09-25
WHY NEEDED:
  `ObserverDiplomacyActionAvailability` reports whether an action is
  allowed but not *which* prerequisite blocks it. The workspace surfaces
  domain status summaries (communication/political status) as the hover
  "why" — accurate but not precise; a player can't distinguish "no
  transmission channel" from "proposal cooldown" without reading prose.
CURRENT GAME SCREEN:
  app/native_client/native_diplomacy_workspace.cpp disabled action slots
  and negotiation terms (shipped in e015e209 / f46fd46c).
DESIRED PUBLIC API:
  Per-action blocker string or enum on the availability projection (e.g.
  `blocker: none|no_channel|cooldown|war_state|...`), observer-safe,
  localized at presentation.
PERFORMANCE CONSTRAINT:
  Constant-size per action; no new simulation queries at render time.
FALLBACK IF NOT AVAILABLE:
  Disabled slots keep surfacing the authoritative domain status summary.

### REQUEST: Render-scale for Scene3DView targets under budget pressure
Status:        OPEN
Requested:    2026-09-28
WHY NEEDED:
  The renderer enforces a 128 MiB aggregate cap on 3D render-target
  memory (`maximum_scene3d_target_bytes`, w*h*16 B per HDR view). The
  game now gates whole layers on that budget: past ~2560x1440 the
  fullscreen celestial dome and the local-nebula emission volume drop
  to their authored 2D paths so content views (planets, globes, hulls)
  keep their targets. That is an all-or-nothing trade — at cap pressure
  the sky loses the tangent warp and the cloud loses raymarching even
  though a half-res 3D target would still look visibly better than the
  flat composite.
CURRENT GAME SCREEN:
  `app/native_client/main.cpp` — `scene_content` stages the system
  backdrop and picks 3D-vs-2D once the frame's total target bytes are
  known; the battle environment gates identically at its early return.
  `native_system_background.hpp` (flat `Image` path) and
  `native_phenomena.cpp` (`volumetric=false`) carry the 2D sides.
DESIRED PUBLIC API:
  A `render_scale` (0.25..1) field on `Scene3DView` that allocates the
  target at `destination * scale` and upscales on composite — cheaper
  targets that keep the volumetric/warped shading. Bonus: a
  `scene3d_target_bytes(const DrawList&)`-equivalent accessor so the
  game mirrors the engine's real accounting (HDR flag included)
  instead of assuming 16 B/px worst-case.
PERFORMANCE CONSTRAINT:
  Strictly reduces target memory; upscale is one blit pass the
  compositor already performs per view.
FALLBACK IF NOT AVAILABLE:
  The shipped whole-layer gate stays — visuals degrade stepwise but
  never crash.

## Delivered

### REQUEST: Nullable `SurfaceEffect3D::next_texture` for single-texture effects
Status:        DELIVERED
Requested:    2026-09-28
Delivered:    2026-09-29 on `game/ui-visual-overhaul` — validation now
requires `next_texture` only when `blend > 0` (`native_scene3d.cpp`);
the header documents the nullable contract. The local-nebula emission
volume (`native_phenomena.cpp`) no longer double-binds its composite.
All downstream consumers were already null-safe (`texture()`/`mip_for`/
`stream_request` fall back to the white placeholder;
`texture_mip_layout3d` accounts a null as 1×1); the shader still samples
the sequence slot but `mix(texel, next, 0)` discards it. Tests:
`native_scene3d_tests` rejects null+blend>0 and accepts null+blend=0.
Verified: `stellar_scene3d_tests`, `stellar_scene3d_gpu_tests`, and
`--developer-smoke` seed 1701 at 1920×1080 all green with
`local_nebula=emission_volume_submitted_passed`.

### REQUEST: Channel-matrix post-process for color-blind simulation
Status:        DELIVERED
Requested:    2026-09-28
Delivered:    2026-09-28 on `game/ui-visual-overhaul` —
`RenderOptions3D::color_matrix` is a column-major 3×3 post-tonemap
channel remap applied in display space after contrast/saturation/
sharpen and before vignette (`tonemap.frag`); identity or any
non-finite element disables the multiply so the default path stays
bit-identical. `PostUniform` grew to `a,b,c,d,e` (80 B) with the
uniform-block `static_assert` updated and `post.b.w` carrying the
enable flag. `apply_color_blind` (`native_ui_theme.hpp`) composes the
identical linear map it applies to CPU colors — Machado simulation +
error redistribution — onto every `Scene3DView` in `world` and
`overlay`, so GPU-rendered 3D content receives the same daltonization
as the 2D chrome. Tests: `native_scene3d_gpu_tests`
`post_gpu=…color_matrix_passed` verifies a channel swap through the
real shader; `native_general_settings_tests` asserts the pass sets a
non-identity matrix on a view and that `None` leaves it identity.
End-to-end: `--system-smoke` under `colorBlind:2` remapped 82% of lit
3D-region pixels vs the `colorBlind:0` baseline capture.

### REQUEST: Time-evolved accretion shear (differential spiral advance)
Status:        DELIVERED
Requested:    2026-09-28
Delivered:    2026-09-28 on `game/ui-visual-overhaul` —
`Material3D::shear_rate` (rad/s at the inner edge, [-8,8]) +
`shear_ratio` (outer/inner, [1,1024]) scroll the azimuthal V
coordinate by `rate·t·rho^(-3/2)` in `scene3d.frag`, packed through
`drift_options.y` (turns/s at inner edge) and `.z` (ratio). The disc
bake is V-periodic so `fract()` keeps the wrap seamless under the
clamped surface sampler; zero rate is bit-identical.
`accretion_disc_material3d` takes a trailing `shear_rate` parameter
and fills `shear_ratio` from its radii. Consumers:
`native_system_workspace.cpp` — black-hole annulus `.8` active /
`.4` quiescent, protostar debris `.12` (sub-Keplerian crawl), each
layered on the existing rigid spins. Tests: `native_scene3d_tests`
validation (rate/ratio bounds + NaN + round-trip);
`native_scene3d_gpu_tests` — `accretion-shear-t0/t4` diverge with the
inner-rim band changing more than the outer, and a zero-shear disc is
bit-identical at t=0 vs t=4 (`…_shear_passed`).

### REQUEST: Flared / non-coplanar annulus geometry for protoplanetary discs
Status:        DELIVERED
Requested:    2026-09-28
Delivered:    2026-09-29 on `game/ui-visual-overhaul` —
`flared_annulus_mesh(inner, outer, flare, exponent=2,
azimuthal=192, radial=8)` in `native_geometry3d.hpp` builds two
mirrored curved sheets y = ±flare·(r/outer)^exponent — the trumpet
silhouette — with radial-U/azimuthal-V coordinates identical to
`annulus_mesh` so `accretion_disc_material3d`, the spiral bake, and
the shear scroll transfer verbatim. Mirrored winding keeps the lower
sheet front-facing (double-sided silhouette through the inner gap);
normals follow the true surface slope for correct rim lighting.
Loader spec `flared_annulus:i,o,flare[,exponent[,azimuthal[,radial]]]`
in `mesh3d_loader.cpp`. Consumers: `native_system_workspace.cpp` —
protostar debris (flare .16, exponent 2 — puffy rim) and both
black-hole flow regimes (flare .07, exponent 1.6 — subtle slim-disc
curve) swapped from `annulus_mesh`; one mesh instance per star, no
new shaders or simulation data. Tests: `native_scene3d_tests` geometry invariants +
malformed-input rejection; `engine_world_tests` loader spec
resolution; `native_scene3d_gpu_tests` `accretion-flare` asserts the
flared rim silhouette extends beyond the flat disc's while the inner
gap stays open (`…_shear_flare_passed`).
