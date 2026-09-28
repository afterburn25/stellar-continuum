# Design Decision Log

This file records durable project decisions that should not be casually reversed in later chats. New decisions that supersede older ones must say so explicitly. This repository is public: never record exact secret-discovery probabilities, hidden artifact triggers, rare secret-AI eligibility, or intentionally undisclosed technology chains here.

## 2026-09-07 — Engine and simulation architecture

**Decision:** Godot 4.7.2 .NET + C# with a custom data-oriented plain-C# simulation core. Godot handles presentation/input/audio/platform integration.

**Guardrail:** Do not create one Godot Node per simulation entity.

## 2026-09-07 — Real-time strategy clock

**Decision:** Continuous real-time simulation with pause and modest speed levels.

**Guardrail:** If hardware cannot sustain requested speed, reduce effective simulation speed instead of accumulating unbounded backlog/memory.

## 2026-09-07 — Fair AI

**Decision:** AI acts only from legitimate knowledge: sensors, scouts, intelligence, trade/treaty information, memory, estimates, and known history.

**Guardrail:** No routine hidden access to exact unseen fleets, colonies, economy, technology, or authoritative fogged map state. Harder AI should primarily mean better decisions/planning, not huge cheats.

## 2026-09-07 — Borders do not automatically create hostility

**Decision:** Shared borders are context, not an automatic opinion penalty. Reactions depend on culture, government, history, interests, treaties, claims, resources, incidents, and perceived threat.

## 2026-09-07 — Pre-warp civilizations are meaningful actors

**Decision:** Pre-warp societies are not free territory. They can develop, remember treatment, trade, be protected/exploited/manipulated/conquered, and later become allies, rivals, or major powers.

## 2026-09-07 — Political defeat is not automatic game over

**Decision:** Vassalage, territorial loss, revolution, fragmentation, protectorate status, or loss of great-power status can become new campaign chapters.

## 2026-09-07 — Intergalactic continuation

**Decision:** Very late game can extend beyond one galaxy. Intergalactic ark/exodus projects preserve scientific knowledge while industrial capability must be rebuilt. Departed galaxies continue through compressed causal strategic simulation.

## 2026-09-07 — Hidden discoveries remain undocumented

**Decision:** Rare artifacts/discoveries/secret chains are not comprehensively listed in public docs or data. Players should genuinely discover and debate them.

## 2026-09-07 — 2050 campaign start

**Decision:** New campaigns begin January 1, 2050. This is a calendar anchor, not a claim every species shares human history.

## 2026-09-07 — Remote old powers as early buffer

**Decision:** A small number of already-spacefaring remote old powers may exist at start. They are initially non-expansionist and neutral unless provoked and remain hidden until legitimately discovered.

## 2026-09-07 — Realism-first rule

**Decision:** Prefer believable physical, logistical, economic, political, cultural, and diplomatic constraints/consequences over arbitrary game restrictions.

Examples: no abstract claim token required before conquest, no closed-border force fields, no arbitrary opinion threshold just to make a diplomatic request, no generic empire-size punishment when real logistics/administration/politics can create the challenge.

## 2026-09-07 — Conquest and claims

**Decision:** Claims are historical/legal/cultural assertions affecting legitimacy, resistance, diplomacy, and negotiation; they are not permission tokens required to invade.

Conquest difficulty comes from actual resistance, logistics, occupation, population response, legitimacy, sanctions, coalitions, insurgency, economic damage, internal politics, and history.

## 2026-09-07 — Borders are warnings, not force fields

**Decision:** Civilizations declare access rules and can warn, intercept, escort, sanction, fire, or escalate. Intruders can still violate the rule and accept consequences.

## 2026-09-07 — Late game remains a living game

**Decision:** Late-game challenge comes from history and scale rather than only inflated enemy stats: government/cultural change, fragmentation, migration, regional identity, shifting alliances, subjects, rebellions, logistics, infrastructure vulnerability, rising powers, decline/recovery, and multi-galaxy growth.

## 2026-09-07 — Design every feature for long-campaign growth

**Decision:** Every long-lived system needs bounded memory/caches/queues, cleanup/expiry, reduced-detail simulation where appropriate, save-size strategy, late-game CPU strategy, and graceful slowdown.

Old low-level history compresses into meaningful consequences rather than accumulating forever.

## 2026-09-07 — Relationships, intelligence, and history fade

**Decision:** Relationships are not permanent opinion numbers. Without interaction, trust/hostility can fade, intelligence becomes stale, records persist imperfectly, cultural memory can distort events, and distant history may become rumor/legend. Important events can persist much longer depending on lifespan, archives, culture, continuity, censorship, and significance.

## 2026-09-07 — Player-facing scale grows with capability

**Decision:** Operational scale expands approximately homeworld/local -> orbital/cislunar -> solar system -> nearby interstellar -> galactic -> multi-galaxy. Astronomical knowledge is not the same as actionable surveyed access. Mature lower layers become delegable rather than disappearing.

## 2026-09-07 — Revised human-like 2050 start

**Decision:** A human-like civilization begins with a mature homeworld, substantial orbital infrastructure, permanent lunar presence, and young Mars colony still materially dependent on homeworld supply/industry. Alien starts can be completely different.

## 2026-09-07 — Realistic in-system travel matters

**Decision:** Pre-warp travel models meaningful travel time, orbital logistics, and transfer constraints without becoming orbital-mechanics software. Human-like working assumptions include days for Earth-Moon crew transit and roughly months for suitable Earth-Mars transfers; orbital geometry/windows matter.

## 2026-09-07 — Interstellar range is logistical, not only propulsion-based

**Decision:** Prototype FTL has limited practical reach. Operational range depends on supply endurance, food/water/life support, maintenance, radiation protection, gravity management, fuel/energy, fabrication, navigation, communications, and support nodes. Players may attempt dangerous missions beyond safe endurance.

## 2026-09-07 — Gravity management is part of deep-space maturity

**Decision:** Long-duration habitation accounts for gravity/health. Use realistic solutions such as rotation, acceleration profiles, exercise, and medicine before assuming fictional gravity generation.

## 2026-09-07 — Automation increases with scale

**Decision:** Routine mature systems become configurable/delegable as civilization scale grows. Player focus shifts from projects -> systems -> sectors -> theaters/regions -> galaxy/intergalactic strategy.

## 2026-09-07 — No universal species technology tree

**Decision:** Similar capabilities do not imply identical technologies. Biology, environment, resources, culture, historical accidents, needs, and discoveries create divergent technological histories. Some civilizations may never independently discover FTL.

## 2026-09-07 — Foreign technology is not automatically usable

**Decision:** Foreign technology can be directly compatible, adaptable, conceptually useful, infrastructure-dependent, biologically incompatible, incomprehensible, dangerous, or unusable to the holder. Knowledge can depend on alien personnel, materials, biology, factories, environmental conditions, or unfamiliar science.

## 2026-09-07 — Technology as a strategic commodity

**Decision:** Technology can be traded/licensed/stolen/brokered rather than being only generic research points. Future depth can include blueprints, manufacturing rights, civilian/military restrictions, joint research, embargoes, resale, and monopolies.

## 2026-09-07 — Biological diversity

**Decision:** Carbon/water life is expected to be common, with rarer unusual-solvent carbon life, silicon-centered speculative life, and synthetic/post-biological civilizations. Habitability and biological-technology compatibility are species-relative.

## 2026-09-07 — Playable species count direction

**Planning target:** roughly 3–4 deep starts for a first demo, ~6 polished species for Early Access, and around 12 deeply differentiated major playable species for 1.0 if quality supports it.

## 2026-09-07 — Working title: Stellar Continuum

**Decision:** Canonical working title is **Stellar Continuum**. It remains pending commercial trademark/domain/social clearance. See `BRANDING.md`.

## 2026-09-07 — Planet gravity is a persistent environmental variable

**Decision:** Surface gravity comes from mass/radius and can affect health, infrastructure, launch cost, transport, migration, and ground combat. Population response can progress through acclimatization, developmental adaptation, long-term natural evolution, and medical/genetic/cybernetic intervention.

**Guardrail:** Do not reduce this to a permanent flat “high-gravity race +X% combat” modifier.

## 2026-09-07 — Adaptive Research replaces fixed visible species trees

**Decision:** Stellar Continuum uses a broad hidden **Technology Possibility Graph**. Each civilization materializes only currently known/plausible/relevant branches. The player never sees the complete future graph.

Branches can emerge from current need, basic science, experiments/anomalies, environment, resources, warfare, foreign contact, captured devices, and legitimate evidence. Capabilities remain separate from implementations.

**Guardrail:** Do not regress to one fully visible fixed universal tree or giant separately maintained fixed tree per species.

Canonical detail: `ADAPTIVE_RESEARCH_SYSTEM.md` and `data/research/v1/`.

## 2026-09-07 — Research economy is RP + Pressure + Labs

**Decision:** Research uses three separate quantities:

1. **Research Points (RP)** — applied scientific work generated by Effective Research Labs.
2. **Research Pressure** — bounded contextual need/evidence; it is not spent and does not directly produce RP.
3. **Effective Research Labs** — physical scientific capacity; every directed project requires a minimum assignable amount.

Large universal RP stockpiles should be avoided. More labs can accelerate a project with diminishing returns.

**Pressure rule:** only explicitly configured technologies are hard-gated by Research Pressure. Complexity and pressure affinities alone do not create a gate; curiosity-driven/basic science remains possible.

Canonical detail: `RESEARCH_ECONOMY.md`, `RESEARCH_CAPACITY_MODEL.md`, and machine-readable data under `data/research/v1/`.

## 2026-09-07 — Directed research starts simple and gains parallelism

**Decision:** This **supersedes the earlier wording that implied lab capacity alone allowed multiple player-directed projects immediately.**

Early civilizations formally direct **one major strategic research program** while unassigned laboratories continue diffuse/basic science.

Parallel directed research is unlocked by actual Adaptive Research nodes:

- `coordinated_research_networks` -> 2 directed programs
- `distributed_scientific_portfolios` -> up to 4
- `autonomous_research_portfolios` -> no artificial slot ceiling; available lab capacity becomes the practical limit

Parallelism therefore requires both institutional coordination and enough physical laboratory capacity.

## 2026-09-07 — Research Pressure creates contextual urgency, not rubber-banding

**Decision:** Research Pressure arises from actual conditions and can create natural technological convergence without hidden catch-up bonuses.

A dominant navy with no credible rival may become less urgent/complacent. A weaker navy suffering losses and observing superior systems can gain strong pressure and useful evidence. When the leader recognizes credible catch-up, its own urgency can rise again.

Culture/government/innovation values affect responses. A technological leader may remain ahead indefinitely if it continues investing effectively.

**Guardrail:** No hidden “behind = +research%” or “ahead = -research%” rule.

## 2026-09-07 — Early Access campaign-duration direction

**Decision:** Initial paid Early Access should target roughly **500 in-game years of officially supported meaningful simulation/content** without a hard year-based game-over. Engineering soak tests should survive at least **1,000 simulated years** without unbounded memory/save/performance failure. Later releases extend content depth further.

## 2026-09-07 — Tiered persistence instead of keeping the universe hot

**Decision:** Long campaigns should separate hot active RAM state, bounded warm summaries/caches, and cold/dormant/historical state in a proven embedded persistent datastore behind a game-owned storage abstraction. Do not build a bespoke database engine unless profiling/requirements later justify it.

## 2026-09-07 — Public Adaptive Research seed expansion

**Decision:** The public normal-research seed is designed as a maintainable multi-domain possibility catalog rather than a single monolithic tree.

Current expanded design target:

- **330 possibility nodes**
- **20 domains**
- **59 Research Pressure types**
- **15 alternative-solution sets**

Newest domains include Agriculture & Biosphere Engineering, Economic & Trade Systems, Cybernetics & Augmentation, Scientific Infrastructure & Metrology, and Megastructure & Stellar Engineering.

The catalog is static shared data; civilizations persist only their small materialized research state. Catalog changes must pass machine validation for IDs, prerequisites, pressure references, counts, solution sets, capacity references, and dependency cycles.

## 2026-09-07 — Adaptive Research emergence is event/evidence driven

**Decision:** The possibility graph is not scanned every simulation tick. Conditions/evidence update sparse Research Pressure and evidence state; pressure-band crossings, new evidence, prerequisite maturity, applicability changes, and bounded basic-science reviews wake only indexed candidates.

Applicability uses biological/civilizational capability traits rather than named race IDs. Evidence has provenance/quality/confidence and never directly grants mature technology. Enemy-relative pressure must come from legitimate observed information.

## 2026-09-07 — Functional capabilities replace hidden implementation lock-in

**Decision:** A later technology/system must distinguish **specific knowledge lineage** from **generic functional capability**.

Use an implementation-specific technology prerequisite only when the later idea genuinely depends on that implementation's knowledge. If the requirement is merely functional, use a cross-lineage capability.

Examples:

- Stable Warp can require Prototype Warp because they are the same propulsion lineage.
- Interstellar Logistics must require reliable `interstellar_transit`, not Stable Warp Drive specifically.
- Prototype Warp can require `spacecraft_construction`, not one exact shipyard technology lineage.
- Wormhole Stabilization can require `megastructure_construction`, allowing different industrial lineages to satisfy the engineering requirement.

Capabilities are scoped to civilization, compatible population/species, or colony/installation as appropriate. A synthetic population's habitation capability does not automatically make biological citizens compatible.

**Guardrail:** Do not silently force all civilizations back onto the human/default technological path through generic late-game prerequisites.

Canonical detail: `RESEARCH_MATURATION_MODEL.md`, `capability_model.json`, and `technology_grants.json`.

## 2026-09-07 — Research maturation can fail without becoming punitive roulette

**Decision:** Research progresses through Unknown -> Rumored -> Hypothesized -> Investigable -> Experimental -> Demonstrated -> Engineering -> Mature/Archived.

Ordinary established engineering can experience setbacks or partial success but cannot randomly become physically impossible. True hypothesis nodes can be supported, refined, disproven, or produce anomalous results.

A disproven hypothesis becomes **Archived with resolution `disproven`**. The civilization keeps negative knowledge, field competence, and possible side discoveries; all RP is not magically erased and the same exact failed hypothesis should not immediately reappear.

Explicit hazardous research can create incidents, but hazard risk must be intentionally attached to the research profile rather than automatically applied to all advanced technologies.

Side discoveries can create evidence, hypotheses, field competence, or reduced uncertainty on legitimately related possibilities. They never directly grant an unrelated mature technology or bypass applicability/prerequisite rules.

**Guardrail:** No universal “research roll failed, lose everything” mechanic.

## 2026-09-07 — Research knowledge and physical deployment are different

**Decision:** Completing research can establish knowledge and enable construction/deployment, but it does not automatically create a population, factory, fleet, institution, or other physical object merely because the civilization knows how.

For example, Synthetic Cognition and Whole-Mind Emulation can enable persistent machine cognition, but the civilization gains `machine_cognition_present` only after persistent autonomous machine cognition is actually instantiated.

## 2026-09-07 — Field competence is theory + experiment + engineering

**Decision:** A civilization does not have one universal technology-level or one generic research-skill number.

Relevant knowledge fields maintain sparse active competence across:

- theoretical understanding
- experimental practice
- engineering/manufacturing practice

Competence grows from actual work. Limited transfer to explicitly related fields is allowed, but direct experience remains much stronger.

Active competence may atrophy if institutions/practice disappear, while archived historical knowledge remains known. Rebuilding lost practice is easier when records/training/institutions survive.

**Guardrail:** High competence does not reveal unknown technologies or substitute for missing evidence/applicability.

## 2026-09-07 — Specialized research infrastructure is physical capacity, not a bonus stack

**Decision:** Specialized scientific institutions provide eligible Effective Research Lab capacity and concrete experimental/prototyping capabilities.

If a project genuinely requires high-energy experimentation, xenoscience containment, planetary environment simulation, or large-scale prototyping, missing that facility can block the relevant stage with a clear explanation.

Do not model institutions primarily as `+10% research` buildings.

Construction/economy workstreams own how facilities are physically built/costed; Adaptive Research owns what research capability they provide/require.

## 2026-09-07 — Tacit knowledge matters for technology transfer

**Decision:** Blueprints are not equivalent to complete reproducible technology.

Research can depend on strategically meaningful aggregated knowledge assets such as:

- codified records
- datasets
- experimental protocols
- intact prototypes
- production tooling
- expert cohorts
- operating institutions
- training pipelines

Foreign expertise can progress **Access -> Interpreted -> Codified -> Trained -> Native Practice**.

A civilization may temporarily operate captured foreign infrastructure with original specialists without being able to reproduce it independently. Losing those experts/institutions before codification/training can reduce practical capability.

**Guardrail:** Do not simulate one object per scientist; expert cohorts are aggregated.

## 2026-09-07 — Project context uses one bounded readiness value

**Decision:** This supersedes the earlier vague `contextual_cost_multiplier` concept.

Base RP represents inherent project workload from complexity/frontier depth. Civilization-specific history changes progress through **one Project Readiness value** derived from applicable:

- field competence
- facility readiness
- evidence readiness
- tacit expertise

Non-applicable components are omitted and remaining weights renormalized. Hard missing facilities/evidence/materials block or pause the relevant stage rather than becoming huge opaque RP penalties.

Research Pressure is **not** a readiness/speed input.

**Guardrail:** Avoid independent species/building/leader `+research%` modifier stacks. The final readiness efficiency is deliberately bounded.

Canonical detail: `RESEARCH_COMPETENCE_MODEL.md`, `knowledge_fields.json`, `research_competence_model.json`, `research_facility_model.json`, `tacit_knowledge_model.json`, and `project_readiness_model.json`.

## 2026-09-07 — Foreign technology uses four independent assessment axes

**Decision:** Foreign technology must not collapse to one reverse-engineering percentage.

Assess separately:

- **Understanding** — Unknown / Observed / Characterized / Principle Understood / Engineering Understood
- **Operability** — Unknown / Unusable / Origin Only / Supported / Adapted / Native Operation
- **Reproduction** — None / Component / Subsystem / Foreign-Process / Native-Process Replication
- **Adaptation** — None / Conceptual Inspiration / Interface Adaptation / Native Derivative / Hybrid Lineage

A device can be operable without being understood, scientifically understood without being manufacturable, reproducible only through alien processes/tooling, or biologically unusable while still scientifically or commercially valuable.

**Guardrail:** foreign acquisition never directly sets a native research node Mature.

Canonical detail: `foreign_technology_model.json` and `RESEARCH_FOREIGN_TECH_MODEL.md`.

## 2026-09-07 — Technology transfer is a composed package

**Decision:** Technology exchange is not one universal “sell tech” token.

A package can contain any composition of observations, scientific theory/records, experimental datasets, engineering blueprints, manufacturing/process documentation, reference hardware, production tooling, expert assistance, training, and operating institutions.

These components map into the same evidence/tacit-knowledge assets used by conquest, archaeology, and reverse engineering. Different package completeness therefore creates different scientific and operational value.

## 2026-09-07 — Technology licenses are law, not physics

**Decision:** Legal rights and technical capability are separate.

Licenses may govern internal research, operation, manufacture, modification, civilian/military use, sharing, export, sublicensing, or resale. A civilization that physically possesses knowledge and can technically violate a restriction is allowed to do so; diplomacy/law/intelligence systems model detection and consequences.

Real copy protection must arise from actual technical measures such as encryption, authentication, biological locks, or machine-identity controls, which may themselves be attacked/reverse engineered.

## 2026-09-07 — Technology has no universal fixed value

**Decision:** Technology value is buyer-specific.

Research-side value depends on legitimate knowledge of capability novelty/current need, existing alternatives, compatibility, expected research work/time saved, field readiness, facilities/materials/population compatibility, package completeness, experts/tooling, dependency/hazard risk, legal rights, scarcity/exclusivity, rival-denial value, and legitimately known third-party demand.

Technology unusable to the holder can be highly valuable to another species, enabling brokerage/arbitrage without a universal `Technology Value: 500` number.

## 2026-09-07 — Adaptive Research UI shows only the current scientific horizon

**Decision:** Unknown possibilities are genuinely absent from the UI: no grey boxes, placeholder slots, hidden future counts, or pressure meters for unknown fields.

Visible states are Rumored / Hypothesized / Investigable / Experimental / Demonstrated / Engineering / Mature / Archived.

When branches emerge, attach them near stable visible anchors, preserve viewport/zoom/selection where possible, avoid globally rearranging unrelated branches, draw links only between visible nodes, and collapse mature/archive-heavy historical branches outside the active horizon when useful.

Early 2050 UI remains simple; competence/facility/evidence/tacit detail is available on demand.

Foreign-tech UI shows Understanding / Operability / Reproduction / Adaptation separately. Exchange UI separately shows package contents, legal rights, and recipient technical ability.

**Guardrail:** UI consumes a materialized civilization research view and never scans/renders the full hidden graph every frame.

Canonical detail: `research_ui_contract.json` and `RESEARCH_UI_MODEL.md`.

## 2026-09-07 — Starting science is composed history, not a species tech tree

**Decision:** A playable civilization's initial research state is composed from **one base-era scientific profile plus reusable historical fragments**, together with biology/home-system facts supplied through the starting-civilization interface.

Fragments describe past/current knowledge, competence, institutions, pressure, evidence, capabilities, and tacit assets. They do **not** contain or precompute the civilization's future tree.

Complete starting compositions must be historically prerequisite-closed. Starting institutions with explicit enabling technologies require those technologies to be Mature.

Starting field competence combines by strongest justified component with a cap rather than additive percentage stacking.

Reference profiles are validation/balance examples, not species-specific catalogs.

**Guardrails:**

- no future Unknown nodes/placeholders in starting profiles
- no FTL hypothesis unlocked merely because the calendar says 2050
- a synthetic civilization may begin with machine cognition as an existing historical fact without pretending it followed the human Synthetic Cognition lineage
- high-/low-gravity starting histories create need/competence but do not preselect which solution branch wins

Canonical detail: `RESEARCH_START_RUNTIME_MODEL.md`, `starting_research_profile_contract.json`, `starting_research_fragments.json`, and `starting_reference_profiles.json`.

## 2026-09-07 — Adaptive Research integrates through events, queries, and materialized views

**Decision:** Other workstreams do not directly mutate the hidden research graph or civilization research internals.

Owning systems push normalized factual events/metrics such as shortages, environmental burdens, combat observations, evidence acquisition, research-facility changes, population applicability changes, deployments, and foreign-asset access.

Other systems consume stable queries such as capability checks, visible technology maturity, blockers, eligible research capacity, foreign-tech assessments, technology-package utility, and the materialized research view.

Functional consumers normally ask **whether a capability exists**, not which technology supplied it.

The UI consumes a read-only visible-only projection; UI/AI commands are requests that authoritative research revalidates against current visibility, labs, facilities, evidence, pressure, capabilities, coordination, and applicability.

**Performance guardrails:** static catalogs/indexes are shared, not copied into every civilization save; no full-graph per-tick scan; no per-frame view rebuild; starting-profile composition is initialization-only; dormant civilizations may use coarser research updates.

Canonical detail: `research_runtime_contract.json`, `research_view_model_contract.json`, and `RESEARCH_START_RUNTIME_MODEL.md`.

## 2026-09-07 — Persistent Adaptive Research workstream ownership

**Decision:** The dedicated Adaptive Research chat owns persistent branch **`dev/adaptive-research`** for technology/research development.

Other concurrent branches may read and consume research interfaces/capabilities but should not independently edit the canonical research graph/schema files while this workstream is active without coordination. See `WORKSTREAMS.md`.

Research milestone PRs merge this persistent branch to `main` after research validators plus normal .NET/Godot gates pass; continued research remains on the same owned workstream.

## How to change a locked decision

If the user explicitly changes a decision:

1. follow the new instruction
2. append a new dated entry explaining what it supersedes
3. update `GAME_DIRECTION.md` / relevant canonical spec / `PROJECT_STATE.md`
4. update `ROADMAP.md` when milestone planning changes

Do not silently resurrect superseded rules.
