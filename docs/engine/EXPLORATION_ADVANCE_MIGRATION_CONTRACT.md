# Exploration advancement boundary

Authority is the complete `Exploration/ExplorationSimulation.Advance` path in
the preserved C# game, together with `CivilizationOperatingCapacity`,
`FleetLocalTransit`, `InterstellarDistance`, `CivilianFleetReturnOrders`' inbound
activation hook, `SurveyOperationsProfiler`, and the existing knowledge API.
This gate advances already-created exploration fleets. It does not add a
campaign scheduler, colonization advancement, diplomacy policy, rewards, or a
save adapter.

## Owned state and API

The native operation borrows ordered, immutable systems, bodies,
civilizations, colonies, and economies; ordered mutable full `FleetState`
records; mutable `CivilizationKnowledgeState`; and a campaign-owned lane graph.
All spans and references are call-scoped owner-thread views. The operation
returns ordered `ExplorationEvent` values with the source's twelve enum values,
five required fields, and optional target-civilization/body identities. Events
are results, not a second knowledge store.

`advance_exploration(world, simulation_delta)` is the complete tick entry
point. The view cannot represent the source's null `GalaxyState`; that source
boundary remains an explicit oracle observation. Invalid nonfinite or negative
delta fails before reading world state. Zero returns an empty event list without
validating or mutating the borrowed world.

## Fleet order and operating capacity

Iterate active fleets in fleet insertion order. For each, resolve the first
civilization with the fleet owner ID before looking up funding. A missing owner
therefore throws even for an unfunded fleet, while mutations and events from
earlier fleets remain committed. Operating funding uses the first matching
economy, defaults to one when absent, clamps finite values to `[0,1]`, and maps
NaN/infinity to zero. Values at or below `1e-7` skip every action, including
refueling and holds.

At an occupied system, refuel before hold handling. Any owned Colony gives full
service; otherwise any owned ResourceOutpost gives half service. Refueling is
`Max(current, capacity*service)` and never lowers fuel. A held local fleet then
stops immediately. An InterstellarWarp hold continues only its current lane and
stops at the inbound gate.

## Survey and contact flow

An idle Scout or Science fleet with no destination tries local survey before AI
planning or movement. Scout work resets its remembered reconnaissance system
when needed, adds funded days capped at two, and consumes the step while still
working. At completion it records a `0.35` reconnaissance floor. A real
knowledge change emits `SystemReconnoitered`, then positive signature events for
matching bodies ordered by body ID: resource, anomaly, activity for each body.
If knowledge was already partial/full or the record operation makes no change,
survey work returns false and the fleet may continue through later flow.

Science work uses the reviewed per-system survey profile and adds
`ProgressPerDay * fundedDays`. No progress beyond `previous+1e-7` returns false.
The first partial transition emits `SystemSurveyStarted`, followed by the same
positive reconnaissance signatures only if the resulting level remains
partial. A direct jump to full omits transient signatures. Completion then
emits `SystemSurveyed`, followed by confirmed body events in body-ID order and
the source per-body order: anomaly, resource, native civilization.

Whenever local survey consumed the step, detect contacts afterward. Contact
checks civilizations in civilization insertion order, skips self/already-known,
and requires an owned colony or active owned fleet in the observer's current
system. Knowledge and `FirstContact` are one-way; passive foreign presence does
not gain reciprocal knowledge. The same contact check runs after inbound lane
arrival, after system/sensor events.

## AI and movement

An idle, destinationless, non-player Scout or Science fleet that did not consume
the step asks the reviewed exploration AI coordinator for a mission. A selected
candidate is assigned through the authoritative route-order helper, preserving
its revision, route, and local reroute semantics. No selection is a no-op.

A destinationless LocalArrival fleet advances only its inbound chart leg. On
completion it clears phase/origin/target/progress and zeroes local position,
then consumes the fleet's step. Other destinationless shapes do nothing.

Compatibility input with a destination, null current system, and phase None is
changed to InterstellarWarp and selects the first route waypoint or final
destination as transit target. Movement receives funded days and loops while a
destination remains and more than `1e-7` day remains:

1. Resolve the first system matching the first waypoint/final destination.
2. Phase None requires a current system, resolves its first match, snaps the
   strategic position to it, records transit endpoints, and begins local
   departure from the finite local position (or zero) toward the outbound gate.
3. Local legs consume chart time without fuel. Completed departure enters warp
   and clears current system. Completed intermediate arrival removes the
   reached waypoint and immediately enters the next warp leg. Completed final
   arrival clears transit state, snaps to the target, removes the waypoint, and
   clears destination only when neither hold nor return is pending. A held final
   arrival preserves its destination and stops.
4. Warp distance uses optional physical depth and the clamped persisted progress;
   a near-zero physical separation falls back to 2D chart distance. A legacy
   shape without a physical origin measures from its current chart position.
   Available distance is the source `Min(speed*days, fuel)`. Insufficient or
   zero fuel stops without inventing progress. Partial travel spends available
   fuel, updates the 2D chart position, and updates progress only with a usable
   physical origin/distance. Full lane travel deducts distance, time, and fuel,
   commits target position/current system, computes inbound and next/final local
   targets, and begins LocalArrival.

Inbound handling refuels first, records whether the target was already known,
reveals systems within the fleet sensor range, and emits `SystemDetected` for a
new target followed by `SensorContact` when at least one system was newly
revealed. It then detects civilization contacts. If return-to-base is pending,
it calls the real recovery activation helper and stops the movement loop even
when activation returns a rejection. This hook may clear paid colony work and
then fail during route mutation at the native revision-overflow boundary; the
partial mutation is authoritative and must be captured.

## Error and verification contract

First-match lookups and all mutations retain source order. Errors do not roll
back earlier fleet work or earlier mutations in the current lane: notably the
arrival position/current-system/fuel changes precede next-system lookup and
inbound processing, and inbound knowledge/events precede queued-return
activation. Native rejects unrepresentable signed revision overflow before UB,
with its documented partial state. State mutations made before an exception
remain, but the method's local event list is not returned and therefore no
events from the failed call are observable. Native `std::overflow_error` is an
explicit `OverflowError` boundary category. Physical nonfinite geometry follows
the already-reviewed `ArgumentException` import boundary rather than producing
undefined vector normalization.

The actual-C# oracle supplies complete before/after systems, bodies,
civilizations, colonies, economies, fleets, knowledge, ordered events, and exact
source errors. The consumer fully decodes and compares mutable fleet and
knowledge state, including preserved combat/loadout/vessel history. The other
collections are immutable spans in this API; the consumer decodes them for the
operation and verifies that the source oracle also kept their frozen input
representations unchanged. It covers zero/tiny/large and multi-leg steps, flat and mixed
depth, local/warp holds and resumed shapes, real queued-return inbound
activation, no/half/full refueling, absent/zero/fractional/nonfinite funding,
survey thresholds and direct completion, signature and confirmed-discovery
ordering, one-way first contact, duplicate first matches, and AI selection.
Fixture parsing and expected-result parsing occur outside operation catches;
only the simulation call is caught. Unrepresentable null records are reported
separately from native executions. Release and Debug standalone consumers build
with MSVC `/W4 /WX`.
