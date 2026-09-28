# Adaptive Research Milestone #9 — Candidate State

This branch-local record preserves the current Alternative Biochemistry / Exotic Biospheres expansion until it is validated and merged. Once accepted, canonical project state/history should absorb this information.

## Scope

Milestone #9 expands biological diversity **inside the shared hidden Technology Possibility Graph**. It does not create named-race trees.

Current candidate counts:

- **360 normal/public nodes** (was 330)
- **21 domains** (was 20)
- **59 Research Pressure types** (unchanged)
- **16 alternative-solution sets** (was 15)
- **14 applicability traits** (was 6)
- **9 evidence types** (unchanged)
- **36 knowledge fields** (was 35; adds `biochemistry`)
- **15 modular starting-history fragments**
- **7 modular reference starting compositions**

## New biochemical applicability traits

- `carbon_centered_biochemistry`
- `water_solvent_biology`
- `ammonia_rich_biology`
- `hydrocarbon_solvent_biology`
- `silicon_centered_biochemistry`
- `cryogenic_biology`
- `mineral_structural_biology`
- `liquid_medium_native`

They are composable population/species facts, not race IDs.

Carbon-water is expected to be common but not universal. Silicon-centered life is explicitly speculative/rare and does not automatically imply stone bodies, radiation immunity, high-temperature superiority, etc.

## New domain

`alternative_biochemistry` — **Alternative Biochemistry & Exotic Biospheres** — 30 public nodes.

Breakdown:

- 4 shared alternative-biochemistry foundation nodes
- 6 ammonia-rich native-specific nodes
- 6 cryogenic hydrocarbon native-specific nodes
- 8 silicon/mineral-structural native-specific nodes
- 6 cross-biochemistry comparative/interface nodes requiring legitimate `alien_biology` evidence

## Research Pressure strategy

Global pressure count remains 59.

The biochemical model reuses existing causal pressures such as food/water/resource shortages, life-support losses, extreme temperature, colony isolation, ecological damage, disease, industrial bottlenecks, alien biology, and foreign technology observations.

Advanced native/cross-biochemistry nodes receive **selective explicit hard pressure gates** in `research_economy.json` when a real need is appropriate. Foundational solvent/substrate science remains possible through basic science.

## Cross-lineage capabilities

Existing general capabilities now have additional implementations:

- `long_duration_habitation`
- `food_independence`
- `biosphere_independence`

New registered functional capabilities:

- `multibiochemistry_habitation_support`
- `cross_biochemistry_medical_support`
- `cross_biochemistry_biofabrication`

This lets other systems consume the function without depending on the exact source technology.

## Modular starting histories

New fragments:

- `history_ammonia_rich_biochemistry`
- `history_cryogenic_hydrocarbon_biochemistry`
- `history_silicon_centered_biochemistry`

New reference profiles:

- `reference_ammonia_rich_early_space`
- `reference_cryogenic_hydrocarbon_early_space`
- `reference_silicon_centered_early_space`

The human-like reference is now explicitly carbon-centered + water-solvent rather than leaving human biology as an unmarked universal default.

`starting_profile_index.json` now loads starting history/profile catalogs modularly and records 15 fragments / 7 profiles.

`validate_research_start_runtime.py` has been generalized to validate all indexed fragment/profile files rather than two hardcoded files.

## Research facilities

New modular research-facing extension:

- Alternative Biochemistry Institute
- Cryogenic Biochemistry Institute
- Mineral Biochemistry Institute
- Cross-Biochemistry Interface Laboratory

`research_facility_index.json` lists base + extension facility catalogs.

Hard biochemical stage requirements reference real experiment/containment/prototyping capabilities rather than percent bonuses.

## New validation

Structural:

```text
python3 scripts/validate_research_biochemistry.py data/research/v1
```

Shared-graph applicability benchmark:

```text
python3 scripts/validate_research_biochemistry_benchmarks.py data/research/v1
```

Research-owned workflows:

- `.github/workflows/research-biochemistry.yml`
- `.github/workflows/research-biochemistry-benchmark.yml`

The normal research/build workflow continues to run the preexisting full research validator stack, long-run benchmark, .NET build, and shared Godot process smokes.

Shared Godot runtime smoke semantic limitation remains issue #61.

## Biochemical benchmark expectations

Within the 30-node biochemical domain without alien evidence:

- human carbon-water reference: 4 shared foundations, **0 exotic native-specific nodes**
- ammonia-rich reference: 4 shared foundations + **6 ammonia-specific nodes**
- cryogenic hydrocarbon reference: 4 shared foundations + **6 cryogenic-hydrocarbon nodes**
- silicon/mineral reference: 4 shared foundations + **8 silicon/mineral nodes**
- pairwise exotic-specific applicability Jaccard distance expected **1.00** at this seed stage (assertion >= 0.80)
- all 6 cross-biochemistry comparative nodes remain evidence-gated

This tests initial applicability separation, not permanent technological isolation; later cross-biochemistry science, hybrid technologies, multispecies civilization, adaptation, and foreign technology can create overlap.

## Remaining acceptance steps

Before merge:

1. all preexisting research validators remain green on 360-node graph
2. long-run 500/1000-year benchmark remains green (historical 330-node baseline may be superseded by a new 360-node baseline)
3. biochemical structural validator green
4. biochemical shared-graph benchmark green
5. .NET build green
6. changed-file audit remains research/continuity/validation only
7. shared Godot process steps reported with issue #61 caveat, not treated as proof of semantic runtime health
8. update canonical `PROJECT_STATE.md`, `DEVELOPMENT_HISTORY.md`, handoff, and validation README with accepted counts/merge after validation
9. merge research milestone; do not promote gameplay VERSION

## Likely next research milestone after acceptance

Discovery-path diversity and FTL accessibility:

- distinguish native discoverability from universal physical possibility
- allow some civilizations to reach interstellar capability through different routes
- allow some civilizations to fail to discover FTL independently without declaring them permanently game-scripted incapable
- foreign observation/captured science can prove otherwise unknown physical possibilities
- preserve fair-information and adaptive-tree rules

That milestone should address the design requirement that some species/civilizations may never achieve warp on their own while avoiding named-race hardcoded trees.
