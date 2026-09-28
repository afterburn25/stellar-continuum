# Adaptive Research Benchmark Baseline

This records the first passing deterministic offline benchmark results for the public Adaptive Research graph. It is a regression reference, **not final game balance**.

Source CI run: `34163593221`  
Benchmark step: `python3 scripts/validate_research_benchmarks.py data/research/v1`

## 500-year same-origin divergence

All three civilizations began from `reference_humanlike_solar_2050` and the same 330-node public catalog.

Observed benchmark result:

- minimum pairwise Mature-tree Jaccard distance: **0.457**
- common Mature nodes across all three: **47**
- Mature fraction of public catalog:
  - Orbital Industrialist: **0.276**
  - Defense Engineer: **0.318**
  - Biosphere Adaptor: **0.382**
- Mature technologies unique against both other civilizations:
  - Orbital Industrialist: **11**
  - Defense Engineer: **36**
  - Biosphere Adaptor: **68**

The benchmark threshold is intentionally looser (minimum Jaccard distance 0.22 and at least 8 unique Mature nodes each) so ordinary catalog evolution does not create brittle exact-output tests.

## 350-year military complacency / response

Observed benchmark result:

- initial hegemon military-capability lead: **10.85 benchmark score units**
- gap at year 180: **-12.65** (challenger had leapfrogged before the leader's later response)
- leader military attention during low-threat period: **1.0**
- later leader military attention after legitimate catch-up observation: **3.0**
- final benchmark military score:
  - Established Hegemon: **18.55**
  - Rising Challenger: **33.40**

Interpretation: the scenario demonstrates that a real lead can be squandered, a challenger can leapfrog without a hidden catch-up multiplier, and the former leader can recognize the changed situation and raise research attention without being guaranteed to recover.

The benchmark does **not** assert forced parity or a guaranteed leader comeback.

## Foreign technology asymmetric value

Observed benchmark result:

- synthetic holder has no metabolic-biology compatibility
- metabolic buyer does
- transfer package contains 3 meaningful components
- brokerage value remains possible even when holder operability is poor
- transfer does not directly set native technology Mature

## 1,000-year research-state soak

Observed final records:

### Soak Generalist A

- node states: **89**
- Mature nodes: **89**
- recent event records: **124**
- detailed history records: **64**

### Soak Generalist B

- node states: **105**
- Mature nodes: **105**
- recent event records: **156**
- detailed history records: **80**

### Soak Specialist C

- node states: **81**
- Mature nodes: **81**
- recent event records: **108**
- detailed history records: **56**

All remained well below the structural bounds of 330 node-state records, 256 recent events, and 512 detailed history records per civilization.

## How to use this baseline

Future research changes should be investigated when they materially change these patterns.

Do **not** require exact numerical reproduction forever. Good reasons for change include improved research costs, better emergence logic, additional public nodes, or better benchmark modeling.

Bad reasons include accidental convergence toward one universal tree, removing causal Research Pressure behavior, introducing hidden catch-up mechanics, or allowing unbounded per-civilization state/history growth.
