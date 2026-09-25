# Infrastructure Networks

Reusable single-resource distribution graphs: power grids, water
mains, data backbones, freight webs. Modules:
`engine/include/stellar/engine/flow_network.hpp`,
`engine/src/flow_network.cpp`. Tests: `flow_network` (ctest).

Status: IMPLEMENTED at engine level. Colony/Core adoption pending — a
settlement's aggregate utility pool (colony.hpp) is the degenerate
single-component case; `FlowNetwork` is the multi-site graph.

## Model

One `FlowNetwork` per resource. **Nodes** supply, demand and optionally
store the resource; **directed edges** carry `capacity_per_day`
(bidirectional links are two edges). Instance ids are caller-supplied.

Topology mutations (add/remove node or edge, enable/disable) mark the
component cache dirty; `components()` rebuilds lazily via union-find —
enabled nodes + enabled edges, deterministic ordering (ascending ids,
smaller-root merge, components ordered by first member). Rate/storage
changes do NOT dirty topology — the grid's shape is stable while loads
fluctuate, which is the common tick pattern.

## advance(elapsed_days)

Three deterministic passes (ascending-id order throughout, expected
values, no RNG):

1. **Local serve + storage release** — each node serves demand from own
   supply, then releases own storage into the remaining deficit.
2. **Greedy edge rebalancing** — edges scan ascending id; each moves
   `min(capacity × days, from-surplus, to-deficit)`. Single pass: flow
   does NOT chain through relays within one step (a documented
   limitation — multi-hop settling emerges over successive advances).
3. **Storage absorption + reporting** — leftover surplus fills node
   storage; unmet deficits are reported per node.

`FlowAdvanceResult` reports supply/demand/served/unmet totals, storage
released/absorbed, nonzero unmet per node and flow per edge. Per-node
`FlowNodeState` retains `last_served/unmet/imported/exported` for
queries and UI.

## Diagnostics

- `unmet_by_node` — where demand failed, and by how much.
- `flow_by_edge` vs `capacity_per_day` — saturation/congestion.
- `components()` — islanded demand visible as components with demand
  but no supply.

## Determinism & scale

Ascending-id passes, no ordering ambiguity (duplicate directed edges
rejected), bit-identical repeat runs asserted in tests. 10k nodes /
~20k edges advance in milliseconds — per-tick cost is O(nodes + edges),
not connectivity-dependent.

## Persistence

Nodes/edges are plain data with caller-supplied ids; serialize node
ids, rates, storage, enabled flags and the edge table. The component
cache is derived — never persisted.

## Remaining limitations

- Greedy single-pass transport, not optimal max-flow; transfers do not
  chain through intermediate nodes within one advance (documented;
  successive ticks settle multi-hop flow).
- Edge capacity is per-resource-per-day; no shared corridor capacity
  across resources, no latency/travel time on edges.
- No loss/efficiency factor on edges yet.
- Storage is per-node; no shared component-level reserves.
