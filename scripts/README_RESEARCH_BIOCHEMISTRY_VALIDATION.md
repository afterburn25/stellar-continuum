# Alternative Biochemistry Validation

Milestone #9 adds two research-owned validation gates alongside the existing Adaptive Research stack.

## Structural validator

```text
python3 scripts/validate_research_biochemistry.py data/research/v1
```

Checks include:

- 360 public nodes / 21 domains;
- 30 nodes in `alternative_biochemistry`;
- 16 alternative-solution sets;
- 14 applicability traits;
- 36 knowledge fields including `biochemistry`;
- global Research Pressure catalog remains 59;
- biochemical applicability traits are composable and not race IDs;
- ammonia-specific nodes require `ammonia_rich_biology`;
- cryogenic hydrocarbon nodes require both `hydrocarbon_solvent_biology` and `cryogenic_biology`;
- silicon-specific nodes require `silicon_centered_biochemistry`;
- mineral-structural nodes require `mineral_structural_biology`;
- comparative cross-biochemistry nodes require legitimate `alien_biology` evidence;
- human-like reference is explicitly carbon-centered + water-solvent;
- ammonia/cryogenic/silicon reference-start modules are present;
- modular starting-profile counts are 15 fragments / 7 profiles;
- biochemical specialist facilities and stage requirements reference real nodes/capabilities/providers.

## Shared-graph applicability benchmark

```text
python3 scripts/validate_research_biochemistry_benchmarks.py data/research/v1
```

Checks that human carbon-water, ammonia-rich, cryogenic hydrocarbon, and silicon/mineral reference populations use the **same 360-node graph** while receiving different biochemical-specific applicability.

Assertions include:

- human reference has zero exotic native-specific applicable nodes;
- each exotic reference has at least five compatible native-specific nodes;
- incompatible biochemical families remain inapplicable;
- pairwise exotic-specific applicability Jaccard distance is at least 0.80;
- cross-biochemistry comparative nodes remain evidence-gated;
- shared metabolic biochemical foundation nodes remain available across metabolic profiles.

## CI

Two separate research-owned workflows run these gates:

- `.github/workflows/research-biochemistry.yml`
- `.github/workflows/research-biochemistry-benchmark.yml`

The standard build workflow continues to run the existing research validators/long-run benchmark plus .NET/shared Godot process steps. This avoids overwriting shared workflow content while multiple development chats are active.

The shared Godot runtime-smoke semantic limitation remains tracked in issue #61 and is not owned by Adaptive Research.
