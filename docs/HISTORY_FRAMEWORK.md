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

The runtime also applies the chronicle retention policy
(`maintain_chronicle` in the adapter): once the history reaches 90% of
capacity, routine records (`significance < 0.35`, the news-report
floor) older than 365 campaign days are pruned via `prune_before`.
Without it the bounded capacity eviction pops the oldest record
regardless of significance — in a long war, high-volume trivia
(damage ticks, detections) would crowd out major history. Majors and
anything within the horizon survive; the policy is deterministic given
the same contents. Coverage: `campaign_event_history` retention tests.

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
Located seeded reports carry their `system_id` into the feed, so the
panel renders a VIEW SYSTEM action (`OpenSystem` → `enter_system`);
the coalesced transient feedback path deliberately stays id-free.
Coverage: `native_notification_events` tests (chronicle ordering,
observer filtering, bound, category labels, system propagation, empty
history).

For history beyond the transient window, `native_chronicle` adds the
scrollable chronicle browser: `snapshot()` projects
`feed(observer, -inf, min_significance)` into display entries (newest
first, capped at 4000 with the true total reported) and
`NativeChronicleView` renders them as a scrollable overlay — opened
from a CHRONICLE button in the notification panel header, refreshed on
demand while open, closed on every session/modal transition alongside
the notification view, and filterable by category domain via
`snapshot()`'s `category_prefix`, a significance floor that cycles
0.0 → 0.3 → 0.5 → 0.7, and an actor filter cycling all intel → the
observer (MINE) → each other civilization appearing in the visible
feed (`actors` exact-match; civ names resolve through an injected
resolver, falling back to "CIV <id>"), and a recency window cycling
all → last 30d → last year → last decade (the feed's own `since_day`
bound, driven by a live campaign-day source so it stays correct while
the browser stays open) — all filters apply
before the cap, so a filtered view still reaches deep history. Entries with a `location` are clickable:
the view returns the system id through `navigation()` (same drain
contract as the debug background), the client closes the overlay and
calls `enter_system` — the workspace's own observation check still
gates what the observer actually sees there. Entries with exactly one
foreign actor additionally render a DIP action: `contact_navigation()`
drains the civilization id and the client opens the diplomacy
workspace on that contact (first contacts, battles, treaties become
one-click relations). Recorded reference tags render as clickable
chips on each card: clicking one sets `snapshot()`'s `tag` filter —
HistoryQuery::tag's exact-match semantics — so "everything fleet:12
did that we can see" is one click away; re-clicking the focused chip
or the X focus button in the intro row clears it. Coverage:
`native_chronicle` tests (snapshot ordering, observer privacy, cap +
total, domain, significance, actor, tag and recency filtering, entry
and contact navigation, view lifecycle, refresh, render smoke).

The admission seeding in `native_notification_events` applies a fixed
0.35 report floor via `feed()`'s `min_significance` — the category
vocabulary assigns high-volume trivia (war.damage_applied 0.1,
signature/system detections <=0.3, survey_started 0.2) below it, so
the transient feed surfaces reports, not noise. Seeded items carry the
same navigation affordances as the browser: located reports get VIEW
SYSTEM, and reports with exactly one foreign actor get OPEN RELATIONS
(the diplomacy workspace's identification check still gates what an
unidentified contact shows).

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
  filters by whole category domains, a coarse significance floor
  (0.0/0.3/0.5/0.7) and a per-civilization actor cycle — continuous
  floors and tag filtering (`query()`'s last unused axis) stay unused
  at the presentation layer.
