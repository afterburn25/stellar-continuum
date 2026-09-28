# Alternative Biochemistry Shared-Graph Benchmark

Milestone #9 adds a focused benchmark to prove that biochemical diversity comes from **applicability within one shared Technology Possibility Graph**, not from separate race trees.

## Reference populations

The benchmark composes traits from the modular starting-history profiles and evaluates the 30-node `alternative_biochemistry` domain without granting alien evidence.

### Human-like carbon-water reference

Expected biochemical context includes:

- metabolic biology
- carbon-centered biochemistry
- water-solvent biology

Expected in the new domain:

- the 4 shared alternative-biochemistry foundation nodes can be applicable;
- **0 exotic native-specific ammonia / cryogenic-hydrocarbon / silicon / mineral nodes** are applicable;
- comparative cross-biochemistry nodes remain unavailable without alien biological evidence.

### Ammonia-rich reference

Expected context includes:

- metabolic biology
- carbon-centered biochemistry
- ammonia-rich solvent biology

Expected native-specific applicability:

- 6 ammonia-specific nodes across ammonia biology, habitation, bioindustry, and biosphere families.

Cryogenic-hydrocarbon and silicon/mineral native families must remain inapplicable.

### Cryogenic hydrocarbon reference

Expected context includes:

- metabolic biology
- carbon-centered biochemistry
- hydrocarbon-solvent biology
- cryogenic active biology

Expected native-specific applicability:

- 6 cryogenic-hydrocarbon nodes across biological, habitat/ecology, and bioindustrial families.

Ammonia and silicon/mineral native families must remain inapplicable.

### Silicon-centered / mineral-structural reference

Expected context includes:

- metabolic biology
- silicon-centered biochemistry
- mineral-structural biology

Expected native-specific applicability:

- 8 silicon/mineral-specific nodes covering biochemical frameworks, metabolism, nutrient processing, repair/regeneration, biofabrication, life support, and biological computation.

Ammonia and cryogenic-hydrocarbon native families must remain inapplicable unless the population independently also carries the relevant solvent traits.

## Divergence assertion

For the three current exotic reference contexts, native-specific biochemical sets are intentionally disjoint at this first seed stage.

The benchmark requires pairwise native-specific applicability Jaccard distance >= **0.80**. The current expected seed result is **1.00** for each exotic pair.

This does **not** imply civilizations can never converge technologically. Shared sciences, cross-biochemistry research, foreign technology, genetic/biological adaptation, mixed populations, and hybrid lineages can create overlap later.

The test only protects the initial rule that genuinely incompatible biochemistry does not secretly start with the same specialized biological technologies.

## Cross-biochemistry evidence

All six comparative/interface nodes in the `cross_biochemistry` solution family require `alien_biology` evidence.

Therefore simply selecting a human, ammonia, cryogenic, or silicon-centered start does not reveal future alien-biochemistry interface technology before the civilization has actually encountered alien biology.

## Performance

This benchmark is a tiny offline applicability check over 30 public nodes. It does not change production runtime behavior.

Production research still:

- stores the universal graph once;
- indexes nodes by applicability/evidence/pressure/prerequisite/capability;
- materializes only a civilization's relevant visible horizon;
- avoids full-graph per-tick and per-frame scans.

## Canonical files

- `data/research/v1/biochemistry_benchmark_scenarios.json`
- `scripts/validate_research_biochemistry_benchmarks.py`
- `docs/RESEARCH_BIOCHEMISTRY_BENCHMARK.md`
