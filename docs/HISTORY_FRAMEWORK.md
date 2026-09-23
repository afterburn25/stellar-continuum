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
- visibility = involved civilizations, widened by
  `widen_history_visibility` to every civilization that knows the
  event's system (`CivilizationKnowledgeState::is_system_known`) —
  major happenings in known space propagate as news; locationless
  events (research/construction/shipbuilding) stay involved-party-only

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

## Presentation consumer (milestone 15)

The player notification panel is a bounded 32-item transient feed —
before this integration it cleared on load, so the persisted chronicle
was unreachable in-game. `seed_chronicle_notifications`
(`native_notification_events`) now runs at campaign admission: it pulls
`history().feed(player_civilization, -inf)` and publishes the most
recent 16 entries with their recorded campaign dates and summaries,
mapping history categories onto the feed's existing display vocabulary
(Construction/Ships/Research/Exploration/Colony/Combat; unmapped
categories keep their stable raw id). Observer privacy is the
chronicle's own projection — the publisher never re-derives visibility.
Coverage: `native_notification_events` tests (chronicle ordering,
observer filtering, bound, category labels, empty history).

For history beyond the transient window, `native_chronicle` adds the
scrollable chronicle browser: `snapshot()` projects
`feed(observer, -inf)` into display entries (newest first, capped at
4000 with the true total reported) and `NativeChronicleView` renders
them as a scrollable overlay — opened from a CHRONICLE button in the
notification panel header, refreshed on demand while open, closed on
every session/modal transition alongside the notification view, and
filterable by category domain via `snapshot()`'s `category_prefix`
(the cap applies after filtering, so a domain view still reaches deep
history). Coverage: `native_chronicle` tests (snapshot ordering,
observer privacy, cap + total, domain filtering, view lifecycle,
refresh, render smoke).

Voice presentation is the pre-existing `NativeGameplayVoiceBridge`:
`route_events` announces every significant chronicle category per
advance (research, construction, shipbuilding, exploration including
first contact, colonization, combat) with authoritative names,
observer-safe payloads and first-occurrence tracking — chronicle
recording and voice announce the same authoritative step events.

## Remaining limitations

- `summary` is an opaque string — localization-key + argument binding
  (structured event text) is a future adapter to LocalizationService.
- No spatial/body indexing beyond a single `location` id; rich
  per-actor timelines come from `actor` queries, not dedicated indices.
- Visibility widens at known-system granularity — a civ that merely
  detected a system sees its major events; delayed intel, survey-level
  gating and sensor-quality degradation are future refinements.
- The chronicle browser snapshots the newest 4000 visible entries and
  filters by whole category domains — per-significance, per-actor and
  tag filtering (`query()`'s other axes) remain unused at the
  presentation layer.
