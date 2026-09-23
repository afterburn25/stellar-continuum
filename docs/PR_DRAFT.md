# PR draft — engine/space-strategy-simulation-specialization → work/foundation-1-30-codex-integration

`gh` CLI is unavailable in the development environment; this file is the
review-ready PR description for manual creation. Delete when the PR is
opened and this text is pasted in.

---

## Title

Space strategy engine specialization: M1–M15 frameworks, Core adoption, chronicle, editor tools

## Summary

Specializes the Stellar Engine into a reusable, deterministic C++23
space-strategy/simulation engine (78 commits, ~19.6k insertions, 121
files). Not an Unreal clone — the goal is a strategy/simulation
substrate: scheduling, economy, population, colonies, infrastructure,
logistics, planetary development, terraforming, strategic AI, warfare,
event history, and editor tooling — all deterministic, persistable,
tested, and consumed by real Core/App surfaces.

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
| 11 | Space rendering | **PARTIAL** — scene3d GPU path with planet/ring/star materials; instancing, HDR/tonemap, TextureStreamer residency pending (needs a GPU-equipped environment) |
| 12 | Editor tools | **PARTIAL** — 15 shell tools (Projects…Physics incl. Simulation/Colony/Economy/Planet/AI/Warfare/Missions/Physics genre inspectors) + per-tool `--frames` ctest smoke; generated projects scaffold executor + persistence; galaxy debugger pending |
| 13 | Scale benchmarks | IMPLEMENTED — `simulation_scale_250…5000`, `combined_scale` (400 settlements, ~870µs/tick, bit-identical) |
| 14 | Event history | IMPLEMENTED — `EventHistory` observer-private chronicle; runtime records every advance incl. diplomatic journal entries; v17 save payload; retention policy |
| 15 | Public-information hooks | IMPLEMENTED — `feed()` substrate; notification seeding + scrollable chronicle browser with every query axis exposed (domain/significance/actor/tag/search/time-window paging); map + diplomacy navigation; voice announcements |

## Verification

- `2026-09-24-development-sync` receipt: **294/294** native tests green
- `2026-09-23-chronicle-suite` receipt: **295/295** runnable tests green
  at `d68c98af` (15 `engine_shell_tool_*` tests need a display)
- User desktop run (`ba7fbf05`): **310/310** including shell smoke tests
- New this branch: `replay` unit tests for per-section checkpoint
  divergence localization
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
  name the diverging subsystem (`save:World.Fleets`)
- `route_unreachable`/`power_brownout`/`sustenance_shortfall`/
  `foreign_armed_presence` operational diagnostics over authoritative
  reach/economy/warfare projections
- `IntegratedAdaptiveCampaignRuntime` records chronicle entries every
  advance; `DiplomacyState` read-only journal accessors

## Limitations (honest)

- M11 space rendering is PARTIAL: no render-graph backend consumption,
  instancing, or HDR path — requires a GPU-equipped environment to
  develop and verify
- Authoritative Core adoption of economy-catalog/colony/logistics/
  population/strategic-AI frameworks pending — current consumption is
  intentionally read-only projection to avoid dual authority
- `physics`/`mission_graph` have shell-tool consumers only; no game
  path consumes them yet
- Galaxy debugger pending; engine has no galaxy model (Core owns
  astronomy authority — read-only projection under way)
- Chronicle browser: multi-select filter composition unexposed;
  `HistoryEvent::summary` is opaque text (structured localization is an
  upstream event-pipeline change)
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
