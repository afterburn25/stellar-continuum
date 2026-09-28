# Research Secrecy, Compartmentalization & Compromise

Milestone #11 defines the research-side semantics for classified science without turning secrecy into a magic technology modifier or taking over the Intelligence/Security workstreams.

## Core rule

**Classification controls access to records, assets, participants, and dissemination. It does not change scientific truth.**

A classified technology is not a separate physics state. If a capability is deployed where outsiders can legitimately observe it, they may learn that the capability exists even while its theory, design, process, and program remain secret.

## Access classes

Research uses four non-magical access classes:

- **Normal Scientific** — ordinary authorized scientific institutions/contexts in the polity can receive codified results through normal dissemination.
- **Restricted Program** — detailed records are limited to explicitly eligible institutions/roles/contexts/collaborators.
- **Classified** — protected records require security authorization and real secure storage/communications/services.
- **Special-Access Compartment** — project records are split across need-to-know compartments.

Only nondefault policies are stored. Normal scientific access does not create a security record on every technology.

## Existence disclosure

A project/record can be:

- Acknowledged;
- Restricted Metadata;
- Concealed Metadata.

This describes disclosure policy, not physical invisibility.

A concealed program can still be inferred from:

- sensor emissions;
- combat telemetry;
- debris;
- observed industrial behavior;
- intercepted traffic;
- defectors;
- captured hardware;
- leaked records;
- other real evidence.

The relevant owning systems decide what can actually be observed/intercepted.

## Secrecy never creates a flat research penalty

There is no `classified research = -20%` rule.

Secrecy affects progress only through real causes:

- fewer Effective Research Labs are authorized;
- some expert cohorts cannot participate;
- some facilities are excluded;
- independent validation may be harder when only a small authorized network can replicate an experiment;
- compartment boundaries can block engineering integration;
- protected archives/redundant sites must physically exist;
- communications/security constraints can delay specific dependencies.

If all necessary real capacity remains available, classification itself does not reduce RP efficiency.

## Compartments

Compartments should map to real separable information, for example:

- theory;
- experimental data;
- prototype design;
- manufacturing process;
- materials/feedstock;
- software/control;
- facility operations;
- deployment integration.

Possessing one compartment does not imply access to the rest.

An engineering stage that requires prototype, manufacturing, and control information needs an authorized integration context. Missing access is a concrete blocker, not a generic cost increase.

## Actual protection comes from actual security

Adaptive Research consumes factual security-service inputs such as:

- identity/access control;
- communications confidentiality/authentication;
- archive hardening/encryption;
- facility physical security;
- personnel screening/audit;
- network isolation;
- counterintelligence monitoring.

Research does **not** simulate spying, infiltration, interception, cryptographic attacks, thieves, security teams, or counterintelligence operations.

Those belong to Intelligence/Security/Communications. They report factual outcomes back into research.

## Compromise

Research only knows a leak/compromise to the degree it was legitimately detected.

Known compromise states are:

- No Known Compromise;
- Suspected;
- Confirmed Partial;
- Confirmed Material;
- Scope Unknown.

A leak can supply different pieces:

- observation dossier;
- theory;
- experimental data;
- engineering blueprints;
- manufacturing process records;
- hardware/prototype;
- production tooling;
- experts;
- training;
- operating institutions.

These route through the existing evidence, tacit-knowledge, foreign-technology, and technology-exchange models.

A stolen blueprint is not an instant technology unlock.

## Partial compromise

Example: an outsider steals an experimental dataset and engineering blueprints for Antimatter Containment but gets no tooling, operating institution, experts, or training.

The outsider may improve from **Observed** to **Characterized** and perhaps reach component-level reproduction, but still lack operability, manufacturing process, native practice, or a Mature native research node.

The exact outcome depends on real recipient competence, facilities, evidence, compatibility, and acquired assets.

## Physical observation vs secret records

A deployed classified system can expose a capability without exposing implementation.

The existing `legitimate_foreign_capability_observed` path remains authoritative.

An observer may learn:

- this kind of defense exists;
- approximate performance;
- visible limitations;
- detectable signatures.

It does not automatically learn:

- exact node identity;
- underlying theory;
- blueprints;
- manufacturing process;
- hidden dependencies.

## Declassification

Declassification changes who may receive records. It does not alter research maturity.

Newly authorized contexts still receive records through real communication paths/latency.

Declassification can improve knowledge resilience by increasing independent archive copies, but it does not automatically create practical expert/facility capability.

## Reclassification

Reclassification can stop **future** normal dissemination.

It cannot magically recall copies that are already distributed.

Deleting or recovering old records requires real physical/technical actions by owning systems.

Reclassification also cannot “un-leak” information already outside control.

## Distributed-research integration

Milestone #11 builds directly on distributed scientific continuity.

Classification becomes a sparse access exception:

- selected contexts can receive protected records;
- authorized contexts still obey communications latency;
- isolated contexts can miss classified updates;
- political fracture transfers protected knowledge according to where records/assets/experts actually are;
- a successor may know a secret program existed but lack actionable implementation records.

No complete security copy of the 360-node graph is created.

## Foreign technology / exchange integration

Protected technology still uses the existing four foreign-assessment axes:

- Understanding;
- Operability;
- Reproduction;
- Adaptation.

Deliberate release/share uses the existing technology-package and legal-right model.

Classification policy can deny authorization to share, but it is still policy rather than a physical force field. Once another party actually acquires a copy, technical copyability and political consequences are separate questions.

## UI / fair information

Authorized own-polity views can show:

- access class;
- existence-disclosure intent;
- authorized context/group summary;
- eligible classified capacity;
- known archive resilience;
- known compromise state;
- known declassification impact.

Unauthorized/foreign views only show legitimately known rumors, observations, records, and intelligence.

There is no omniscient leak icon.

Harder AI receives no special access to secret programs or undetected leaks.

## Runtime contract

Canonical machine files:

- `research_secrecy_model.json`
- `research_secrecy_runtime_extension.json`
- `research_secrecy_benchmark_scenarios.json`

The runtime extension provides seven factual input events and six queries for classification policy, security-service changes, known compromise, acquired records, classified asset control, observed classified capabilities, and declassification.

## Persistence / performance

Persist only nondefault security state.

Do not store:

- a security row for every normal node;
- per-person clearance lists;
- a duplicate technology graph;
- duplicate physical asset state.

Use group refs, sparse security records, bounded compromise history, and compaction after declassification/retirement.

## Benchmark fixtures

The secrecy benchmark tests:

1. restricted real capacity without a magic research penalty;
2. compartment integration blockers;
3. classified deployed capability observation;
4. partial blueprint compromise;
5. declassification increasing dissemination/resilience without changing truth;
6. reclassification unable to recall existing copies;
7. 1,000-year bounded security-state growth.

These are deterministic architecture regression fixtures, not final commercial balance.
