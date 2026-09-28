# Distributed Scientific Knowledge & Regional Research Continuity

Milestone #10 extends Adaptive Research from one civilization-level scientific history into a realism-first distributed information/practice model suitable for very large, delayed, isolated, fractured, or multi-galaxy civilizations.

It does **not** create a technology tree per colony, sector, or successor state.

## Core distinction

Four things must stay separate:

1. **Scientific truth / research maturity** — what a scientific community has actually established.
2. **Codified access** — whether a local research context has usable records, data, protocols, and engineering documentation.
3. **Active practice** — whether local institutions, experts, facilities, tooling, training, and current work maintain real practical competence.
4. **Physical deployment** — whether actual devices, factories, habitats, fleets, populations, etc. exist.

A technology can therefore be historically known to a civilization while a distant or isolated region is unable to use or reproduce it locally.

## Research contexts are exceptions, not map tiles

Do not make one research object per planet or colony.

A `research_context` exists only when a region/network is scientifically different enough from the synchronized civilization baseline to justify explicit state. Typical causes:

- long communication isolation;
- censorship or information embargo;
- autonomous scientific institutions;
- major specialist concentration;
- federation membership;
- political fracture;
- intergalactic separation.

A normal colony that has the same knowledge access/practice as the civilization default needs no research context record.

Contexts merge/compact again after reconnection once meaningful differences disappear.

## Knowledge access states

Regional access is separate from node maturity:

- **Absent** — no actionable local record.
- **Reference Only** — knows the work exists but has insufficient detail for serious reconstruction.
- **Codified** — usable formal records/data/protocols exist locally.
- **Practiced** — codified access plus active institutions/expertise maintain working practice.

These are access/practice states, not alternative scientific truth states.

The physics is not re-rolled independently in every region.

## Communication-delayed dissemination

A new discovery or mature result originates in the research context(s) that produced it.

The result can then disseminate through actual communications networks.

Communications/logistics owns:

- path existence;
- one-way latency;
- relevant bandwidth limits;
- jamming/blockade state;
- physical travel time.

Adaptive Research owns what arrives and how it changes local access/practice.

### Data can move; physical practice cannot teleport

Codified records, equations, ordinary publications, and many datasets can travel electronically after real latency.

But:

- expert cohorts;
- prototypes;
- manufacturing tooling;
- operating institutions;
- hands-on training ecosystems

remain physical/tacit assets.

Receiving a blueprint packet never creates the factory or expert workforce needed to exploit it.

## No universal distance research penalty

Distance is not itself a magic penalty.

A distant region with excellent communications, archives, facilities, and staff can remain scientifically synchronized.

A nearby region under censorship, infrastructure collapse, or war can diverge badly.

Only real causes matter.

Existing causal Research Pressures already support this layer:

- `communication_delay`
- `administrative_distance`
- `political_fragmentation`
- `archive_loss`
- `research_bottleneck`

## Regional competence

The canonical competence model remains:

- theoretical;
- experimental;
- engineering.

Regional state stores only meaningful deviations from the civilization baseline.

Isolation does not automatically cause decay. Competence falls only if actual practice disappears: damaged labs, lost training pipelines, missing tooling, vanished expert cohorts, shrinking programs, etc.

Reconnection can restore theoretical/codified knowledge faster than engineering practice.

## Archive continuity

Knowledge loss requires real loss of accessible records/practice.

Destroying one archive does not erase a technology if redundant copies survive elsewhere.

Archive resilience can be local, regional, civilization-wide, or externally backed.

An isolated region with only one actionable local archive can genuinely lose access after destruction/corruption. It may retain `Reference Only` historical awareness and later recover formal knowledge from another archive.

Recovered records still do not recreate lost practical expertise.

## Political fracture and successor states

A successor state inherits research according to what it actually controls and can access:

- local archives;
- locally received recent discoveries;
- expert cohorts;
- facilities;
- tooling/prototypes;
- training pipelines;
- active projects;
- accessible external allies/archives.

It does **not** automatically receive the old empire's full technology list.

Widely replicated foundational science should overlap strongly between successors. Recent frontier discoveries and specialist practice can diverge sharply.

This makes political collapse scientifically meaningful without an arbitrary `-technology` penalty.

## Reintegration

When contexts reconnect:

1. compare revisions/archives;
2. exchange allowed codified records through real communications paths;
3. reconcile conflicting records using provenance/evidence;
4. update local access;
5. move/rebuild experts, facilities, tooling, and training only through real physical/institutional processes;
6. compact context exceptions when synchronization is complete.

Political reunification does not instantly homogenize practical science.

## Federations and allies

An alliance does not merge technology trees.

Science sharing can include:

- publication access;
- joint research;
- datasets;
- experts;
- facility access;
- licensed engineering records.

Legal rights and technical ability remain separate under the existing technology-exchange model.

## Runtime interface

Canonical machine contracts:

- `distributed_research_continuity_model.json`
- `distributed_research_runtime_extension.json`

Key input events include communication-path changes, censorship/information policy, archive changes, research-asset movement, polity fracture/reintegration, context topology changes, and external science-sharing changes.

Key queries include local knowledge access, local capability use, local field practice, pending knowledge delivery, successor inheritance preview, and context divergence summary.

## Save/performance rule

Never serialize:

- a complete research graph per context;
- a complete node-state map per context;
- a complete 36-field competence map per context;
- per-scientist research state.

Persist only materially divergent contexts and sparse exceptions.

Pending transmissions deduplicate by destination/revision and are removed after application. Old continuity events compress into summaries. Reintegrated contexts merge away when they no longer carry material differences.

## Fair-information rule

AI and player use the same local knowledge-access rules.

An isolated AI-controlled frontier cannot read a core discovery before the information actually reaches it.

A successor AI gets only inherited local research state.

Foreign regional research differences require legitimate intelligence to know.

## Offline benchmarks

`distributed_research_benchmark_scenarios.json` and `validate_research_distributed_continuity_benchmarks.py` test:

- communication-latency delivery;
- 40-year partition and reintegration;
- asymmetric successor-state inheritance;
- isolated archive catastrophe/recovery;
- 1,000-year sparse-context state growth across 120 geographic regions.

These are deterministic engineering/design guardrails, not final commercial balance.
