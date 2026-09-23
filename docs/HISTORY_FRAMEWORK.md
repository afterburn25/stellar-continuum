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

`events_` deque + `next_id_` are plain data — serialize records in id
order, restore by appending.

## Remaining limitations

- `summary` is an opaque string — localization-key + argument binding
  (structured event text) is a future adapter to LocalizationService.
- No spatial/body indexing beyond a single `location` id; rich
  per-actor timelines come from `actor` queries, not dedicated indices.
- No automatic recording — systems must call `record()` (integration
  points are part of Core adoption).
- Observer model is allow-list only — no "known after delay" or
  intelligence-quality degradation yet.
