# Adaptive Research Long-Horizon Benchmarks

Stellar Continuum's Adaptive Research architecture is intended to support campaigns lasting centuries or millennia without forcing every civilization toward the same finished technology tree or allowing research state to grow without bound.

This benchmark layer tests those claims **offline** before the full gameplay runtime is integrated.

## Important boundary

`validate_research_benchmarks.py` is a deterministic engineering/design harness.

It is **not** the gameplay research runtime.

Because it runs offline in CI against only 330 public nodes, it is allowed to scan the public possibility graph directly in order to test structural behavior cheaply.

The actual game remains bound by `research_runtime_contract.json`:

- event/index-driven candidate updates;
- sparse per-civilization state;
- no full-graph scan every simulation tick;
- no per-frame hidden-graph UI work.

Benchmark success therefore validates design properties, not the runtime implementation algorithm.

## What the reference harness uses

The harness consumes real public research data:

- starting reference profiles/history fragments;
- node prerequisites;
- applicability traits;
- cross-lineage capabilities;
- explicit Research Pressure gates;
- complexity/depth RP costs;
- minimum/recommended labs;
- directed-program progression;
- public node domains/pressure affinities;
- selected benchmark culture/agenda inputs.

It deliberately does not invent alien/anomaly evidence for ordinary native progression.

## Scenario 1 — Same origin, divergent 500-year histories

Three civilizations begin from the same human-like 2050 reference scientific history.

### Orbital Industrialist

Sustained problems/opportunities emphasize:

- launch cost;
- orbital growth;
- asteroid resources;
- industrial bottlenecks;
- supply-line reach.

Research attention favors Space Industry, Logistics, Materials, Energy, and Research Infrastructure.

### Biosphere Adaptor

Conditions emphasize:

- food/water security;
- low-gravity health;
- gravity divergence;
- colony isolation.

Research attention favors Life/Medicine, Biosphere Agriculture, Biotechnology, Planetary Engineering, and Cybernetics.

### Defense Engineer

Conditions emphasize:

- missile threats;
- fleet losses;
- ineffective weapons;
- stealth threats;
- enemy mobility.

Research attention favors Military Engineering, Sensors/Communications, Computing, Materials, and Propulsion.

### Assertions

After 500 years the benchmark requires:

- minimum pairwise Mature-tree Jaccard distance of 0.22;
- at least 8 Mature technologies unique to each civilization relative to the other two;
- no civilization Mature in more than 70% of the public catalog;
- all still use the exact same static possibility graph.

This proves divergence can arise from history/need/attention rather than separate species trees.

## Scenario 2 — Military complacency and renewed response

An Established Hegemon begins with a benchmark-only military technology lead and a culture characterized by high complacency/institutional conservatism and relatively low threat sensitivity.

A Rising Challenger begins behind but experiences serious actual military pressure.

The leader receives only scheduled legitimate peer observations. It does not know the challenger's exact hidden research state.

The benchmark requires:

- the challenger's simulated military capability gap narrows before the leader's renewed response;
- the leader's military research attention falls during the low-threat/adequacy period;
- the leader's attention rises after a sufficiently credible legitimate observation shows the challenger closing the gap;
- no hidden catch-up multiplier;
- no hidden leader penalty;
- no forced parity.

The test does **not** require the challenger to win or reach equality. Technological gaps are allowed to remain, widen, narrow, or reverse based on the scenario.

## Scenario 3 — Foreign technology has asymmetric value

A synthetic Machine Broker possesses a foreign biological industrial technology fixture.

The technology is directly unusable to the holder because of biological/process dependencies, but the package contains theory, blueprints, and hardware.

A metabolic buyer is biologically more compatible.

The benchmark asserts:

- direct holder operability can be worse than expected buyer operability;
- the holder can still derive research or brokerage value;
- transfer does not make a native technology instantly Mature;
- technology value is buyer-specific rather than universal.

This protects the alien-technology trade design from collapsing back into generic RP/items.

## Scenario 4 — 1,000-year state soak

Three research histories run for 1,000 benchmark years.

The benchmark asserts hard structural bounds:

- no more than 330 node-state records per civilization (the public catalog size);
- recent event buffer capped at 256;
- detailed research-history records capped at 512 before compression/summary;
- zero per-civilization copies of the static catalog;
- zero persisted copies of reconstructible research indexes;
- UI layout cache not serialized in saves;
- the full 1,000-year run completes without unbounded record growth.

These are architectural ceilings/guardrails, not a claim the final game will store exactly those specific counts.

## Reference simulation simplifications

The offline harness intentionally simplifies several things:

- one-year reference steps rather than production simulation tick frequencies;
- approximate agenda scoring for benchmark selection;
- simplified lab allocation/readiness;
- scheduled synthetic legitimate peer observations for the complacency scenario;
- no full diplomacy/economy/population simulation;
- no secret technologies/content.

A benchmark is useful only if we remember what it proves.

It proves that the **public research graph and contracts are capable of the intended behavior under deterministic causal conditions**. It does not prove final Early Access balance.

## CI role

Research CI runs:

```text
python3 scripts/validate_research_benchmarks.py data/research/v1
```

before .NET/Godot validation.

If future graph changes make the benchmark fail, we should investigate the reason rather than simply lowering thresholds. Sometimes a threshold should change because the model genuinely evolved, but the change should be explicit and justified.

## Canonical files

- `data/research/v1/research_benchmark_scenarios.json`
- `scripts/validate_research_benchmarks.py`
- `docs/RESEARCH_BENCHMARK_MODEL.md`
