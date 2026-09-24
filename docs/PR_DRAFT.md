# PR draft — engine/space-strategy-simulation-specialization → work/foundation-1-30-codex-integration

`gh` CLI is unavailable in the development environment; this file is the
review-ready PR description for manual creation. Delete when the PR is
opened and this text is pasted in.

---

## Title

Space strategy engine specialization: M1–M15 frameworks, Core adoption, chronicle, editor tools

## Summary

Specializes the Stellar Engine into a reusable, deterministic C++23
space-strategy/simulation engine (256 commits, ~38.5k insertions, 290
files). Not an Unreal clone — the goal is a strategy/simulation
substrate: scheduling, economy, population, colonies, infrastructure,
logistics, planetary development, terraforming, strategic AI, warfare,
event history, rendering frontier, accessibility, and editor tooling —
all deterministic, persistable, tested, and consumed by real Core/App
surfaces.

**Architecture discipline throughout:** engine supplies mechanics and
frameworks; Core keeps gameplay authority. Core consumption happens
through read-only projection adapters (`campaign_economy_projection`,
`campaign_warfare_projection`, `planetary_adapter`) feeding diagnostics
— never dual simulation authority. Observer privacy is enforced in
Core/engine projections, never re-derived in UI.

## Milestone map (docs/SPACE_STRATEGY_ENGINE.md)

| # | Milestone | Status |
|---|---|---|
| 1 | Simulation scheduler + LOD | IMPLEMENTED — `SimulationScheduler`/`SimulationExecutor`; Core's 12-phase step coordinator adopted it (parity-verified) |
| 2 | Economy framework | IMPLEMENTED (engine) — `EconomyCatalog`/`analyze_economy`; Core consumption via sustenance/power projection into operations diagnostics |
| 3 | Population | IMPLEMENTED (engine) — cohort demographics, 17M-headcount scale; `population_habitability` needs bridge |
| 4 | Colony framework | IMPLEMENTED (engine) — districts/structures/utilities, 1000-colony scale |
| 5 | Infrastructure networks | IMPLEMENTED (engine) — `FlowNetwork` directed utility graphs |
| 6 | Strategic logistics | IMPLEMENTED (engine) — `LogisticsNetwork` freight routes, deterministic dispatch |
| 7–8 | Planetary + terraforming | IMPLEMENTED (engine) — `HabitabilityProfile`/`evaluate_habitability`; `to_engine_environment` Core projection feeds it |
| 9 | Strategic AI | IMPLEMENTED (engine) — `StrategicMind` utility machinery |
| 10 | Strategic warfare | IMPLEMENTED (engine) — `WarfareModel` cohorts/interdiction/Lanchester; Core consumption via theater projection + `foreign_armed_presence` diagnostics |
| 11 | Space rendering | IMPLEMENTED — scene3d GPU path with planet/ring/star materials; HDR/tonemap resolve (RGBA16F + fullscreen resolve, UNORM fallback); SSBO instanced rendering with `DrawBatcher`-owned ordering/batching; `RenderGraph` pass scheduling (scene→tonemap); `TextureStreamer` byte-budget residency with runtime-tunable limit, pinned-fallback pop-in, screen-footprint LOD demand and partial mip-tail residency. Remaining: no indirect draw |
| 12 | Editor tools | **PARTIAL** — 16 shell tools (Projects…Galaxy incl. Simulation/Colony/Economy/Planet/AI/Warfare/Missions/Physics genre inspectors + the `GalaxyMap` debugger) + per-tool `--frames` ctest smoke; generated projects scaffold executor + persistence |
| 13 | Scale benchmarks | IMPLEMENTED — `simulation_scale_250…5000`, `combined_scale` (400 settlements, ~870µs/tick, bit-identical) |
| 14 | Event history | IMPLEMENTED — `EventHistory` observer-private chronicle; runtime records every advance incl. diplomatic journal entries; v17 save payload; retention policy |
| 15 | Public-information hooks | IMPLEMENTED — `feed()` substrate; notification seeding + scrollable chronicle browser with every query axis exposed (domain/significance/actor/tag/search/time-window paging); map + diplomacy navigation; voice announcements |

## Verification

- `2026-09-24-development-sync` receipt: **294/294** native tests green
- `2026-09-23-chronicle-suite` receipt: **295/295** runnable tests green
  at `d68c98af` (15 `engine_shell_tool_*` tests need a display)
- User desktop run (`ba7fbf05`): **310/310** including shell smoke tests
- User desktop run (`d5c7e0d3`): **314/314** after GalaxyMap + replay chain
- Handoff receipt (`550a50d3`): **315/315** after app/editor lanes
- Handoff receipt (`4b82ca7a`): **321/321** full suite over the merged
  state incl. the cadence-oracle tests
- New this branch: `replay` unit tests for per-section checkpoint
  divergence localization; `campaign_phase_cadence_oracle` pins the
  per-phase activity matrix
- `stellar-continuum-native` builds `/W4 /WX` clean throughout

## Notable capabilities added

- `EventHistory` + `campaign_event_history` adapter — persistent
  observer-private strategic chronicle; diplomatic journal entries join
  via an event-id watermark (exactly-once across saves), exempt from
  knowledge widening so excluded observers never learn identities
- `maintain_chronicle` retention — protects major events from
  bounded-capacity trivia eviction
- Native chronicle browser — newest-first projection, 4000-cap with
  true totals, all `HistoryQuery` axes surfaced, clickable tag chips,
  system/diplomacy navigation, `wants_text_input()`-gated search
- Engine persistence + JSON codecs for every specialization framework;
  `PhysicsWorld` capture/restore; `MissionRuntime` serialize/restore
- Replay divergence localization — per-JSON-section checkpoint hashes
  name the diverging subsystem (`save:World.Fleets`); on divergence the
  actual canonical document dumps to `replay-divergence-<tick>.json`;
  recorded checkpoints a replay never produced are flagged as skipped
  saves; `--replay` warns on seed/game_version provenance mismatch
- `route_unreachable`/`power_brownout`/`sustenance_shortfall`/
  `foreign_armed_presence` operational diagnostics over authoritative
  reach/economy/warfare projections
- `engine::GalaxyMap` — reusable game-agnostic star-chart model
  (systems/lanes/markers, deterministic ascending-id queries, lazy
  adjacency, spatial queries, versioned `State` + framework JSON
  codec); `project_galaxy_map` fills it from authoritative Core
  geography; GALAXY shell tool debugs a synthetic chart
- Diplomatic chronicle privacy hardening — an empty restored journal
  audience falls back to involved parties instead of leaking the event
  to all observers (`visible_to` empty = public by `EventHistory`
  contract)
- `IntegratedAdaptiveCampaignRuntime` records chronicle entries every
  advance; `DiplomacyState` read-only journal accessors
- Engine view-model framework adoption — `VirtualizedList` (colony
  roster, editor systems list, all four diagnostics views, both
  developer indexes, empire monitor, phenomena dump — `sync_rows` owns
  the shared configure/clamp/snap contract, with selection-follow +
  tail clamping), `TableModel` (roster sort/filter +
  diagnostics phase-table sort, `refilter` keeps state across live
  `set_rows`), `TreeModel` (editor system list, diagnostics ENTITIES
  inspector with id-stable selection + detail pane + search reveal,
  controlled-assets navigator with persisted collapse), `UndoHistory`
  (editor annotation layer)
- Keyboard-focus contract across the native client — settings
  hub/panels, startup + new-game flow, settlement/logistics/economy/
  shipyard/construction/research/fleet/battle workspaces, chronicle,
  notification feed, diplomacy, colony roster, controlled-assets
  navigator, small-body survey panel, pause menu, inspection card:
  Tab/arrows/Home/End rings, Return/Space activation through the same
  dispatch pointers take, edit-mode text-field ownership, modal
  narrowing, `wants_keyboard_focus()` suppressing bound galaxy actions
  (Space→pause) while a ring is live. Always-on chrome is reachable too:
  `map_focus_group_` chains the assets navigator, fleet outliner and HUD
  chrome as ordered focus groups — a nav key that would wrap a group's
  boundary releases the ring so the same key lands in the next group —
  and `NativeUiLayout::hud_actions()` drives a HUD focus ring whose
  activation replays the pointer dispatch paths. The settings hub Controls
  view is a real rebind UI on the live `InputMapper`: it lists every
  bindable GALAXY action with `describe_bindings` labels, captures the next
  keypress, right-click or gamepad button (modifiers fold into chords,
  alternates survive, Escape/click cancels, conflicting primaries are
  stolen with a reassignment notice), and persists through
  `save_contexts` to galaxy-controls.json loaded over the defaults at
  startup. Non-keyboard bindings actually fire: the client feeds
  `GamepadButton`/`MouseButton` through the same gameplay gate as keys
  and records them for replay as `gamepad_button`/`mouse_button`
  commands. Gamepad camera axes land through a `GALAXY_PAD` context —
  left stick pans, right stick zooms (Axis1D, dead-zone + dt-scaled,
  same surface gate as wheel input) — injected on user-map load only
  when the saved map lacks it. The axis rows are rebindable in the
  Controls view: capture accepts a stick deflection or wheel scroll.
  The navigation smoke exercises pad/right-click rebinding and stick
  pan/zoom end-to-end — and caught a real defect: Escape cleared the
  inspection card without resetting `selected_id_`, so
  `refresh_inspection()` reopened it the same frame; the card now
  deselects on Escape like its own close path does
- Screen-reader substrate — `AccessibilityAnnouncer` is the bounded
  live-region queue (polite/assertive, dedup, capacity eviction,
  monotonic sequences) a platform AT bridge will drain; live consumers
  today: notification arrivals announce, pause-menu/HUD focus moves
  announce localized labels, every focus-bearing surface exposes
  `focused_label()` so the dispatcher names the ringed control on every
  focus move, and pending announcements render through the voice-caption
  channel while subtitles are enabled
- Accessibility preferences — `interfaceScale` (Compact→Huge user
  multiplier through `NativeUiLayout`), `reduceMotion`, `reduceFlashing`,
  `highContrast` (global luminance pass), `colorBlind` (Machado
  daltonization), plus `subtitlesEnabled`/`subtitleScale`/`textScale` —
  a third General Settings row whose consumers are real: the caption
  renderer gates on subtitles and multiplies the voice-preferred pixel
  size, and `NativeUiLayout::set_text_scale` enlarges shared font
  metrics without growing chrome — all persisted with draft/cancel
  semantics
- Diagnostics tooling — ENTITIES inspector over the read-only campaign
  projection (incremental sync, tag drill-down, detail pane, search,
  full keyboard tree contract), phase-table sort, events-view search,
  continuation inspection, fault capture for developer step throws,
  authoritative logistics/treasury findings
- Engine hardening — profiler recording off the global mutex, replay
  document checkpointing (no re-parse) with expected-document sidecars
  (`<recording>.expected/<tick>.json`) and `document_leaf_diff` leaf-level
  divergence reports (`replay-divergence-<tick>.diff.txt`), and a
  `--replay-until <tick>` bisect dump that canonicalizes state at a chosen
  tick and leaf-diffs it against the expected sidecar, plus a headless
  `--replay-info <file>` JSON inventory (header, command ticks/kinds,
  per-tick checkpoints + expected-sidecar presence) for picking bisect
  ticks, occupancy
  censuses (MemoryTracker world/event-history/replay-recorder),
  pull-based audio stream decoder + equal-power panning, phase-cadence
  oracle pinning the per-phase activity matrix for minimal + seeded
  worlds plus event-driven command wakes
- Editor annotation layer — trait overrides (anomaly/rare/pre-warp),
  numeric `radiusEarth`/`orbitAu` overrides, multi-file projects
  (`project.json` + `assets/` with embedded png discovery)
- `campaign_colony_projection` — Core projects authoritative colony
  state into the engine `Colony` settlement model (first projection-
  beyond-diagnostics consumption)

## Limitations (honest)

- M11 space rendering is now IMPLEMENTED: instancing (SSBO arrays),
  RenderGraph scheduling, TextureStreamer byte-budget residency +
  mip-tail streaming all landed; remaining is indirect draw.
  Offscreen Vulkan captures verify in CI, windowed shell smoke still
  needs a desktop display
- Authoritative Core adoption of economy-catalog/colony/logistics/
  population/strategic-AI frameworks pending — current consumption is
  intentionally read-only projection to avoid dual authority
- `physics`/`mission_graph` have shell-tool consumers only; no game
  path consumes them yet
- `engine::GalaxyMap` (systems/lanes/markers, deterministic adjacency,
  spatial queries, versioned persistence + framework codec) landed
  with a Core `project_galaxy_map` projection and a shell debugger —
  full-authority projection; observer filtering is a consumer
  responsibility and the native client's map is not yet migrated onto
  it
- Chronicle browser: multi-select filter composition unexposed;
  `HistoryEvent::summary` is opaque text (structured localization is an
  upstream event-pipeline change)
- Accessibility stays PARTIAL: the announcement substrate +
  focus-label convention landed (announcer queue, notification/menu/HUD
  consumers, `focused_label()` on every focus-bearing surface) along
  with opt-in speech playback (Voice & Subtitles "Speak interface
  announcements") and a Windows UIA notification bridge
  (`NativeAccessibilityBridge` — HWND subclass answers UiaRootObjectId
  and raises NotificationKind events); AT-SPI/non-Windows backends and
  semantic providers (focus traversal, control patterns) are still open
- `NativeMissionView` (missions/settlement panel) is a tested component
  not yet instantiated by the client
- Pause-menu/inspection-card focus rings are verified by client build +
  code inspection; the windowed campaign class has no headless
  event-loop harness
- `engine_shell_tool_*` smoke tests require a desktop display

## Conventions preserved

- No Godot/C#/Unity/Unreal reintroduction; Godot fixtures remain
  historical provenance only
- Determinism: campaign clocks authoritative; replay verifies per-section
  state hashes at save checkpoints
- Observer privacy: `feed()`/`query()` are the only visibility oracle;
  diplomatic audiences come from `known_to_civilization_ids`
- Save format: chronicle serializes through v17 (`"EventHistory"`,
  strict ordered decode, 1M bound); pre-chronicle saves load empty
