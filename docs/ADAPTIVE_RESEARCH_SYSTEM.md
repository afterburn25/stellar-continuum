<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> Godot/C#/.NET references below are legacy implementation or fixture provenance,
> not the current runtime or instructions to restore it.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Adaptive Research System

## Purpose

Stellar Continuum does not use a conventional fixed technology tree that the player can inspect from beginning to end.

The simulation contains a broad **Technology Possibility Graph** describing discoveries that may be physically/scientifically possible in the setting. Each civilization materializes only the small portion of that graph it currently understands well enough to investigate.

The visible research tree therefore changes as the civilization changes. A civilization may open the research screen in 2080 and see branches that did not exist for it in 2050. Another civilization beginning from similar broad knowledge can reach 2400 with a radically different tree because its biology, environment, wars, resources, discoveries, culture, institutions, and contact history were different.

This creates deep technological divergence without maintaining a separate handcrafted fixed tree for every playable species.

## Core rules

1. **The player never sees the complete possibility graph.**
2. **Need creates Research Pressure, but need is not the only source of discovery.**
3. **Basic science can expose possibilities before a practical use is known.**
4. **Observation, anomalies, alien contact, captured devices, and foreign science can expose branches absent from the native research horizon.**
5. **A known possibility is not automatically researchable.** Prerequisite knowledge, evidence, infrastructure, biology, materials, pressure where explicitly required, and laboratory capacity can still be missing.
6. **A researched idea is not instantly mature technology.** Hypothesis, experiment, demonstration, engineering, and mature deployment are distinct states.
7. **Speculative hypotheses can fail or produce partial/side discoveries.**
8. **Similar capabilities can have different technological implementations.**
9. **Foreign technology is not an instant unlock and may be incompatible, dangerous, or beyond current understanding.**
10. **Dominant civilizations do not receive an arbitrary research penalty.** Complacency/catch-up emerges from conditions, institutions, culture, intelligence, competition, and frontier difficulty.
11. **A civilization can remain ahead if it continues investing intelligently.** There is no forced rubber-band equalization.
12. **Research must scale to very long campaigns.** The runtime must never evaluate the whole graph every simulation tick for every civilization.

## Player-facing tree

Only the civilization's current research horizon is rendered.

Possible states are:

- **Rumored** — weak/uncertain evidence suggests a phenomenon or possibility.
- **Hypothesized** — scientists can describe a testable idea.
- **Investigable** — requirements are sufficient for a serious directed program.
- **Experimental** — prototypes/tests are underway.
- **Demonstrated** — the principle works under controlled conditions.
- **Engineering** — civilization is making it practical, reliable, and manufacturable.
- **Mature** — established capability/technology.
- **Archived** — mature or historically important knowledge no longer occupies the active research horizon.

`Unknown` possibilities are never rendered.

The tree is therefore a living map of what the civilization currently knows and considers plausible. Branches can appear, split, reconnect, become dormant, or become historically archived.

## What exposes a branch?

### Problem-driven pressure

Examples include high/low-gravity health burdens, radiation, food/water shortages, supply-line overstretch, maintenance failures, missile threats, armor failures, enemy mobility superiority, sensor blindness, communication delay, ecological damage, resource scarcity, labor shortages, research bottlenecks, and AI-safety incidents.

A problem does not guarantee one solution. It raises contextual Research Pressure and makes applicable solution families more relevant.

### Basic/exploratory science

Civilizations can fund high-energy physics, gravitational physics, quantum measurement, exoplanetary science, materials characterization, complex systems, observatories, metrology, and other fields before a direct application is known.

**Complexity alone never creates a Research Pressure requirement.** A frontier basic-science project can become investigable without a crisis when prerequisite knowledge/evidence supports it.

### Observation/discovery

New evidence can permanently alter the tree:

- alien signals
- observed foreign propulsion or weapons
- captured foreign devices
- alien biology
- anomalous astrophysical phenomena
- battlefield telemetry
- unexpected experiments

Observation may prove that a capability exists without revealing how it works.

## Research economy: RP + Pressure + Labs

Adaptive Research uses three separate quantities.

### Research Points (RP)

Research Labs generate RP. Assigned laboratories apply RP to directed projects. Large universal RP stockpiles are avoided so centuries of banked generic science cannot instantly finish a newly exposed field.

### Research Pressure

Research Pressure is a bounded 0–100 condition/evidence score. It is not spent and does not directly produce RP.

Only nodes explicitly configured with `required_pressure` or `required_pressure_any` are hard-gated by pressure. Pressure affinities make a technology relevant but do not automatically prohibit basic-science discovery.

### Effective Research Labs

Every directed project requires minimum assigned lab capacity. More labs can accelerate the project, with diminishing coordination returns at very large program sizes.

Early civilizations formally direct one major strategic project while unassigned labs continue diffuse/basic science. The possibility graph now contains actual institutional discoveries that unlock greater coordination:

1. **Single Priority Program** — 1 directed major project.
2. **Coordinated Research Networks** — 2 directed projects.
3. **Distributed Scientific Portfolios** — up to 4 directed projects.
4. **Autonomous Research Portfolios** — no arbitrary slot ceiling; lab capacity becomes the practical limit.

This keeps early play simple without pretending all scientists work on one subject.

## Natural catch-up and complacency

A civilization with overwhelmingly superior warships may gradually experience less military urgency because current designs win, no credible rival is visible, doctrine appears proven, funding shifts elsewhere, and further progress sits at a harder frontier.

A weaker civilization facing those ships can accumulate strong pressure through losses, observed performance gaps, wreckage, telemetry, espionage, and political urgency.

This can narrow a technological gap without hidden underdog bonuses. When the leader observes credible rival progress, its own pressure can rise again and create an arms race.

Culture, government, threat sensitivity, and innovation norms modify this. A paranoid or innovation-focused civilization may remain highly active even while dominant.

## Capability vs implementation

The simulation reasons about broad capabilities separately from the technologies that implement them.

Examples:

### Long-duration habitation

- bioregenerative life support
- rotating habitats
- metabolic torpor
- symbiotic biological systems
- machine habitats

### FTL access

Public normal possibilities currently include:

- warp-field development
- infrastructure-heavy stabilized wormholes

Additional rare/secret routes are intentionally excluded from the public catalog.

### Spacecraft survivability

- layered/reactive/adaptive armor
- active protection
- defensive fields
- living/self-repairing hulls

### Food and biosphere independence

- closed-loop ecology
- synthetic food
- microbial protein
- automated agronomy
- engineered symbiotic crops
- self-sustaining colony biospheres

### Environmental adaptation

- medical acclimatization
- inherited biological adaptation
- powered/adaptive exosystems
- cybernetic gravity compensation

A civilization can therefore satisfy the same strategic need through a different technological history.

## Species and biology

The catalog is shared at universe scale, but individual nodes have applicability requirements.

A metabolic biological civilization can investigate gravity medicine; a synthetic civilization does not need cardiovascular treatment. An aquatic or silicon-centered lineage can have different environmental pressures and therefore materialize different parts of the same broad possibility space.

Species traits do not select a fixed species tree. They filter and weight what can plausibly emerge.

## Foreign technology

Suggested progression:

1. observe a foreign capability
2. obtain telemetry/sample/device
3. identify the scientific domain
4. perform material/software/biological analysis
5. determine compatibility
6. reproduce subsystems where possible
7. create a native adaptation
8. potentially create hybrid technology

Some steps may be impossible with current science or biology. A technology useless to its current holder can still be enormously valuable to another civilization, enabling future technology trade, licensing, brokerage, espionage, and monopolies.

## Runtime scalability

The static catalog is not an active per-civilization tree.

Each civilization stores only compact research state such as:

- mature technology IDs
- known hypotheses
- currently visible/investigable candidates
- active directed programs
- allocated effective labs and RP progress
- field competencies
- evidence tokens
- known-field Research Pressures
- cultural/government priorities
- recent discovery history

### Candidate indexing

Build static indexes once:

- prerequisite -> child nodes
- pressure -> candidate nodes
- knowledge field -> candidate nodes
- evidence type -> candidate nodes
- trait/applicability -> candidate nodes
- capability/solution family -> candidate nodes

Re-evaluate candidates only when relevant state changes: a technology matures, evidence arrives, a pressure crosses a meaningful band, traits change, research institutions change, or a low-frequency research review runs.

A universe catalog containing hundreds or eventually thousands of possibilities should still leave each civilization with only a few dozen active research-state records.

### Persistence

The full static catalog belongs in game data, not duplicated in every save. Campaign databases store stable IDs plus civilization-specific state. Old detailed research events can be compressed into historical milestones.

## Public seed dataset v1

The current public seed contains:

- **330 possibility nodes**
- **20 research domains**
- **59 Research Pressure types**
- **15 explicit alternative-solution sets**
- a validated prerequisite graph
- applicability/evidence tags
- pressure affinities
- knowledge-field tags
- capability outputs
- Research Point/Lab/Pressure requirement metadata

Domains:

1. Foundational Science
2. Energy & Power
3. Propulsion & Transit
4. Space Industry & Habitats
5. Life Support & Medicine
6. Planetary Engineering
7. Materials Science
8. Computing, AI & Robotics
9. Sensors & Communications
10. Military Engineering
11. Logistics & Fabrication
12. Administration & Institutions
13. Xenoscience & Foreign Technology
14. Biotechnology & Living Systems
15. Synthetic Civilization Systems
16. Agriculture & Biosphere Engineering
17. Economic & Trade Systems
18. Cybernetics & Augmentation
19. Scientific Infrastructure & Metrology
20. Megastructure & Stellar Engineering

The dataset is architectural seed data, not final balance.

## Public repository boundary

The public dataset contains normal research possibilities only. It must not expose exact hidden discovery triggers, rare probabilities, complete secret artifact chains, hidden special-AI eligibility, or intentionally secret technologies. Those can plug into the same runtime schema from a separate content source later.

## Validation requirements

Before accepting a catalog change:

- node IDs must be unique
- every prerequisite reference must exist
- graph must remain acyclic
- pressure references must exist
- alternative-solution node references must exist
- domain counts/index must match files
- research-economy overrides must reference valid nodes/pressures
- lab/RP requirements must be sane
- static data must not contain campaign/player state
- public catalog must not declare secret content enabled

`scripts/validate_research_catalog.py` enforces these structural requirements in CI before .NET/Godot validation.

## Early-release requirement

Early Access does not need every one of the 330 seed nodes implemented as finished gameplay content. It **does** need the runtime architecture to support the evolving visible tree, selective Research Pressure gates, RP/Lab capacity, staged parallel directed research, multiple discovery sources, applicability/evidence filtering, alternative implementations, foreign-tech state, archival/mature research state, bounded candidate evaluation, and save-safe IDs.

This is the architectural boundary that prevents us from having to rebuild research after long campaigns and additional species already exist.
