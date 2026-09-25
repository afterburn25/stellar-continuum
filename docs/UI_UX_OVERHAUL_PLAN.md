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
| 9 | Fleets / battle groups | ☑ | HIGH | `native_fleet_workspace` outliner + detail + orders (hold/defend/retreat), locate, engage, route preview, recovery confirm, ship-art rows, empire-overview detail when nothing selected. No battle-group layer in `NativeOwnFleet` — fleets are flat rows; per-fleet ships/strength breakdown not surfaced. ~~Flat list will not scale to many fleets; no grouping by role/location.~~ **Shipped:** the outliner renders status-grouped rows (in combat → in transit → on mission → stationed, localized headers) and Controlled Assets sorts the FLEETS section urgent-first; battle-group/per-ship breakdown remains open. |
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
   palettes alias the semantic colors. Shipyard, construction,
   diplomacy, research, the colony roster, missions, and the startup /
   pause chrome are now migrated as well, and every keyboard/pad focus
   ring routes through the shared `focus_ring` helper.
2. ~~**No global quick-find / command palette**~~ → DONE: Ctrl+K opens a
   modal palette built from FoW-filtered view models covering known
   systems, owned colonies, own fleets, identified contacts, active
   mission fleets, and workspace command rows (Research/Economy/
   Logistics/Shipyard/Construction/Diplomacy/Missions/Planets) that route
   through the same open+refresh calls as the navigation rail. Type
   badges, context labels, kind-label matching, arrow/Enter/Escape
   navigation and the shared Edit-focus AT contract.
3. ~~**Galaxy map has no legend or overlay vocabulary.**~~ → DONE for the
   render vocabulary: collapsible legend explains charted/uncharted stars,
   lanes, territory, fleet markers, selection halo and the planned-route
   green/amber halves; the zoom readout carries a band label
   (OVERVIEW / SECTOR / LOCAL) matching the label-density thresholds.
   Phenomena iconography and strategic overlay toggles shipped: surveyed
   regions carry designation labels once legible, and the legend's
   lane/territory/phenomenon rows double as show/hide layer toggles
   (FoW shroud and marker vocabulary always remain).
4. ~~**Colony screen lacks an at-a-glance vitals summary.**~~ → DONE.
   Vitals strip + numeric alert chips render for owned colonies
   (observer-safe); chips now route to the first affected structure
   (structures tab, site selected) when the deficit maps to facilities —
   else the economy tab — and hover lists the affected structures by name.
5. ~~**Logistics workspace is home-system-scoped only.**~~ → DONE within the
   canonical scope: the view now projects the authoritative link graph as
   FREIGHT CORRIDORS rows (origin↔destination, capacity/day, used/day summed
   from real flow allocations, transit days, enabled/bidirectional, and a
   derived Idle/Normal/Busy/Saturated/Disabled status). Links touching
   sealed nodes are dropped rather than partially disclosed. Interstellar
   coverage now ships too: the supply view projects the canonical
   `CivilizationLogisticsCoverage` — owned systems beyond home get
   INTERSTELLAR COVERAGE rows (worst colony condition, colony count, local
   capacity, support demand, import requirement, corridor flag) and the
   section surfaces `unrepresented_interstellar_support_per_day` as an
   amber demand-gap callout.
6. ~~**Economy workspace is a flat undifferentiated list.**~~ → DONE.
   Section headers, metric tiles, tone colors, shared buttons and focus
   ring; verified via live capture (which caught a text-anchor defect the
   tests missed).
7. ~~**Fleet list is flat.**~~ → DONE for display organization: the
   outliner renders a status-grouped projection (IN COMBAT → IN TRANSIT →
   ON MISSION → STATIONED) shared by click/focus/scroll/render/tooltip
   paths, and Controlled Assets sorts the FLEETS section urgent-first.
   Per-fleet composition now surfaces too: `NativeOwnFleet` projects the
   resolved design name, cargo fill, embarked population, and tactical
   vessel condition, rendered as trailing stat rows. Fleets are single
   vessels in the model — a true battle-group layer would need a
   fleet-composition change in core, out of client scope.

### MEDIUM

1. Tooltip coverage is uneven — PARTIALLY DONE. Disabled action buttons now
   explain themselves at the point of interaction: `theme::hover_tooltip`
   renders the authoritative blocker (shipyard `batch_blocker`/
   `cancellation_blocker`, construction `start`/`queue` messages, planetary
   hub/building lock reasons and affordability/capacity/surface gates) when
   the pointer rests on an unavailable control, and the HUD rail's hand-rolled
   hint was folded into a new compact `theme::hint` helper. The diplomacy
   action row is no longer hidden-when-illegal: all three actions render in
   fixed slots, disabled when unavailable, with the authoritative
   communication/political status surfaced as the hover why, and the
   negotiation modal now lists all six terms with unavailable ones disabled
   (`ModalTerm::enabled` + `tip` carrying the authoritative political /
   access / agreements / communication status — no legality rules duplicated
   in UI). HUD chrome (nav rail, pause/speed/notifications, zoom, legend
   toggles, Switch view) now renders hover hints via `theme::hint` reusing
   the exact localized labels the focus ring announces — pointer and
   keyboard/AT share one vocabulary. Non-action content now explains
   itself too: diplomacy relationship meters (TRUST/RESPECT/FEAR/
   HOSTILITY/COOPERATION) and the six economy KPI tiles carry localized
   hover explainers (`DIPLOMACY_TIP_*` / `ECONOMY_TIP_*`, en+de) through
   `theme::hover_tooltip`. Still open: a few minor status rows elsewhere
   could use the same treatment if captures flag them.
2. ~~Notifications feed has no severity iconography or grouping~~ — severity
   axis and severity filtering shipped (see work log). Category-based
   grouping remains a possible future refinement.
3. Empty/error states are inconsistent — PARTIALLY DONE. Colony roster
   and the research tree now pair the bare "No X" line with a localized
   next-action hint through the shared `empty_state` helper (shipyard,
   construction and missions already guided). Remaining bare states:
   minor lists that have no meaningful next action.
4. Typography hierarchy varies per workspace — PARTIALLY DONE. New
   `theme::type` ramp (title/body/small = 24/15/12, compact_body/
   compact_small = 14/11) is the single vocabulary; construction, colony,
   diplomacy (26→24) and economy (23→24) migrated to the canonical triple,
   battle/fleet/inspection body+small to the compact ramp, and research's
   title joined (its denser 16/14 tree text stays documented custom).
   Deliberately untouched: settings dialogs (bigger reading sizes),
   overview/map labels (different four-rung shape), and missions/research
   body text where the tighter ramp is load-bearing for density.
5. ~~System view + battle workspace need live visual review~~ → REVIEWED.
   `--system-smoke` and `--battle-smoke` captures inspected: system view
   inspector/orbits/controls are sound; battle feed events were bare text
   on the starfield — they now render as translucent cards with severity
   accent bars matching the notification-card vocabulary.

### LOW

1. ~~Scrollbar width/track styling differs per workspace.~~ → DONE: new
   `theme::scrollbar` (faint keyline rail + `selected` thumb) replaces the
   per-surface hand-rolled thumbs in body inspector, chronicle, controlled
   assets, economy, system inspection, logistics, new-game species
   details, notifications, research inspector + guided dashboard, and the
   planetary facts/queue rails. The fleet outliner list gained a scrollbar
   for the first time (render-path `sync` keeps the thumb honest).
2. ~~`zoom_text` shows a bare number; a zoom-band label ("OVERVIEW /
   SECTOR / LOCAL") would orient players.~~ → DONE with the legend work
   (the readout carries the matching band label).
3. ~~Selected-system card (bottom-left) is a fixed-size info block —
   verify clipping at small viewports.~~ → VERIFIED: `inspection_bounds`
   clamps height to `max(100, min(500s, bottom-top))` inside scaled
   insets; cards render fully down to ~500px-high viewports.
4. ~~Startup/main-menu uses its own palette~~ → DONE. Startup screens and
   the pause menu alias theme colors and use shared `button`/`focus_ring`;
   all hard-coded `{160,210,255}`/`{164,221,237}` focus rings across the
   client now render through the shared helper (one ring color
   everywhere).

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
| 2026-09-25 | Shipyard workspace migrated onto the shared theme: palette constants alias `theme::color`, the shared `button` helper replaces the hand-rolled panel+text chrome (categories/search/sort/filter/qty/favorite/action all get real hover/active/disabled states — the build action now visibly disables instead of just re-tinting), design cards and order rows use themed fills/keylines with a selection accent bar, BUILD ORDERS uses `section_header`, empty surfaces use `empty_state` with a next-action hint (SHIPYARD_EMPTY_HINT / SHIPYARD_NO_ORDERS_HINT, en+de), and the focus ring uses the shared helper. Verified via `--shipyard-smoke` capture. | d337de8f |
| 2026-09-25 | Construction workspace migrated onto the shared theme: palette aliases `theme::color`, section panels use `panel` + `section_header` (KNOWN PROJECTS / PROJECT DETAILS / CONSTRUCTION STATUS now read as headers with rules), actions use the shared `button` with real disabled state and a construction-tone accent, selected rows carry a selection accent bar, empty lists use `empty_state` with localized hints (CONSTRUCTION_NO_PROJECTS_HINT / CONSTRUCTION_NO_ACTIVE_HINT, en+de), focus ring is the shared helper. Verified via `--construction-smoke` capture; the thin-track containment test now scopes to the orders panel since section-header rules are also thin rects. | d3661410 |
| 2026-09-25 | Diplomacy workspace migrated onto the shared theme: palette constants alias `theme::color` (relations teal → `diplomacy`, status gold → `economy`, hostility red → `danger`), surfaces use `menu_panel`/keylined regions, filter chips and action buttons use the shared `button` with hover/active states (Declare war keeps a danger keyline + accent bar), the tab strip uses `tab` (active underbar), relationship meters draw on `canvas` tracks with semantic tones, modal negotiation/confirm/cancel buttons use `button` with danger tone on destructive confirms, CONTACT DIRECTORY / RELATIONSHIP use `section_header`, and the focus ring is the shared helper (test now asserts `color::focus` instead of the retired local accent). Verified via `--diplomacy-smoke` capture (progress mode, proposal accepted, notifications read). | 15e21b1d |
| 2026-09-25 | Global quick-find palette (Ctrl+K): modal searchable overlay indexing known systems, owned colonies, own fleets and identified contacts — all sourced from FoW-filtered authoritative view models so uncharted names never leak. Results render type badges + context labels; field opens focused (AT `Edit`), arrows move into results, Enter activates, Escape/outside-click closes; `set_focused_text`/`focused_value` integrate the existing AT text contract. Activation routes through existing paths: map re-center + inspect for systems, `open_overview_colony`, `focus_mission_fleet`, and diplomacy open + `select_contact_civilization`. New `native_quick_find` test target covers filtering, navigation, activation, capture and AT; `--quick-find-smoke` verified on Vulkan (opened/8 entries/4 matches/1 highlighted, save ok). Shipyards and missions remain unindexed — follow-up. | f43bcc62 |
| 2026-09-25 | Research workspace migrated onto the shared theme: palette aliases `theme::color`, outer surface uses `menu_panel`, domain tabs gain hover/active fills with a Science accent bar, the four view tabs use the shared `tab` underbar treatment, toolbar/sort/filter/queue buttons use the shared `button` (Cancel research keeps a danger keyline), the search field frames with a focus-colored keyline, RECOMMENDED badges and progress bars take the science tone, SELECTED TECHNOLOGY / ACTIVE RESEARCH are `section_header`s, and the focus ring is the shared helper. Scroll regions (graph, dashboard list, active strip) keep flat clipped fill/stroke so nothing escapes the viewport. Verified via `--research-smoke` Vulkan capture (active program, recommended cards, inspector, queue buttons all render). | 42c01435 |
| 2026-09-25 | Quick-find extended to missions and workspaces: `EntryKind::Mission` indexes active mission fleets (`build_mission_board`, activating through `focus_mission_fleet`) and `EntryKind::Workspace` adds command rows for the eight navigation surfaces (Research/Economy/Logistics/Shipyard/Construction/Diplomacy/Missions/Planets) routing through the same open+refresh calls as the rail. Kind labels join the searchable text, badges take Science/Neutral tones, placeholder text now names missions/screens (en+de), and new tests cover kind matching, activation and badge rendering. Re-verified via `--quick-find-smoke` (16 entries, filtered query intact). | 47870e62 |
| 2026-09-25 | Missions workspace migrated onto the shared theme: palette aliases `theme::color` (cyan accents → `selected`, gold → `economy`), outer surface uses `menu_panel`, the Missions/Colony Sites strip uses `tab` (active underbar), close/nav/View/Land/Collect controls use the shared `button` styling with hover/disabled states via a tracked pointer, SELECT SHIP ON MAP is a success-toned primary action, mission cards and colony rows use `surface`/`keyline`, the empty missions list renders the shared `empty_state`, and the focus ring is the shared helper. Fixed a pre-existing defect found by capture: the panel top ignored `native_workspace_top`, hiding the title and close button under the nav bar at 720p. The colony smoke now opens the missions board and stores a `-missions` capture. | bcf1677d |
| 2026-09-25 | Startup chrome migrated onto the shared theme: `native_startup_workspace` palette constants alias `theme::color` (brand green → `success`, gold → `economy`, warning → `caution`), dialog surfaces use `menu_panel`, Development/ModeSelection/LoadSlots/Busy/Failure buttons use the shared `button` (LOAD SELECTED is a success-toned primary gated on a chosen slot), and the in-game pause menu's buttons/focus ring use `theme::button`/`focus_ring` with `text_primary`/`text_secondary` chrome. Focus-ring sweep: every remaining hard-coded ring — HUD controls, pause menu, startup, settings (general/audio/video/voice), inspection, chronicle, battle, colony freight confirm, galaxy creation, new-game setup, settlement confirm, small-body survey, planetary — now renders through `theme::focus_ring`. Verified via `--restart-exit-smoke` (entry screen) and `--smoke` (pause menu) captures; three tests updated to assert `color::focus`. | 7617f665 |
| 2026-09-25 | Colony roster migrated onto the shared theme: palette constants alias `theme::color` (selection cyan → `selected`, warning amber → `caution`), the search field frames with a focus-colored keyline, REFRESH/close use the shared `button`, rows use `surface_secondary`/`surface_hover` with a `keyline_strong` hover outline, the scrollbar track uses `keyline`, and the focus ring is the shared helper. Empty-state sweep: the roster's bare "No owned colonies" now pairs with a localized next-action hint (`ROSTER_LIST_EMPTY_HINT` — the unavailable state stays hint-free), and the research tree's "No known research" gains `RESEARCH_NO_MATCH_HINT`; both render through the shared `empty_state`. Verified via `--colony-smoke` Vulkan capture (themed search/chrome/rows) and updated roster tests asserting theme constants instead of retired literals. | 7d9dd2d3 |
| 2026-09-25 | Notification severity filtering: ALL / IMPORTANT chips in the feed intro strip (shared `button`, caution tone on IMPORTANT) keep the session filter — IMPORTANT projects the feed down to Caution/Alert items through a view-local projection (`visible_items`; the authoritative deque is never touched). Filtering joins the focus ring (chips ring after CHRONICLE/X with localized AT labels `NOTIFY_FILTER_*_LABEL`), resets scroll and ring on toggle, clamps wheel scroll to the filtered extent, and a filtered-empty feed renders its own hint (`NOTIFY_EMPTY_IMPORTANT`). New `severity_filter` test covers chip activation, feed immutability, filtered rendering, scroll bounds, ring order, Return-activation of a filtered card action, reopen persistence and the filtered-empty state; the previously dead `keyboard_focus` test was wired into `main` with corrected ring indices. Smoke note: an earlier run tripped the `--diplomacy-smoke` feed-count gate with `items=8` against saves that prior smoke runs had autosaved in place; re-authoring the save from the canonical Rows fixture restored `items=2` and the gate now passes end-to-end. The gate also reports the offending items in its failure text. | 31d1f30e |
| 2026-09-25 | Why-disabled tooltips at the point of interaction: new `theme::hint` (compact single-line, measurer-aware) and `theme::hover_tooltip` (gates the shared `tooltip` on hover + non-empty body). Shipyard's disabled BUILD/CANCEL button surfaces `batch_blocker()`/`cancellation_blocker`; construction START/QUEUE surface the authoritative `start`/`queue` action messages; the planetary command-center button surfaces `hub_upgrade_lock_reason` (with localized affordability/requirements fallbacks); and the structures-tab action rows (Begin construction, Upgrade, Repair) surface their real gates — observer/surface/capacity/authorization/condition — via new `PLANET_TIP_*` keys in en+de. The HUD rail's hand-rolled hint block migrated onto `theme::hint`. Tests cover exact-match tooltip text per surface. | 07cc79b1 |
| 2026-09-25 | Alert-chip navigation + affected-facility detail: colony issue chips now activate into the structures tab with the first affected site selected (dark POWER structures, unstaffed buildings, non-operational essential services, efficiency-degraded buildings under underfunding), falling back to the economy tab for colony-wide deficits; hovering a chip lists up to four affected structure names via `hover_tooltip` (`PLANET_CHIP_AFFECTED`, `PLANET_CHIP_MORE`, `PLANET_TIP_COLONYWIDE`, `PLANET_TIP_HUB` in en+de). NO COMMAND CENTER routes to the structures tab. Planetary test covers both the hover detail and site selection. | adb306e7 |
| 2026-09-25 | Diplomacy actions are discoverable when illegal: the relations action row no longer hides unavailable actions — Open/Establish transmission, Negotiate, and Declare war render in three fixed slots with the shared disabled button state, and hovering a disabled action surfaces the authoritative `communication_status`/`political_status` as the why (no legality rules duplicated in UI). Render, click dispatch, and the focus ring enumerate the same fixed slots; disabled slots capture clicks as no-ops and stay out of the ring. The action area cap grew 128→160 to hold three rows + the discovery hint, which itself now only renders where it does not overlap the tab strip. Verified via `--diplomacy-smoke` end-to-end (`items=2`, proposal accepted, unknown contact redacted) — the unknown-contact capture shows all three slots disabled without the old empty panel. Tests cover disabled render, tooltip text, and captured no-op clicks. | e015e209 |
| 2026-09-25 | Negotiation modal keeps illegal terms visible: `ModalState::terms` became `ModalTerm{label, action, enabled, tip}` — all six terms render in fixed slots (42s pitch), unavailable ones draw disabled via the shared button and surface the domain's authoritative status on hover (`agreements_summary`/`access_summary`/`political_status`/`communication_status`, no legality rules in UI). Click dispatch, render, and the (y,x)-sorted focus ring share fixed indices; disabled terms capture clicks as no-ops. Also fixed a dangling-`else` in modal focusables that only the new disabled rows could expose. Tests cover all-six render, captured no-op clicks on disabled terms, and the at-war fixture. | f46fd46c |
| 2026-09-25 | Fleet organization — two layers, both projection-only over authoritative state. The `native_fleet_workspace` Outliner (dev-inspection / embedded harness presentation) now renders a status-grouped list: `fleet_group`/`fleet_rows` project fleets into IN COMBAT → IN TRANSIT → ON MISSION → STATIONED with localized `FLEET_GROUP_*` caption headers (en+de) drawn through `section_header`; a single non-empty group renders header-free so flat geometry is preserved. Every consumer — focus ring, wheel scroll, keyboard snap, click dispatch, render, hover tooltip — iterates the same projected rows; `view_->own_fleets` order is never touched. The player-facing counterpart: `native_controlled_assets` rows gained an `urgency` rank and the FLEETS category now surfaces engaged → in-transit → on-mission → idle hulls ahead of creation-order sorting, matching the displayed activity labels. Tests cover group-header rendering, click dispatch beneath headers, End-key scroll-into-view for off-viewport rows, single-group flatness and assets urgency ordering; `--fleet-smoke` re-verified live (moving colony fleet now tops the FLEETS section). | 5d68bd72 |

| 2026-09-25 | Phenomenon identification on the galaxy map: surveyed regions render a player-facing designation + type-name label once their projected extent is large enough for legible text (90–520px window — suppressed at overview clutter and at screen-filling zooms), drawn after the decal batches so labels sit above the clouds they name, and clipped to the screen. Gating reuses the authoritative `surveyed` set (systems partially surveyed or better / developer session), so unknown regions never disclose a name; unsurveyed artwork still renders untouched. The MAP LEGEND gained a "Surveyed phenomenon" row with a soft-glow glyph (en+de). Tests cover surveyed-label emission ("RC-1 · Reflection Nebula"), unsurveyed secrecy, and overview suppression; `--galaxy-art-smoke` captures verified the legend row and decluttered overview/regional bands. | ef368f49 |

| 2026-09-25 | Strategic overlay toggles: the MAP LEGEND's lane / empire territory / surveyed phenomenon rows are now show/hide layer switches — each draws a checkbox at the row's right edge, dims glyph + label while off, and hover-highlights like the panel's other interactive rows. State is client-local (like the collapse toggle), click press/release matching reuses the shared `map_legend_row_bounds` geometry, and the three rows join the HUD focus ring after the legend toggle with localized AT labels (`HUD_MAP_LAYER_*`, en+de). Territory hiding uses a new `NativeTerritoryRenderStyle::draw_ownership` flag that suppresses fill/contours/claims/region labels while the unexplored-space fog shroud keeps rendering — FoW geometry is never toggleable. Phenomena hide also suppresses the map hover inspection for the hidden layer; lanes hide only the charted-lane lines (markers and knowledge vocabulary untouched). Territory tests assert ownership marks vanish while `fog_images` still emit; `--galaxy-art-smoke` re-verified (territory stats unchanged with all layers on). | d5eacf30 |

| 2026-09-25 | HUD hover hints: pointer resting on any HUD ring item (nav rail, pause/speed/notifications, zoom controls, legend toggle + its three layer rows, Switch view) renders a compact `theme::hint` with the same localized label the focus ring announces for that action — one vocabulary for pointer, keyboard and AT users. Gated off while the pause menu, settings, or quick-find modal is open. `--galaxy-art-smoke` re-verified clean (no hint when the pointer rests off-chrome). | cf52ea0e |

| 2026-09-25 | Fleet composition in the detail card: `NativeOwnFleet` now projects `design_name` (resolved through `find_ship_design`), `cargo_materials`/`cargo_material_capacity`, `embarked_population_millions`, and `has_vessel_state`/`hull_integrity` from `tactical_vessel`. The detail block renders Design/Condition/Cargo/Embarked rows (`FLEET_STAT_*`, en+de) after the core telemetry — extras land last so cramped cards clip the least-critical rows first — and stat rows now clip to the details block so they can never spill into the route preview (the armed-fleet stack had silently overdrawn the rail gap before). `fleet_height` grows to 215s when the detail space can spare it (>300s), keeping the 720p behavior identical. Test covers all four composition rows; `--fleet-smoke` capture shows a colony ship reporting Design "Interstellar Colony" + Embarked 250.0M. | 675262f1 |

| 2026-09-25 | Interstellar logistics coverage: the supply view's projector now returns the canonical `CivilizationLogisticsCoverage` (which embeds the home network) instead of `home_system_logistics` alone — `View` gains `external` rows (`ExternalRow{system, name, condition, colonies, local capacity, demand, import, corridor}`), `owned_system_count` and `support_gap_per_day`. Sealing is unchanged: external rows whose civilization or system record can't be verified are dropped, same rule as node/link endpoints. The workspace renders a third scroll section — INTERSTELLAR COVERAGE — with per-system rows and an amber "unrepresented interstellar demand" callout sourced from Core's gap flag. Tests cover canonical row parity, foreign/home exclusion, header/row/gap rendering and clip bounds; `--logistics-check` smoke verified live on a fixture with a Velari colony (row renders Critical). Also fixed a stale assertion in `native_startup_artwork`: it pinned the pre-theme accent literal `{122,230,190}` instead of `theme::color::success`. | 87b4a468 |

| 2026-09-25 | "What does this do" tooltips on non-action content: the five diplomacy relationship meters each gain a hover explainer describing what the axis measures (`DIPLOMACY_TIP_*`), and `NativeEconomyCard` gained a `detail` field — projected localized explainers (`ECONOMY_TIP_*`) rendered through `theme::hover_tooltip` on each KPI tile, with the tile detail folded into the cache signature. Tests cover meter-tooltip text on hover and KPI-tile explainer rendering; `--economy-check` smoke re-verified canonical totals and priority save. | 00759a07 |

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
  `PLANET_CHIP_*` (5), `PLANET_ALERT_NONE_SHORT`,
  `RESEARCH_NO_MATCH_HINT`, `ROSTER_LIST_EMPTY_HINT`.
