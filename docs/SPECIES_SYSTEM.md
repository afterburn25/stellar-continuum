<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> Godot/C#/.NET references below are legacy implementation or fixture provenance,
> not the current runtime or instructions to restore it.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Species and Race Mechanics

This document defines the public architecture for biological species mechanics in **Stellar Continuum**.

The player may casually think of this as the game's "race system," but the simulation deliberately separates layers that many strategy games collapse together:

1. **species biology** — inherited physiology, chemistry, morphology, perception, metabolism, life history and environmental requirements;
2. **population adaptation** — acclimatization and longer-lived changes affecting a particular population;
3. **population/demography** — actual cohort size, growth, mortality, migration and composition;
4. **civilization traits/culture** — learned political, social, strategic and institutional behavior;
5. **technology/capabilities** — ways a civilization compensates for or exploits biological/environmental conditions.

These layers must remain separate enough that biology does not automatically dictate culture, politics, morality, intelligence, diplomacy or ideology.

## Core rule: contextual consequences, not arbitrary racial bonuses

A species does not receive a generic permanent modifier such as `+10% combat`, `+15% science`, `+20% ship range`, or `-10% diplomacy` merely because of its species identity.

Advantages and disadvantages arise from physical circumstances wherever practical.

Examples:

- A population evolved for high gravity can function naturally on a high-gravity world while another population needs substantial mitigation.
- That same high-gravity species can itself be outside its comfortable range on a low-gravity colony.
- An aquatic species can require immersed crew spaces and colony habitats even when temperature and atmospheric chemistry otherwise match.
- A cryogenic hydrocarbon species can find an Earth-like environment lethal while thriving where water/oxygen biology cannot.
- Radiation tolerance changes shielding burden; it is not a universal military bonus.
- Body mass, metabolic demand and dormancy physiology affect transport and life-support burden rather than granting abstract economic modifiers.
- Lifespan, maturity and generation length constrain demographics and adaptation tempo rather than automatically changing research quality.
- Different manipulators/body plans can make another species' equipment awkward or unusable without redesign; this is an engineering-interface problem, not a blanket competence penalty.
- Species that lack a shared natural signal channel require instrumentation/translation rather than receiving an automatic diplomatic dislike modifier.

## Static species definition

`SpeciesDefinition` is immutable shared data referenced by stable species ID. It currently combines:

- biochemical basis and habitat mode;
- physiology;
- environmental preferences/tolerance bands;
- breathable atmospheres and compatible biological solvents;
- species-specific adaptation/plasticity profile;
- xenobiological molecular profile;
- sensory and natural communication profile;
- morphology/ergonomics;
- metabolism/activity/dormancy profile;
- biological life history.

Full definitions must not be copied into every population/save record. Runtime population state references them by stable ID.

## Physiology and environmental preferences

`SpeciesPhysiology` currently carries:

- typical adult mass;
- baseline lifespan;
- maturity age;
- baseline metabolic demand;
- radiation tolerance;
- musculoskeletal robustness.

`SpeciesEnvironmentalPreferences` currently carries:

- preferred/comfortable/survivable gravity range;
- preferred/comfortable/survivable temperature range;
- preferred/comfortable/survivable pressure range;
- preferred atmosphere;
- biological solvent;
- immersion requirement;
- unprotected-vacuum capability where biologically appropriate.

`HabitatEnvironment` is the interface-sized physical environment input: gravity, temperature, pressure, atmosphere, available solvent, normalized radiation hazard and immersion state.

A later planet/environment owner should provide these physical values. Species mechanics must not create a competing planet-generation model.

## Natural environmental assessment

`SpeciesEnvironmentEvaluator` deterministically compares a species/population with a habitat.

Its `SpeciesEnvironmentAssessment` exposes:

- natural habitability;
- unprotected operational capacity;
- gravity/temperature/pressure/atmosphere/solvent/immersion/radiation suitability;
- strongest limiting factor;
- gravity mitigation requirement;
- thermal-control requirement;
- pressure-control requirement;
- sealed-habitat requirement;
- artificial-biosphere/immersion requirement;
- radiation-shielding requirement;
- derived health stress.

**Natural habitability is constrained by the worst essential physical requirement.** Excellent temperature cannot average away an unbreathable atmosphere or incompatible solvent.

Unprotected operational capacity uses a geometric mean as a smooth secondary summary while preserving hard limiting factors separately.

## Supported habitats and technological compensation

`HabitatSupportCapabilities` / `SpeciesSupportedHabitatEvaluator` model what a functioning habitat can physically provide without hard-coding a specific research tree.

Support can include bounded:

- gravity correction;
- thermal control;
- pressure control;
- sealed atmosphere support;
- compatible biosphere/solvent support;
- immersion support;
- radiation shielding.

The evaluator keeps **natural habitability** and **supported habitability** separate. Technology can make an otherwise hostile place usable without pretending the species naturally evolved for it.

Research, construction and logistics decide how a civilization obtains, powers, maintains and pays for those capabilities.

## Species-specific adaptation and plasticity

`SpeciesAdaptationProfile` describes inherited plasticity rather than a generic empire-wide "adaptable" bonus:

- acclimatization responsiveness;
- developmental plasticity;
- multigenerational adaptability;
- maximum natural preference shift;
- maximum natural tolerance expansion;
- maximum natural radiation-tolerance increase.

`PopulationAdaptationState` belongs to a population and can contain bounded changes to:

- gravity preference/tolerance;
- temperature preference/tolerance;
- pressure preference/tolerance;
- radiation tolerance;
- acclimatization.

`PopulationAdaptationProgression` advances this state deterministically on a low-frequency demographic cadence.

Key rules:

- calendar time is converted into species-relative generations;
- locally born fraction matters to developmental/multigenerational changes;
- species plasticity affects rate and ceiling;
- long-term natural adaptation requires a nonlethal environment;
- atmosphere, solvent and required-immersion chemistry do not silently mutate through passive residence;
- natural changes remain capped relative to the baseline species envelope;
- base species definitions are never rewritten by local population adaptation.

Major biochemical redesign, directed evolution, cybernetics or divergent speciation belongs to explicit future systems.

## Bounded population cohorts

`SpeciesPopulationCohort` is the species-side representation for one meaningful species/adaptation band:

- species ID;
- population millions;
- adaptation state;
- residence years;
- generations in environment;
- locally born fraction.

It deliberately does not own births/deaths, migration, employment, culture, politics, housing or economy.

`PopulationCohortReducer` prevents adaptation history from producing unbounded micro-cohorts:

- default maximum detailed adaptation cohorts per species: **4**;
- hard supported maximum: **8**;
- only cohorts of the same species can merge;
- different species are never averaged together;
- closest adaptation states merge first using deterministic normalized distance;
- population is conserved;
- summaries are population-weighted;
- input enumeration order does not affect the reduced result.

## Current colony population bridge

The current gameplay colony model still owns one scalar `PopulationMillions`; it is not yet a true multi-species collection.

To make present physical colonization species-safe without prematurely replacing the population engine, the branch uses a transitional **single-species scalar bridge**:

- `ColonyState.PopulationSpeciesId` identifies the species represented by the colony's scalar population;
- `ShipyardState.ReservedPopulationSpeciesId` travels with active reserved colonists;
- queued `ShipBuildOrderState` entries retain their reserved population species;
- `FleetState.EmbarkedPopulationSpeciesId` travels with physically embarked colonists;
- a founded colony receives exactly the species ID carried by the colony ship;
- when the ship unloads, both embarked population and its species ID are cleared.

The physical chain is therefore:

`source colony population + species` → `shipyard reservation + species` → `completed colony fleet + species` → `destination colony population + species`.

Population quantity and species identity must be conserved together.

This bridge is intentionally **not** the final multi-species representation. When the colony/population owner replaces the scalar with bounded cohorts, these single-species fields should be migrated into the cohort collection rather than expanded into multiple parallel scalar fields.

## Biological life history

`SpeciesLifeHistory` records biological demographic constraints:

- reproductive mode;
- reproductive maturity age;
- typical offspring per reproductive event;
- minimum biological interval between events;
- dependent-development duration;
- reproductive span;
- baseline generation length.

`SpeciesDemographicEnvelopeEvaluator` derives quantities such as generations per century and a biological reproductive upper envelope.

These are **not actual population growth rates**. Real growth must eventually include mortality, health, resources, housing, reproductive-role structure where relevant, policy, war, migration, environment and social behavior.

## Metabolism, activity and dormancy

`SpeciesMetabolismProfile` separates biological energy/life-support demand from arbitrary strategic bonuses.

It can describe:

- thermoregulation strategy;
- resting metabolic demand multiplier;
- peak activity demand multiplier;
- natural dormancy/torpor mode;
- metabolic demand while dormant;
- maximum continuous natural dormancy duration.

`SpeciesMetabolicDemandEvaluator` provides physical demand envelopes for normal/rest/dormant states.

A naturally torpor-capable species may therefore need less life support during a long voyage **only while actually dormant and only within its biological limits**. A non-torpor species does not receive that benefit unless a separate medical/technological system provides it.

## Morphology and equipment ergonomics

`SpeciesMorphology` describes physical interface facts such as:

- body plan;
- locomotion mode;
- normal work orientation;
- typical body length/width/reach;
- primary manipulator count;
- fine-manipulator count;
- buoyant-workspace requirement.

`SpeciesEquipmentCompatibilityEvaluator` is directional: species A using equipment designed for species B is not necessarily equivalent to B using A's equipment.

It exposes direct-use compatibility and whether a user needs:

- control adaptation;
- workspace adaptation;
- environmental enclosure/crew-space adaptation.

This supports alien ships, captured equipment, multi-species crews and habitat design without converting morphology into a generic skill or combat rating.

## Perception and natural communication

`SpeciesPerceptionProfile` represents sensory and natural signaling modalities such as visible/IR/UV perception, airborne or waterborne sound, vibration, pressure sense, chemoreception, electrosense, vocal/visual/chemical/vibrational/bioluminescent signaling and related channels.

`SpeciesCommunicationCompatibilityEvaluator` measures only **physical channel overlap**.

It can indicate:

- shared sensory compatibility;
- shared natural signaling compatibility;
- whether sensory translation is needed;
- whether communication mediation/instrumentation is needed.

It does not model language comprehension, culture, trust, diplomacy or willingness to communicate. No shared natural channel means "build a translator/interface," not "these species dislike each other."

## Xenobiology and medical compatibility

`SpeciesXenobiologyProfile` separates molecular/medical facts from social relations:

- nutrient chirality;
- hereditary-system class;
- cellular organization;
- protein-like catalyst usage;
- whether self-replicating microscopic parasites are biologically plausible.

`SpeciesXenobiologyCompatibilityEvaluator` separately evaluates:

- broad biochemical interoperability;
- nutritional cross-compatibility;
- potential cross-pathogen transmission;
- tissue-integration potential;
- natural reproductive compatibility.

Shared carbon/water chemistry does **not** imply reproductive compatibility.

`SpeciesBiologicalRelationshipCatalog` is the only authority for explicit natural cross-species reproductive relationships. If no pair relationship exists, natural hybridization is zero even when the two species share solvent, atmosphere, chirality or molecular architecture.

This allows a pair to have meaningful food/medical/pathogen interoperability without inventing biologically implausible hybrids.

## Physical requirement summaries for other systems

`SpeciesPopulationRequirementsEvaluator` translates a population cohort into read-only physical quantities including:

- population size;
- reference metabolic demand;
- aggregate adult biomass;
- typical adult mass;
- lifespan/generation length;
- generations per century;
- environmental assessment;
- number/type of environmental mitigation categories currently required.

These are **inputs**, not finished bonuses.

Intended consumers include:

- economy/logistics: feedstock, water, gas, power, habitat and cargo burden;
- shipbuilding/habitation: crew-space geometry, atmosphere, pressure, immersion, gravity and biomass requirements;
- medicine: environment stress, xenobiology and life history;
- combat: local environmental operation and actual physiology rather than universal species modifiers;
- research: biological applicability and foreign-technology compatibility conditions.

## Initial mechanical proving-ground species

Current prototypes are deliberately different enough to exercise the architecture:

- `terran_baseline` — water/carbon terrestrial baseline near Earth-like conditions;
- `pelagic_high_pressure` — water/carbon aquatic biology requiring immersion and high pressure;
- `compact_high_gravity` — water/carbon terrestrial physiology centered on strong gravity and dense conditions;
- `cryogenic_hydrocarbon` — hydrocarbon-solvent carbon biology in very cold reducing conditions, with deep natural torpor.

They differ in environment, morphology, perception, molecular biology, metabolic behavior, plasticity and life history.

They are **mechanical proving grounds, not final lore commitments or the final playable-species roster**.

Synthetic/post-biological life exists in the type system but requires explicit energy, maintenance, fabrication/reproduction, consciousness-continuity and environment design before becoming a finished playable species.

## Civilization behavior remains separate

Existing `CivilizationTraits` such as aggression, territoriality, greed, scientific curiosity, risk tolerance, survival priority and honor describe civilization/AI behavior.

They are not inherited species statistics.

A species can produce many cultures/governments/strategic histories. A future mature civilization can also contain multiple species.

New seeded civilizations receive a deterministic founding `SpeciesId` through `SpeciesAssignmentPolicy`, based on campaign seed + civilization ID rather than civilization archetype/personality.

## Adaptive Research boundary

Adaptive Research owns its possibility graph, applicability schema, capability schema, competence model, foreign-technology transfer model and research UI data.

Species owns physical/biological facts.

Research may consume species facts for applicability, xenobiological compatibility, interface requirements or environmental pressure. Species code must not create a competing research system.

## Colony/population boundary

Species now provides:

- validated species identity;
- transitional single-species scalar identity for current colonies/transports;
- bounded future cohort type;
- cohort reduction;
- adaptation progression;
- physical requirement summaries.

The colony/population owner remains responsible for:

- births/deaths;
- scalar growth during the current bridge phase;
- future multi-species composition ownership;
- migration;
- cohort creation/splitting;
- housing/health/policy consequences;
- determining when distinctions are strategically meaningful enough to retain.

When multi-species colonies become authoritative, use `SpeciesPopulationCohort` rather than creating an unbounded list of individuals or parallel species-specific scalar fields.

## Logistics, shipbuilding and combat boundaries

Species mechanics exposes causes such as:

- metabolic/life-support demand;
- habitat pressure/temperature/atmosphere/solvent/immersion needs;
- radiation shielding;
- gravity requirements;
- passenger biomass;
- dormancy envelope;
- body geometry/ergonomics;
- sensory/interface compatibility;
- local environmental operating capacity.

Other workstreams decide how those facts affect their own systems. Species must not duplicate logistics, ships, ground combat or economic accounting.

## Persistence and migration

The branch builds on the physical shipbuilding/colonization **save v7** baseline and defines candidate **save v8**.

V8 currently persists:

- civilization founding `SpeciesId`;
- current scalar colony `PopulationSpeciesId`;
- shipyard active/queued reserved-population species IDs;
- embarked colony-fleet population species ID;
- existing v7 shipyard state;
- existing physical embarked-population quantities;
- staged survey knowledge and the rest of current integration state.

Migration rules:

- v1–v7 civilization species identity is assigned deterministically from campaign seed + civilization ID;
- v7 colony/fleet/shipyard population species identity is reconstructed from the owning civilization without changing population amounts;
- pre-v7 physical-colonist migration still deducts real population before creating/filling legacy colony fleets;
- v8 rejects unknown species IDs rather than silently substituting biology;
- reconstructible environmental/compatibility assessments are not serialized.

`SpeciesPopulationCohort` adaptation state is not persisted yet because the colony system does not yet own authoritative cohorts. Persisting unattached side-table cohorts would create a second source of truth.

## Scalability rules

- Never create one simulation object per individual person.
- Do not duplicate full species definitions into populations/saves.
- Keep detailed adaptation cohorts bounded.
- Never merge different species merely to satisfy an adaptation-detail bound.
- Environmental/compatibility assessments are deterministic and reconstructible.
- Do not serialize reconstructible caches.
- Adaptation runs on demographic/maintenance cadence, not every frame.
- AI should consume relevant derived summaries rather than recomputing every species/environment pair every tick.

## Current implementation status

Implemented on `work/species-race-mechanics`:

- immutable species definitions and validation;
- deterministic founding-species assignment independent from civilization personality;
- environmental tolerance/habitability assessment;
- supported-habitat/mitigation assessment;
- species-specific adaptation/plasticity and multigenerational progression;
- bounded same-species population cohorts and deterministic cohort reduction;
- life-history/demographic envelopes;
- metabolic/activity/dormancy envelopes;
- physical population-requirements summaries;
- morphology and directional equipment/workspace compatibility;
- sensory/natural-communication channel compatibility;
- xenobiological nutrition/pathogen/tissue/reproductive compatibility boundaries;
- explicit-only natural cross-species hybridization relationship authority;
- single-species scalar population identity through colony → shipyard → fleet → colony transfer;
- candidate save v8 migration/persistence for civilization and transferred-population species identity;
- combined current integration validation: research validators, core simulation, core runtime, quality/fair-information, logistics, species checks, and Godot smoke gates.

Still intentionally deferred:

- detailed planet/environment state beyond the prototype system-level `HasHabitableWorld` flag;
- authoritative bounded multi-species cohort ownership inside `ColonyState`;
- persisted adaptation cohorts;
- species-driven birth/death/migration rules inside the population engine;
- explicit directed genetic redesign/speciation systems;
- full species-aware life-support cost accounting inside logistics;
- species-aware local ground-operation consequences inside combat;
- player-facing species selection/customization UI;
- final species lore, art, names and roster.
