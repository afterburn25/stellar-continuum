# Starting Research Histories & Runtime Contract

Stellar Continuum does **not** give each playable species a hand-authored fixed technology tree.

Instead, a new civilization begins with a **composed scientific history**: what it already knows, what scientific practices it is good at, what research institutions physically exist, what unresolved problems it recognizes, and what evidence it legitimately possesses on January 1, 2050.

After that historical state is composed, the normal Adaptive Research emergence system generates the civilization's current visible research horizon.

## Why this matters

Two civilizations can begin at a broadly comparable early-space/pre-FTL era while having very different scientific histories.

One may have excellent orbital engineering and mediocre biological science. Another may have strong closed-ecology practice but weaker robotics. A synthetic civilization can begin with machine cognition as an existing fact without ever having followed a human-style Synthetic Cognition research lineage.

The **future tree is never stored in the starting profile**.

## Starting profile composition

A valid start contains exactly one base-era profile plus zero or more historically justified fragments.

Current reusable public fragments include examples for:

- early-space scientific foundations
- orbital industrialization
- fission/storage infrastructure
- fusion-transition research
- computational automation
- deep-space observation/communications
- closed-loop metabolic habitation
- advanced structural materials
- economic/logistical competence
- machine-origin science
- high-gravity biological experience
- low-gravity settlement experience

These are history fragments, not species IDs.

The species/start workstream can compose them differently and can supply additional biology/home-system facts through the defined interface.

## Reference starts

The data includes several reference compositions purely to prove the architecture:

### Human-like Solar 2050

The human-like reference has mature modern science, orbital activity, automation, deep-space observation, closed-loop settlement experience, fission/storage infrastructure, and advanced conventional materials.

Practical fusion is intentionally only **Investigable** rather than automatically Mature.

No FTL hypothesis is seeded merely because the calendar says 2050.

### Synthetic early-space civilization

The synthetic reference begins with `machine_cognition_present` as an existing civilization fact.

It is not required to claim that the civilization independently researched the human `synthetic_cognition` node. This is the same principle used throughout Adaptive Research: **capability/history is not the same thing as implementation lineage**.

### High-gravity metabolic civilization

The high-gravity reference begins with real high-gravity medical pressure and relevant competence. It does not preselect whether the civilization solves the problem using medicine, genetic adaptation, cybernetics, habitat design, or another later solution.

### Low-gravity settlement civilization

Likewise, low-gravity history creates legitimate health/development pressure without preselecting a technological response.

## Starting state is prerequisite-closed

A complete starting composition must be historically coherent.

If a node begins Mature, Experimental, Demonstrated, Engineering, or Investigable, its required knowledge prerequisites must already be Mature in the composed historical state.

Starting research institutions with explicit enabling technologies also require those technologies to be Mature.

This prevents a start from quietly receiving an advanced technology without the history required to support it.

The validator checks the complete reference compositions rather than requiring every reusable fragment to be independently complete; fragments are intentionally composable partial histories.

## Starting competence

Starting field competence uses the same three dimensions as campaign research:

- theoretical
- experimental
- engineering

Fragments combine using the strongest justified component values with a composition cap. They **do not add percentages together**.

This keeps scientific history meaningful without allowing several overlapping fragments to create absurd starting competence.

## Research runtime boundary

Other workstreams should not directly manipulate the hidden graph.

They interact with Adaptive Research through normalized input events and stable queries.

### Typical inputs into research

Examples include:

- resource/energy/food shortages
- health or environmental burdens
- transport/logistics problems
- fleet losses or observed enemy capabilities
- acquired evidence or alien artifacts
- construction/destruction of research facilities
- population/species applicability changes
- actual deployment of a research-enabled entity
- acquisition/loss of foreign experts, tooling, factories, or technology packages

The owning subsystem supplies the factual event or normalized metric. Adaptive Research determines which pressure/evidence/candidate indexes are affected.

### Typical queries from other systems

Other systems can ask questions such as:

- Does this civilization have `interstellar_transit`?
- Does this population have compatible `long_duration_habitation`?
- Is this visible technology Mature?
- Why is this visible project blocked?
- How much eligible research capacity is available?
- What does this civilization currently understand about a foreign device?
- What is the known research utility/dependency risk of this technology-transfer package?

A caller should normally ask for a **capability**, not reverse-engineer which technology created it.

## Project progression

For an active project the runtime:

1. verifies current visibility and directed-program capacity;
2. verifies minimum eligible labs;
3. checks hard stage requirements such as facilities/evidence/material access;
4. calculates stage-relevant Project Readiness;
5. applies lab diminishing returns and readiness to current RP production;
6. advances the project stage;
7. resolves only contextually permitted setback/hypothesis outcomes;
8. emits compact research-state changes;
9. grants valid capabilities/structural effects when appropriate;
10. wakes only indexed neighbors rather than scanning the entire graph.

## Materialized research view

The UI consumes a read-only projection rather than raw runtime state.

The projection contains only legitimately visible information:

- visible nodes
- visible-to-visible edges
- active projects
- recognized pressures
- known blockers
- optional recognized field competence
- known foreign-technology assessments
- bounded recent events
- paged/collapsed history

Unknown future nodes and hidden node counts never appear in the projection.

## UI commands are requests

Starting research, pausing, resuming, or reallocating labs uses stable visible node IDs, but the authoritative research runtime always revalidates the command.

A stale UI cannot start a project that has since lost its facility, evidence, capacity, or applicability.

## Performance

The runtime does not maintain a full active copy of the 330-node catalog for every civilization.

It stores compact civilization-specific state and uses indexes for:

- pressure -> candidates
- evidence -> candidates
- trait -> candidates
- prerequisite -> children
- capability -> candidates
- field -> nodes
- foreign constraint -> assessments

Starting fragments are only composed at new-game/migration boundaries. They are not reevaluated every campaign tick.

UI projections are revision-cached and rebuild on meaningful research changes rather than every frame.

Dormant civilizations can evaluate research at coarser strategic intervals.

## Cross-workstream ownership

Adaptive Research owns:

- research graph/state
- research-history profile schema and validation
- emergence/progression
- field competence
- research-facing facility requirements
- foreign-tech interpretation
- capability queries
- research view-model semantics

Other workstreams own the facts they produce:

- Construction/economy: physical facility construction/cost/upkeep/damage
- Species/population: biological and population facts
- Diplomacy/trade: negotiation/payment/legal consequences
- Exploration/intelligence: legitimate observations/evidence acquisition
- UI: presentation/interaction using the view-model contract

## Canonical files

- `data/research/v1/starting_research_profile_contract.json`
- `data/research/v1/starting_research_fragments.json`
- `data/research/v1/starting_reference_profiles.json`
- `data/research/v1/research_runtime_contract.json`
- `data/research/v1/research_view_model_contract.json`
- `scripts/validate_research_start_runtime.py`

The reference starts are **balance/design seeds**, not locked commercial starting content.
