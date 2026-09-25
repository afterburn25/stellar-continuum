# UI/UX Overhaul Plan — Stellar Continuum

Workstream: `game/ui-visual-overhaul` → PR target `engine/space-strategy-simulation-specialization`.
Scope: `app/native_client/` game-facing UI only. Engine/renderer capability gaps go to
`docs/GAME_VISUAL_ENGINE_REQUESTS.md` — do not build a parallel renderer.

Authority: Core owns simulation state and observer privacy. Every screen presents real,
observer-safe data — never fake shortages, numbers, availability, or diplomacy. Preserve
save compatibility, determinism, localization (`LocalizationTable`, en+de catalogs),
accessibility (focus rings, UIA patterns, pad nav), and performance bounds.

## Severity rubric

| Rank | Definition |
|---|---|
| CRITICAL | Blocks task completion, hides required information, breaks navigation/keyboard/pad/AT flow, or presents wrong/misleading state |
| HIGH | Materially slows a common task; persistent clutter, unreadable hierarchy, or buried actions on a primary screen |
| MEDIUM | Consistency/polish gap; repeated info; missing filter/sort/tooltip where peers have it |
| LOW | Cosmetic refinement; spacing/typography polish |

## Audit checklist (apply to every screen)

Hierarchy and typography · discoverability of actions · navigation (where am I /
what's selected / how do I go back) · information density and empty space · clutter at
each zoom/scale · search, filter, sort availability · tooltips (what/why-disabled/what-
happens) · status feedback and empty/error/loading states · contextual navigation to
related objects · keyboard/pad ring order and activation · UIA focus metadata
(label/bounds/control/state) · localization coverage · resize behavior · performance
(list virtualization, redraw churn)

## Screen inventory

Player-facing surfaces (audit each; developer-tool surfaces are English-only by
convention and lower priority — classify during the audit):

| # | Screen / surface | Status | Severity found | Notes |
|---|---|---|---|---|
| 1 | Startup / main menu / new game / galaxy selection | ☑ | LOW | `native_startup_workspace`, `native_new_game_workspace`, `native_startup_session`, `native_startup_entry`. Own palette + helpers; loading tips localized; seeded-art cover. Consistent enough to defer. |
| 2 | Galaxy map (all zoom bands) | ☑ | HIGH | Zoom-gated labels (>2× relative zoom), collision-aware label layout with HUD obstacles, compact markers >10k systems, territory labels at overview blend, fleet route effects + preview, phenomena inspect — all already present. Gaps: **no map legend** (star-class palette, fleet marker, lane, territory color, route-preview green/amber are unexplained), no zoom-band indicator, no strategic overlay toggles. Highest-priority visual screen. |
| 3 | System view | ☑ | MEDIUM | `native_system_workspace` + `native_system_view`: bodies, orbits, small bodies, lanes with metrics, travel snapshot, shipyard marker, settlement prep, body inspection panel, small-body AT ring. Rich; mainly needs visual polish review via captures. |
| 4 | Planet view / colony command center | ☑ | HIGH | `NativePlanetaryScreen` inside `native_colony_workspace`: globe + layers, facts column, alert strip, 5 action buttons, 4 tabs, slots grid, per-site management, queue, confirmation modals, full hit-registry AT ring. Gaps: no at-a-glance vitals (population/stability/power/food/employment scattered across a scrollable fact list + two tabs); alert strip is bare text tokens ("POWER DEFICIT.") with no magnitudes or per-facility impact; economy tab is dense wrapped text. Phase 8 "major problems obvious" unmet. |
| 5 | Economy workspace | ☑ | HIGH | `native_economy_workspace`: flat scrollable row list — 2×3 card tiles, treasury health, 3 industry-priority buttons, income/cost rows, guidance. Own palette/helpers; section headings render as plain rows; no grouping chrome, no "what changed/why/where" affordance, no shortage/stockpile breakdown beyond rows the view emits. |
| 6 | Logistics workspace | ☑ | HIGH | `native_logistics_workspace` (`SupplyWorkspace`): home-system scope only — 4 metric tiles (available/demand/delivered/shortfall) + per-node table (location/status/offered/demand/delivered). `corridor_count` is a subtitle number; no route list, no origin→destination, no congestion/transit, no navigation to affected colony/system. Needs a view-model extension before richer presentation is possible (authoritative scope is `HomeSystemLogisticsNetwork`). |
| 7 | Research workspace (adaptive research) | ☑ | MEDIUM | `native_research_workspace`: 6 modes (Guided/Tree/Recent/Favorites/Completed/Queue), search, filter, sort, inspector, bookmarks, dropdowns, why-explanations, per-mode AT focus. Deep; Phase 11 largely satisfied by Guided mode. Audit residual: card density/readability at small scales. |
| 8 | Diplomacy | ☑ | MEDIUM | `native_diplomacy_workspace`: contact list + stage + meters + action row + 5 tabs (agreements/proposals/history/intelligence/overview), negotiation modal, FocusSystem navigation, contact filter. Strong; verify discoverability of the contact-filter control and tab labels via captures. |
| 9 | Fleets / battle groups | ☑ | HIGH | `native_fleet_workspace` outliner + detail + orders (hold/defend/retreat), locate, engage, route preview, recovery confirm, ship-art rows, empire-overview detail when nothing selected. No battle-group layer in `NativeOwnFleet` — fleets are flat rows; per-fleet ships/strength breakdown not surfaced. Flat list will not scale to many fleets; no grouping by role/location. |
| 10 | Battle presentation | ☑ | MEDIUM | `native_battle_workspace` + `native_battle_art`/`native_battle_sprites`: tactical field, ship targets, environment backdrop. Needs visual capture review for hierarchy (forces summary vs field). |
| 11 | Shipyard / construction queues | ☑ | MEDIUM | `native_shipyard_workspace`, `native_construction_workspace` + controllers: queues exist with progress; verify blocked-reason surfacing and unavailable-item treatment in captures. |
| 12 | Missions | ☑ | LOW | `native_missions` (`NativeMissionView`): missions/sites tabs, cards, fleet pagers, land/collect, FocusFleet/OpenColony routing, focus ring over actionable controls only. Newly live — polish pass, keep scroll/focus contract. |
| 13 | Notifications | ☑ | MEDIUM | `native_notifications`: bounded feed (32), category/date/message, VIEW SYSTEM + diplomatic-contact actions, unread badge, chronicle hand-off, scroll. Gaps: no severity iconography or grouping; transient feed vs retained feed is subtle. |
| 14 | Chronicle | ☑ | LOW | `native_chronicle`: observer-safe `HistoryQuery` projection, domain filter, significance floor, actor filter, time-window paging, tag chips, header search, system/DIP navigation. Reference-quality surface. |
| 15 | Controlled assets navigator | ☑ | LOW | `native_controlled_assets`: TreeModel categories, search, persisted collapse prefs, manage actions, tooltips, full UIA incl. `focused_expanded`. Reference surface — reuse its patterns. |
| 16 | Settings (hub, general, audio, video, voice, controls) | ☑ | LOW | `native_settings_hub` + per-domain settings: controls view has full rebind UI (capture/conflict-steal/chords/persist). Audit residual: visual consistency with workspace chrome. |
| 17 | Pause menu / session chrome | ☑ | LOW | `NativeUiLayout` menu panel + brand/nav bar + context plate (`native_command_hud`). Consistent; context plate is a strong "where am I" anchor. |
| 18 | Developer surfaces (diagnostics, indices, giant panel) | ☑ | LOW | Dev-only by convention; English-only allowed. No player-facing requirements. |

## Design system inventory (Phase 2)

Existing shared chrome to extend — not replace: `native_menu_style` (panel, button,
text, ink/muted/cyan palette), `native_ui` DrawList passes (high-contrast, color-blind
daltonization), `NativeUiLayout::for_viewport` (interface-scale multiplier),
`VirtualizedList`/`TableModel`/`TreeModel`/`ScrollView`. Inventory what each workspace
draws through these vs. ad-hoc styling; the design language lands as extensions to the
shared helpers so every surface follows.

Component checklist: panels (primary/secondary) · cards · headers/section titles ·
tabs · action/icon buttons · toggles · inputs/search · list/table rows · selected /
hovered / disabled / warning / critical / positive states · tooltips · dialogs /
confirmations · status badges · progress meters.

## Audit findings (verified against source — 2026-09-24)

Hypotheses verified/refuted during the audit:

- **Confirmed:** no global quick-find exists; search is per-surface only
  (chronicle, colony roster, controlled assets, research, shipyard, diagnostics,
  celestial index, seed). Phase 5's shared overlay must respect knowledge/FoW and
  reuse the existing focused_value Edit contract.
- **Confirmed:** contextual navigation exists in places (notification feed VIEW
  SYSTEM / contact actions, controlled-assets manage actions, missions
  FocusFleet/OpenColony, chronicle system/DIP entries) but is inconsistent —
  supply rows, fleet route failures and economy notices know their targets but
  do not link them.
- **Refuted:** galaxy map labels *do* cull by zoom — `relative_zoom > 2` gates
  system names, compact markers engage above 10k systems below zoom 16, and
  `layout_native_galaxy_labels` resolves collisions against star + HUD
  obstacles with priority ordering. The real gap is a **legend** and
  zoom-band signalling, not label spam.
- **Confirmed:** tooltip coverage is uneven (assets rows and HUD buttons carry
  tooltips; most workspace controls do not explain themselves or their
  disabled state).
- **Confirmed:** per-workspace style drift is the dominant consistency issue —
  `native_ui_theme` (palette + panel/section_header/button/progress/tooltip)
  is nearly unused; every workspace redefines `ink`/`muted`/`accent` with
  slightly different values and re-implements `fill`/`label`/`scrollbar`/
  focus-ring drawing (the focus color `{160,210,255,255}` is hand-duplicated
  in ≥5 surfaces).
- **Confirmed:** several surfaces predate or partially implement the
  `focused_expanded`/`focused_value`/`focused_range` AT family — any layout
  change must keep the focus-accessor contract honest (verified list in the
  session prompt's Row-26 note: `native_accessibility_bridge`,
  `native_controlled_assets`, `native_developer_diagnostics`,
  `economy_animation`, `stellar_startup_ui`, per-surface tests).

## Execution order

1. Live audit + screenshots → fill the inventory table, ranked issue list below.
2. Shared design language (extend `native_menu_style` / layout helpers).
3. Galaxy map information hierarchy (zoom-banded detail).
4. Colony/planetary command center summary.
5. Remaining phases per the mission spec, smallest-blast-radius first.

## Ranked issues

### CRITICAL

(none found — no screen blocks task completion, hides required information,
breaks navigation/AT flow, or presents wrong state)

### HIGH

1. **No adopted shared design language.** → IN PROGRESS. The theme now
   provides `metric_tile`, `badge`, `key_value`, `focus_ring`,
   `empty_state`, `tab`, `section_header` and `clipped`; planetary,
   economy, supply/logistics and fleet workspaces consume them and their
   palettes alias the semantic colors. Still open: research, shipyard,
   construction, diplomacy, missions, startup chrome migrations.
2. **No global quick-find / command palette** (Phase 5). Known/accessible
   systems, colonies, fleets, contacts, shipyards and missions are only
   findable through per-surface searches or manual map hunting. Design must
   reuse the existing Edit-focused AT contract and FoW-filtered name sources.
3. ~~**Galaxy map has no legend or overlay vocabulary.**~~ → DONE for the
   render vocabulary: collapsible legend explains charted/uncharted stars,
   lanes, territory, fleet markers, selection halo and the planned-route
   green/amber halves; the zoom readout carries a band label
   (OVERVIEW / SECTOR / LOCAL) matching the label-density thresholds.
   Still open: phenomena iconography, strategic overlay toggles.
4. ~~**Colony screen lacks an at-a-glance vitals summary.**~~ → PARTIALLY
   DONE. Vitals strip + numeric alert chips now render for owned colonies
   (observer-safe). Still open: "affected facilities" detail on chips,
   per-issue navigation targets beyond the economy tab.
5. ~~**Logistics workspace is home-system-scoped only.**~~ → DONE within the
   canonical scope: the view now projects the authoritative link graph as
   FREIGHT CORRIDORS rows (origin↔destination, capacity/day, used/day summed
   from real flow allocations, transit days, enabled/bidirectional, and a
   derived Idle/Normal/Busy/Saturated/Disabled status). Links touching
   sealed nodes are dropped rather than partially disclosed. Still open:
   interstellar corridor representation beyond the home system — the
   authoritative `CivilizationLogisticsCoverage` external-system status
   would need projection + a wider surface design.
6. ~~**Economy workspace is a flat undifferentiated list.**~~ → DONE.
   Section headers, metric tiles, tone colors, shared buttons and focus
   ring; verified via live capture (which caught a text-anchor defect the
   tests missed).
7. **Fleet list is flat.** → PARTIALLY DONE. The detail block now uses a
   heading + identity line + `key_value` stat rows; chrome is on the theme.
   Still open: `NativeOwnFleet` has no battle-group layer — grouping by
   role/location/readiness needs a projection change, not more styling.

### MEDIUM

1. Tooltip coverage is uneven — workspace controls mostly lack
   what/why-disabled/what-happens explanations (HUD buttons and asset rows
   are the exception).
2. ~~Notifications feed has no severity iconography or grouping~~ — severity
   axis now shipped (see work log); grouping/filtering remains open.
3. Empty/error states are inconsistent: some surfaces give next-action
   guidance ("No scout or science vessel…"), others show bare "No X" text.
4. Typography hierarchy varies per workspace (heading/body/small pixel
   triples differ slightly everywhere); unify through theme tokens.
5. System view + battle workspace need live visual review — cannot be
   verified from source alone (Phase 30 captures).

### LOW

1. Scrollbar width/track styling differs per workspace.
2. `zoom_text` shows a bare number; a zoom-band label ("OVERVIEW /
   SECTOR / LOCAL") would orient players.
3. Selected-system card (bottom-left) is a fixed-size info block — verify
   clipping at small viewports.
4. Startup/main-menu uses its own palette — cosmetic only.

## Work log

| Date | Change | Commit |
|---|---|---|
| 2026-09-25 | Extended `native_ui_theme` with shared components: `focus_ring`, `metric_tile`, `badge`, `key_value`, `empty_state`, `tab`, plus a `clipped` rect-intersection helper — the Phase-2/3 vocabulary the audit found missing. | 22ca3783 |
| 2026-09-25 | Galaxy map: collapsible MAP LEGEND panel under the zoom readout (charted/uncharted star, lane, territory swatch, fleet marker, selection halo glyphs matching the live render vocabulary). Collapse toggle joins the HUD focus ring ahead of Switch view, announces "Map legend", and is reserved as a label-layout HUD obstacle; clicks inside the panel never become star selections. | 0b4d6604 |
| 2026-09-25 | Colony command center: headline vitals strip (POPULATION / STABILITY / POWER net / FOOD days / EMPLOYED) between the hero block and the fact list, and a two-column issue-chip grid inside the alerts block — chips carry real magnitudes (POWER -99, LIFE SUPPORT 84%) and focus the economy tab on activation. All rows/chips gated on `!observer_only` so the survey-only surface stays leak-free. | 22ca3783 |
| 2026-09-25 | Economy workspace migrated onto the shared theme: KPI cards now use `metric_tile`, section rows use `section_header` (INCOME / OPERATING COSTS now read as headers, not data), buttons use the shared `button` with hover/active/disabled states, palette constants alias the semantic theme colors, and the focus ring uses the shared helper. Zoom readout gained a band label (OVERVIEW / SECTOR / LOCAL) matching the label-density thresholds, and the legend gained a Planned route row (green/amber halves matching the preview colors). Visual capture review caught and fixed a text-anchor defect: centered/right-aligned helpers anchored on the rect's left edge instead of its midpoint/right edge. | f14cf11f |
| 2026-09-25 | Supply/logistics workspace migrated onto the shared theme (themed chrome, hover-aware buttons via a tracked pointer, `metric_tile` KPI cards with a caution-toned shortfall, column-header rule, shared `focus_ring`/`empty_state`, theme scrollbar colors). Battle review found the event feed read as one flat tone — it now severity-tones from observer-visible actor/target ids (own losses danger, inflicted losses success, disruptions caution). System view reviewed: inspector/labels/orbits already structurally sound. Planetary test now covers the deficit path (vitals strip + POWER chip → economy tab). | 0281c063 |
| 2026-09-25 | Fleet workspace migrated onto the shared theme: legacy `ui_skin`/`menu_style` bevel chrome replaced by shared `panel`/`button`/`focus_ring`/`progress`, palette constants alias theme colors, and the fleet detail block is no longer one text blob — it renders as a heading + identity line + `key_value` stat rows (Strength / Fuel / Range / Speed / Order). FLEET_DETAILS/FLEET_ORDER_SUFFIX superseded by FLEET_STAT_* keys in en/de. | 88777eab |
| 2026-09-25 | Logistics freight corridors: `native_logistics::View` gained `LinkRow` projection over the canonical `HomeSystemLogisticsNetwork` links (endpoints sealed-checked, capacity/transit/enabled/bidirectional verbatim, usage summed from real `daily_flow.allocations` route membership, localized Idle/Normal/Busy/Saturated/Disabled status). The workspace appends a FREIGHT CORRIDORS section with its own column captions inside the scroll body; cached rows key on nodes+links. | 8355226d |
| 2026-09-25 | Notification severity axis: `NotificationSeverity` (Info/Positive/Caution/Alert) on `NativePlayerNotification`, assigned by every publisher — campaign feedback (combat→Alert, contact→Caution, completions→Positive), chronicle seeding (`war.*`→Alert), diplomatic events (war→Alert, rejected/terminated→Caution, accepted/activated/contact→Positive), and rejected player commands→Caution. Cards render a tone accent bar plus a color-independent "!" marker on Alerts; category hue is preserved. Verified via `--diplomacy-smoke` capture and new test coverage for feed retention, per-publisher mapping, and accent rendering. | 2d3e6cb3 |

### Implementation notes

- Legend toggle state is client-local (`map_legend_collapsed_`); it is not
  persisted. `smoke_map_point_exposed` now excludes the legend bounds.
- The legend panel shifts right together with the zoom readout when the
  inspection card or fleet panel claims the left edge (shared
  `map_zoom_bounds` origin).
- Vitals/alerts consume `NativeColonyView` fields only — no simulation
  changes, no new engine capability required.
- New localization keys added to `en.json` + `de.json`:
  `HUD_MAP_LEGEND`, `MAP_LEGEND_*` (7), `PLANET_VITAL_*` (6),
  `PLANET_CHIP_*` (5), `PLANET_ALERT_NONE_SHORT`.
