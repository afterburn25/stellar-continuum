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

### REQUEST: Channel-matrix post-process for color-blind simulation
Status:        OPEN
Requested:    2026-09-28
WHY NEEDED:
  The color-blind accessibility modes (Protanopia/Deuteranopia/Tritanopia,
  Machado severity-1) remap every CPU-drawn surface — text, primitives,
  image and mesh tints — but every rendered 3D scene stays unremapped:
  planets, ships, eruption ribbons and the nebula emission volume keep
  their authored channel balance while the surrounding chrome adapts,
  so a low-vision player sees an inconsistent frame. The high-contrast
  sibling already adopted `RenderOptions3D::contrast`/`sharpen` for the
  same surfaces; color-blind simulation needs a 3x3 channel matrix the
  scalar post-process terms cannot express.
CURRENT GAME SCREEN:
  `app/native_client/native_ui_theme.hpp` `apply_color_blind` — the pass
  walks `DrawList` world+overlay and is the single integration point;
  it would set a new per-view matrix option on each `Scene3DView`.
DESIRED PUBLIC API:
  `RenderOptions3D::color_matrix` (or `colorblind_matrix`) — a 3x3
  post-tonemap channel-remap matrix applied in display space on the
  resolved LDR frame (identity default; document whether it composes
  before or after `contrast`/`saturation`/`sharpen`).
PERFORMANCE CONSTRAINT:
  One additional uniform + a 3x3 multiply per resolved fragment; no
  extra passes, no simulation or save impact.
FALLBACK IF NOT AVAILABLE:
  `apply_color_blind` keeps covering the 2D chrome only — the 3D scene
  gap is documented in the function's comment.

### REQUEST: Nullable `SurfaceEffect3D::next_texture` for single-texture effects
Status:        OPEN
Requested:    2026-09-28
WHY NEEDED:
  Any `surface_effect` — including the nebula emission volume, which only
  uses the volume fields — fails validation without a bound secondary
  texture (`Invalid surface effect sequence or occlusion sphere`), so the
  game binds the same composite twice as a no-op `blend=0` pair. The
  second bind is harmless but misleading: readers assume a two-image
  sequence where none exists.
CURRENT GAME SCREEN:
  `app/native_client/native_phenomena.cpp` — the local-nebula emission
  volume binds `surface_effect.next_texture = composite` purely to
  satisfy validation.
DESIRED PUBLIC API:
  Permit `next_texture == nullptr` whenever `blend <= 0`, keeping the
  sequence validation only for actual two-image blends.
PERFORMANCE CONSTRAINT:
  Validation-only change; no render-path cost.
FALLBACK IF NOT AVAILABLE:
  The double-bind workaround stays — it is cheap and validated, just
  obscure. Documented as a limitation in the ledger.

### REQUEST: Flared / non-coplanar annulus geometry for protoplanetary discs
Status:        OPEN
Requested:    2026-09-28
WHY NEEDED:
  Protostars render their protoplanetary debris as a flat `annulus_mesh`,
  but real young-star discs are flared (scale height grows with radius)
  and optically thick — a flat sheet cannot show the rim shadow lane or
  the warped silhouette that makes protostars read correctly. The same
  geometry would benefit the black-hole accretion discs, which are also
  coplanar today.
CURRENT GAME SCREEN:
  `app/native_client/native_system_workspace.cpp` — protostar debris
  disc and both black-hole flow regimes consume `annulus_mesh` with
  `accretion_disc_material3d`.
DESIRED PUBLIC API:
  A mesh primitive or mesh-loader spec for a flared disc (inner/outer
  radius, flare exponent, radial+azimuth segments), ideally double-sided
  so the far rim silhouettes through the inner gap; consumption is a
  straight `annulus_mesh` swap on existing instances.
PERFORMANCE CONSTRAINT:
  One extra vertex batch per star; no new shaders or simulation data.
FALLBACK IF NOT AVAILABLE:
  The flat annulus with deterministic per-system inclination stays —
  readable and physically motivated, just geometrically thin.

### REQUEST: Time-evolved accretion shear (differential spiral advance)
Status:        OPEN
Requested:    2026-09-28
WHY NEEDED:
  Accretion disc spiral density waves are a static bake; the game
  rotates the whole instance on `visual_seconds_`, which keeps the
  doppler lane view-fixed but spins the arm pattern rigidly — inner
  and outer edges orbit at the same angular rate, physically wrong for
  a Keplerian flow (inner edge should lap the outer several times over).
CURRENT GAME SCREEN:
  `app/native_client/native_system_workspace.cpp` — black-hole annulus
  (active `.32`, quiescent `.16` rad/s) and the protostar debris disc
  (`.07` rad/s) compose the spin into the tilt.
DESIRED PUBLIC API:
  A `band_drift`-style time term on the disc material that advects the
  spiral phase differentially with radius (e.g. `shear_rate` —
  rad/s at unit radius, evaluated as `phase + t·shear_rate/r^1.5`),
  preserving the authored bake as the t=0 shape.
PERFORMANCE CONSTRAINT:
  One extra multiply in the existing disc shader; zero CPU cost, no
  new textures; freezes on pause with `options.time` like every
  animated term.
FALLBACK IF NOT AVAILABLE:
  Rigid instance spin stays — it already animates the arms without
  disturbing the doppler lane; the shear would only add inner-edge
  differential motion.

## Delivered

(none yet)
