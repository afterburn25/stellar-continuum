# Event History Framework

The strategic chronicle: significant simulation happenings recorded as
queryable data with observer filtering. Modules:
`engine/include/stellar/engine/history.hpp`,
`engine/src/history.cpp`. Tests: `history` (ctest).

Status: IMPLEMENTED at engine level. Complements `EventBus` (transient
typed dispatch) and `MissionRuntime` (scripted missions) — this is the
authoritative record those surfaces feed.

## Model

`HistoryEvent` — `at_day` (campaign clock), `category`
(`"colony.founded"`, `"war.battle"`, ...), `summary` (display text or
localization key), `actors`, optional `location`, `significance`
(0..1), `visible_to` observer list, free-form `tags`.

Privacy model: `visible_to` empty = public knowledge; non-empty =
visible only to listed factions in filtered queries. The record itself
keeps full truth — filtering is a query-time projection, so the same
history serves omniscient developer tools and fog-of-war-correct
player chronicles.

## Queries

`query(HistoryQuery)` — category/tag/actor/time-range/significance
filters + optional observer + `limit` (most recent N, still
time-ordered). Results always sort (at_day, id) — record order may
interleave days.

`feed(observer, since_day, min_significance)` — the M15 news substrate:
significant, observer-visible events since a day. Presentation
(voice/UI) stays app-level.

## Capacity + pruning

Bounded (`capacity` drops oldest records) plus explicit
`prune_before(day, keep_significance)` — low-significance old events
prune, majors persist. `event(id)` is a binary search; ids are
monotonic so lookups survive pruning.

## Determinism & scale

Append-only monotonic ids, no RNG, queries sort explicitly — bit-equal
histories asserted. 200k events + full scan query in ~20ms.

## Persistence

Versioned `State` (`capture_state`/`restore_state`) carries retained
records plus `next_id`; `capacity` is constructor policy. Restore
rejects non-ascending event ids and `next_id` collisions — record
order and binary lookup depend on them. JSON codec in
`framework_state_json.hpp`.

## Core consumer

`core/campaign_event_history` maps each authoritative
`IntegratedAdaptiveCampaignStepResult` onto records:

- categories: `construction.project`, `shipbuilding.ship`,
  `research.legacy`, `research.adaptive`, `exploration.<type>`,
  `war.<type>`, `colony.founded`
- actors = involved civilization ids; fleet/system/body/project/design/
  tech/colony references preserved as tags; `location` = system id
- `at_day` = the step's absolute end day
- visibility = involved civilizations only (fog-of-war safe)

`IntegratedAdaptiveCampaignRuntime` owns an `EventHistory` and records
every completed advance — the chronicle is populated automatically for
all callers (`runtime().history()` / `frame().history()`). Aggregate
phase counters (sensor-contact recordings, diplomacy maintenance) are
not discrete happenings and are not recorded.

## Save integration

The chronicle is authoritative state: it serializes into the v17
player/developer save payload as `"EventHistory"` (PascalCase fields,
strict ordered decode, 1M-event bound). Saves written before the
chronicle existed load with an empty history; corrupt states (non-
ascending ids, `next_id` collisions, unsupported version) are rejected
with `PlayerCampaignPersistenceDataError`.

## Remaining limitations

- `summary` is an opaque string — localization-key + argument binding
  (structured event text) is a future adapter to LocalizationService.
- No spatial/body indexing beyond a single `location` id; rich
  per-actor timelines come from `actor` queries, not dedicated indices.
- Visibility is involved-party allow-list only — widening to observers
  that know the location (or delayed/degraded intel) is knowledge-layer
  work, not assumed here.
- News/voice presentation on top of `feed()` remains app-level work.
