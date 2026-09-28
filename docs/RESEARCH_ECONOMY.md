# Research Economy

This document defines the player-facing resource model that powers Stellar Continuum's Adaptive Research System.

The goal is simple early research with much deeper late-game behavior as civilizations, species, institutions, and technology diverge.

## The three research quantities

### 1. Research Points (RP)

Research Points measure scientific/engineering work applied to an active project.

Operational **Research Labs** generate RP. The early playable balance is **400 RP per Effective Research Lab per in-game year**; later campaign tuning may refine it.

RP normally flows from active laboratory capacity into projects instead of accumulating as a huge civilization-wide stockpile. A civilization should not save centuries of generic research and instantly complete a newly discovered field.

Unassigned laboratories still contribute to basic science, field competence, hypothesis generation, observation/evidence processing, and exploratory work.

### 2. Research Pressure

Research Pressure represents recognized need, opportunity, danger, or compelling evidence.

Examples:

- high-gravity health problems
- missile threats
- serious fleet losses
- supply-line overstretch
- inability to reach distant systems
- ecological damage
- observed foreign technology
- research throughput bottlenecks

Pressure is normalized on a **0–100** scale, is not spent, and can decay when the underlying condition disappears.

### Pressure is selective, not universal

Only technologies explicitly configured with `required_pressure` or `required_pressure_any` are hard-gated by pressure.

A node's `pressure_affinities` say which circumstances make the research relevant; they are not themselves an availability requirement.

Likewise, Advanced or Frontier complexity does **not** automatically require pressure. Basic science, observatories, experiments, and theory can expose difficult research without a crisis.

This distinction is essential: need should shape the tree without making civilization incapable of curiosity-driven discovery.

### 3. Effective Research Labs

An Effective Research Lab is a normalized scientific-capacity unit rather than necessarily one literal building.

Early facilities may equal one lab each. Later research institutes, orbital laboratories, high-energy facilities, biological research ecosystems, machine-science arrays, and alien equivalents can contribute multiple effective units.

Every directed research project has a **minimum lab requirement**. A civilization can understand a technology but still lack enough scientific infrastructure to attempt it.

Lab allocations to directed projects are exclusive.

## Directed projects and background science

A **Directed Research Program** is a major strategic program chosen by the player or AI.

The civilization always conducts broader science in the background. Early-game simplicity therefore does not imply every scientist works on one subject.

### Starting stage

**Single Priority Program** — one directed project.

### Institutional progression

Parallel directed research is itself part of the tech tree:

- **Coordinated Research Networks** (`coordinated_research_networks`) — 2 directed projects.
- **Distributed Scientific Portfolios** (`distributed_scientific_portfolios`) — up to 4 directed projects.
- **Autonomous Research Portfolios** (`autonomous_research_portfolios`) — no arbitrary slot ceiling; available lab capacity becomes the practical limit.

This replaces a permanent fixed research-slot rule with an institutional capability that grows naturally with the civilization.

## Minimum and recommended laboratories

Seed defaults:

| Complexity | Minimum labs | Recommended labs | Seed base RP cost |
|---|---:|---:|---:|
| Foundation | 1 | 2 | 250 |
| Developing | 2 | 4 | 800 |
| Advanced | 4 | 8 | 2,600 |
| Frontier | 8 | 16 | 9,000 |

Individual technologies can override these values. Prototype FTL, terraforming, interstellar gateways, ecopoiesis, and other civilization-scale projects can require substantially larger programs.

## Assigning more laboratories

Assigning more than the minimum speeds research.

Seed scaling:

- up to recommended labs: 100% efficiency per lab
- recommended → 2× recommended: 35% efficiency per extra lab
- above 2× recommended: 10% efficiency per extra lab

There is no hard maximum. A civilization can mount an enormous crash program, but coordination and specialist bottlenecks make very large programs increasingly inefficient.

## Technology availability

A possibility can require several distinct conditions:

1. prerequisite knowledge/capability
2. evidence where relevant
3. explicit Research Pressure threshold where relevant
4. species/biology/environment applicability
5. special infrastructure/materials where relevant
6. minimum Effective Research Labs
7. free directed-program capacity at the civilization's current institutional stage
8. enough RP to complete the program

Thus **known**, **investigable**, **researching**, **demonstrated**, and **mature** are different states.

## Examples

### High-gravity medicine

A population establishes itself on a 1.4g world. High-Gravity Health Pressure rises. Once the civilization understands the medical problem and the configured pressure threshold is crossed, **High-Gravity Cardiovascular Adaptation** can become Investigable. It still requires labs and RP to complete.

### Curiosity-driven physics

A civilization with strong gravitational physics and metrology may hypothesize an advanced field phenomenon even without a current military or transport crisis. No generic Frontier pressure tax blocks it unless that specific node explicitly requires pressure.

### Research bottleneck

A growing multiworld civilization repeatedly has more worthwhile projects than it can formally coordinate. **Research Bottleneck Pressure** can expose **Coordinated Research Networks**, later **Distributed Scientific Portfolios**, and eventually **Autonomous Research Portfolios**. The civilization's ability to run parallel major programs therefore grows through its own research history.

## Natural technological catch-up

There is no hidden underdog research bonus and no automatic penalty for being ahead.

A dominant navy can become complacent because current designs keep winning and military pressure falls. A weaker civilization facing those ships can experience high Fleet Loss, Weapon Ineffectiveness, Enemy Mobility, or Missile Threat pressure, plus useful evidence from wreckage and observation.

That naturally concentrates its research on deficiencies. If the leader notices the gap closing, its own pressure can rise again.

Culture, government, threat sensitivity, and institutional conservatism modify how strongly these pressures affect priorities.

## Player UI

### Early game

Keep the primary display simple:

- available research projects
- RP/year
- total Effective Research Labs
- assigned/unassigned labs
- one Directed Research project
- minimum/recommended labs
- estimated completion date

### Mid game

Add:

- lab allocation between concurrent projects
- recognized pressure values
- field competence
- specialized research institutions
- evidence requirements

### Late game

Add:

- multiple portfolios
- regional/specialized research complexes
- foreign/hybrid programs
- automated research policies
- archived/dormant branches

Never display pressure meters for fields that remain unknown; doing so would leak the hidden future tree.

## Persistence and performance

Do not simulate individual scientists.

Per civilization, keep compact state:

- effective lab capacity
- lab assignments
- active project RP progress
- pressures relevant to known fields
- evidence tokens
- field competence
- visible hypotheses/candidates
- mature technology IDs
- current directed-program stage

The universal 330-node seed graph remains static game data and is not copied into every campaign save.

Machine-readable data:

- `data/research/v1/research_economy.json`
- `data/research/v1/research_capacity.json`
- `data/research/v1/research_infrastructure.json`

The values establish architecture and relative scale. They remain subject to simulation/balance testing without changing the underlying **RP + Pressure + Labs** model.
