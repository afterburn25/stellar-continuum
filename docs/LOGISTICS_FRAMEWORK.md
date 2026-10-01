# Logistics Framework

Strategic freight: cargo moving between settlements/stations over
multi-leg routes with transit time and route capacity. Modules:
`engine/include/stellar/engine/logistics.hpp`,
`engine/src/logistics.cpp`. Tests: `logistics` (ctest).

Status: IMPLEMENTED at engine level. Core lane/freight adoption
pending — the authoritative Core economy/lane planner remains the
routing authority; this framework is the deterministic shipment layer
those routes feed.

## Model

- **Nodes** — caller-owned waypoint ids (colonies, stations, depots).
  Registered so routes validate; a node referenced by a route cannot be
  removed.
- **`FreightRoute`** — explicit path + per-leg transit days +
  `capacity` (max in-flight quantity). Explicit paths, not pathfinding:
  the caller's routing engine (Core lanes/reach) owns path choice.
- **`Shipment`** — one convoy: resource, quantity, caller id, stamped
  `departed`/`eta`. Etas are fixed at departure and never drift.

Cargo accounting stays with the owner: `dispatch` assumes the origin
already paid the cargo; `advance` returns `FreightDelivery` records the
owner deposits. The network never touches inventories itself.

## advance(elapsed_days)

Two phases, both deterministic:

1. **Departures** — queued shipments scan in ascending id and depart
   where route capacity allows, stamped at the START of the step (a
   shipment travels during the step it departs; zero-length routes
   deliver the same step). Disabled routes hold their queue; in-flight
   shipments always continue.
2. **Arrivals** — shipments with `eta <= now` deliver in (eta,
   shipment id) order; route `in_flight` decrements.

Capacity is checked against in-flight quantity at departure time —
freed capacity is reusable the next step. `cancel()` works only on
queued shipments; in-flight cargo is committed. `remove_route` drops
queued shipments and loses in-flight cargo (inspect `in_transit()`
first).

## Diagnostics

- `queued()` / `in_transit()` — backlog and en-route manifests.
- `route_utilization()` — in-flight ÷ capacity per route.
- `shipment_progress(id)` — 0..1 transit fraction for map display.

## Determinism & scale

Ascending-id dispatch and (eta, id) delivery order, expected-value
transit, caller-supplied ids — bit-identical repeat runs asserted in
tests. 20k shipments across 499 routes advance in milliseconds.
Elapsed-time stepping composes with `SimulationExecutor` dormant
tiers: a quiet trade lane integrates weeks in one call.

## Persistence

Routes, queue and in-flight shipments are plain data with caller ids;
serialize `routes()`, `queued()`, `in_transit()` and `now()`. No hidden
state.

## Remaining limitations

- Routes are explicit paths — no pathfinding, rerouting or waypoint
  failure handling (caller supplies new routes).
- No per-leg position tracking beyond aggregate progress fraction.
- Route capacity is total in-flight quantity — no convoy-count,
  ship-class or per-leg capacity modeling.
- No interdiction/piracy hooks — route interruption is caller-driven
  (disable route, remove route, or cancel before departure).
- Cargo loss on `remove_route` is silent beyond the in-transit listing.
