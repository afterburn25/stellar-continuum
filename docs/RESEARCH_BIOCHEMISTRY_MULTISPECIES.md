# Multispecies Biochemical Research Scope

A civilization can contain multiple populations with substantially different biochemistry.

Adaptive Research must distinguish **shared scientific knowledge** from **population-scoped biological applicability**.

## Core rule

A civilization can know a technology without every population being able to use it.

Example:

- a mixed civilization contains humans and an ammonia-rich population;
- the civilization researches `ammonia_solvent_homeostasis` using the ammonia population as the target applicability context;
- the scientific/engineering knowledge becomes part of civilization research history;
- ammonia-compatible medicine/operation applies to the relevant ammonia biochemical population;
- the human population does **not** gain ammonia compatibility simply because its government owns the research record.

The reverse is also true: water-solvent medicine is not automatically useful to a silicon-centered or cryogenic-hydrocarbon population.

## Research project context

Biochemistry-specific directed projects should carry a stable `target_applicability_context` identifying the relevant population/species biochemical context.

This already fits the existing materialized/runtime project contract.

The target context affects:

- applicability validation;
- evidence interpretation;
- specialist facility environment;
- expert/tacit knowledge relevance;
- Project Readiness;
- operational capability scope after maturity.

## Mature knowledge

Mature biochemical research can be stored as civilization knowledge while recording which population contexts have validated operational applicability.

Do not duplicate the entire technology node for every individual population.

A compact implementation can track:

- civilization knows node X;
- validated target context IDs for X;
- population-scoped capabilities produced for those contexts;
- optional adaptation/compatibility status for additional populations.

This keeps state bounded while preserving species differences.

## Cross-biochemistry research

Cross-biochemistry technologies can deliberately expand the set of supported contexts.

For example:

- `cross_biochemistry_medicine` can establish meaningful support for a foreign biochemical population;
- `multibiochemistry_habitat` can allow an installation to support multiple incompatible biochemical populations through separate environmental systems;
- `cross_biochemistry_biofabrication` can let a civilization engineer across multiple biochemical lineages.

These do not make the populations biologically identical.

## Migration / conquest / federation

If a civilization gains a new population through migration, federation, conquest, uplift, or annexation:

- new biochemical traits become present in the relevant population context;
- research candidate indexes may wake compatible branches;
- experts/institutions belonging to that population can supply tacit knowledge;
- previously useless foreign biological technology may suddenly acquire operational or trade value;
- existing native medicine/life support may be inadequate for the new population.

Nothing is unlocked simply because territory changed color; knowledge, people, facilities, compatibility, and institutions still matter.

## Performance

Do not materialize a complete per-population copy of the 360-node graph.

Use:

- one shared static graph;
- civilization-level known/mature node records;
- sparse target-context applicability/capability records only for technologies where population scope matters;
- indexes keyed by changed population biochemical traits;
- low-frequency cleanup/merging for obsolete or extinct contexts while preserving historical summaries.
