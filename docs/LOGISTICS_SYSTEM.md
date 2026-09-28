# Solar Economy & Logistics System

## Purpose

Stellar Continuum models logistics as strategic physical support rather than as an invisible empire-wide bonus or one simulated object per cargo crate. The early-release implementation is deliberately bounded, deterministic, and reconstructible from authoritative campaign state.

The system currently answers three different questions at different levels:

1. **How much support does each colony require and produce locally?**
2. **How can represented local logistics nodes share bounded capacity?**
3. **Where does the empire have support requirements that no represented transport corridor can currently satisfy?**

These are related but must not be collapsed into one omniscient empire-wide pool.

## Colony logistics health

`IEconomyLogisticsView` exposes read-only colony and civilization summaries.

Each represented colony contributes:

- strategic support demand per day;
- local support capacity per day;
- imported support required per day;
- bounded local coverage ratio; and
- `Healthy`, `Strained`, or `Critical` condition.

The current formulas are prototype strategic abstractions over real represented colony population, infrastructure, stability, economy throughput, and completed infrastructure. They are not a final commodity model and do not create a second persistent economy.

## Represented home-system network

`IHomeSystemLogisticsNetworkView` reconstructs the civilization's represented home-system logistics graph.

Current node sources are strictly authoritative:

- owned home-system colonies -> `Homeworld` / `PlanetarySettlement` nodes;
- completed Orbital Launch Complex / Orbital Shipyard -> orbital logistics hub;
- completed Orbital Shipyard -> shipyard node.
- completed Asteroid Resource Network -> resource-site node connected to the orbital hub.

Local surplus becomes supply offers and local import requirement becomes prioritized demand. `LogisticsRoutePlanner` finds routes on demand using a bounded cache, and `LogisticsFlowAllocator` performs aggregate daily allocation while respecting source availability, route capacity, disabled links, and demand priority.

No network node may be created merely because a location would be plausible. If the simulation does not represent the settlement or facility, logistics must not invent it.

## Empire coverage and external colony systems

`ICivilizationLogisticsCoverageView` layers an empire-scale read model over the detailed home-system network.

Owned colony systems outside the home system are summarized from authoritative colony logistics state. For each external system the view reports:

- represented colony count;
- support demand and local capacity;
- local surplus;
- import requirement;
- worst local supply condition; and
- whether a represented interstellar freight corridor exists.

### No implicit interstellar freight

The current campaign model does **not** yet have one authoritative owner for interstellar freight corridors, transport capacity, route access, convoy assignment, blockade state, or treaty freight rights.

Therefore external systems default to:

`HasRepresentedInterstellarFreightCorridor = false`

and any imported support they require is exposed as `UnrepresentedInterstellarSupportPerDay`.

This is intentional. Owning two systems does not prove cargo can move between them. The logistics layer must not silently assume unlimited interstellar transport simply because fleets can travel or because both colonies share a civilization.

A future transport/freight owner can supply real cross-system corridors behind these read-model contracts. That owner should incorporate, as appropriate:

- actual transport/merchant capacity;
- travel time and range;
- infrastructure at both ends;
- fuel/maintenance/endurance;
- hostile interdiction or blockade;
- diplomatic access and treaty freight rights;
- route loss/risk; and
- throughput already committed to competing demands.

## Persistence boundary

Current logistics summaries, route caches, flow plans, home-system networks, and empire-coverage views are reconstructible derived state and are **not persisted**.

Do not introduce a competing save migration from the Solar Economy/Logistics workstream while another workstream owns the next shared save-schema change. Persist only genuinely authoritative logistics state once its owner and migration boundary are coordinated.

## Performance contract

- no all-pairs route precomputation;
- no unbounded route cache;
- no one-object-per-cargo simulation;
- no per-frame empire-wide logistics recomputation;
- aggregate flow is recalculated only when requested by strategic consumers/UI at bounded cadence;
- UI consumes read-only views and never becomes authoritative for supply or transport.

## AI and UI boundaries

Civilization AI may use logistics read models for its own legitimately known state. It must not infer rival supply networks from authoritative hidden campaign state.

Player UI may display the player's represented network and explicit support gaps, but should distinguish **unrepresented transport capacity** from confirmed delivery failure. A missing corridor in the current model means the game has no authoritative route to credit with delivery; it does not authorize the UI to fabricate one.
