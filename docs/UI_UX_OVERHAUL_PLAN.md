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
| 1 | Startup / main menu / new game / galaxy selection | ☐ | | |
| 2 | Galaxy map (all zoom bands) | ☐ | | highest-priority visual screen |
| 3 | System view | ☐ | | |
| 4 | Planet view / colony command center | ☐ | | |
| 5 | Economy workspace | ☐ | | |
| 6 | Logistics workspace | ☐ | | |
| 7 | Research workspace (adaptive research) | ☐ | | |
| 8 | Diplomacy | ☐ | | observer-safe data only |
| 9 | Fleets / battle groups | ☐ | | |
| 10 | Battle presentation | ☐ | | |
| 11 | Shipyard / construction queues | ☐ | | |
| 12 | Missions | ☐ | | newly live; keep scrolling/focus |
| 13 | Notifications | ☐ | | |
| 14 | Chronicle | ☐ | | privacy filtering preserved |
| 15 | Controlled assets navigator | ☐ | | reference surface — search/collapse/reveal already polished |
| 16 | Settings (hub, general, audio, video, voice, controls) | ☐ | | |
| 17 | Pause menu / session chrome | ☐ | | |
| 18 | Developer surfaces (diagnostics, indices, giant panel) | ☐ | | classify; dev-only by convention |

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

## Known starting hypotheses (verify with live captures — Phase 30)

- No global quick-find exists; search is per-surface only (chronicle, roster, assets,
  research, shipyard, diagnostics, celestial index, seed). Phase 5 designs the shared
  overlay and must respect knowledge/FoW filtering.
- Contextual navigation exists in places (notification feed action buttons, assets
  manage actions, missions OpenColony) but is inconsistent across surfaces — Phase 24
  inventory: which notices know their target but don't link it.
- Galaxy map label/marker density at each zoom band needs measurement; no evidence yet
  that labels cull by zoom.
- Tooltip coverage is uneven (assets rows carry tooltips; many chrome controls do not).
- Several surfaces predate the `focused_expanded`/`focused_value` AT metadata — any
  layout change must keep the focus-accessor family honest.

## Execution order

1. Live audit + screenshots → fill the inventory table, ranked issue list below.
2. Shared design language (extend `native_menu_style` / layout helpers).
3. Galaxy map information hierarchy (zoom-banded detail).
4. Colony/planetary command center summary.
5. Remaining phases per the mission spec, smallest-blast-radius first.

## Ranked issues (fill during audit)

### CRITICAL

(none yet)

### HIGH

(none yet)

### MEDIUM

(none yet)

### LOW

(none yet)

## Work log

| Date | Change | Commit |
|---|---|---|
| | | |
