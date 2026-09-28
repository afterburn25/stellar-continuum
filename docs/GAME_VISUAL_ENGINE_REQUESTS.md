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

## Delivered

(none yet)
