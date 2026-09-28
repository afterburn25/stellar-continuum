# Engineering Guardrails

This file records non-negotiable engineering rules for keeping long campaigns stable, performant, debuggable, and extensible.

## Core architecture

- Godot owns presentation, input, rendering, audio, and platform integration.
- The authoritative galaxy simulation lives in plain C# data/services.
- Do not model thousands of simulation entities as Godot Nodes.
- Presentation objects are created only for things that need to be rendered or directly interacted with.
- Simulation state must remain serializable, testable, and independently benchmarkable.
- Hot paths may later move to C++/GDExtension only if profiling proves the need.

## Design for future scale before it becomes a crisis

Every long-lived subsystem must answer these questions before it is considered architecturally complete:

1. How does it behave with 10 entities?
2. How does it behave with 10,000+ entities?
3. What data can be summarized, merged, expired, or discarded over time?
4. What happens when the player's hardware cannot process the requested simulation speed fast enough?
5. What is the bounded-memory strategy?
6. What is the save/load representation at late-game scale?

Do not defer all scalability thinking until late game already exists.

## Simulation detail must scale with relevance

Different things require different update rates.

Examples:

- active combat/movement: frequent updates
- economy/population: periodic updates
- government/culture/history: slower updates
- distant inactive systems: reduced detail
- departed galaxies: compressed strategic simulation

The game should not spend tactical-detail CPU on places where tactical detail cannot affect the current player experience.

## Sustainable simulation speed

Requested game speed is not allowed to create an unbounded processing backlog.

If a machine cannot sustain the requested speed:

- preserve simulation correctness
- reduce effective simulation throughput/speed gracefully
- expose requested vs effective speed in diagnostics
- never allow queues/memory usage to grow without bound simply to pretend 4x speed is being maintained

Correct slower simulation is better than eventual instability or crashes.

## Bounded data structures

Caches, logs, diagnostics, temporary histories, and work queues must be bounded or have explicit eviction/cleanup policies.

Examples:

- route/path cache: bounded/LRU-like/selectively invalidated
- AI decision caches: expire when relevant state changes
- diagnostics event buffer: bounded
- logs: rotated/bounded
- notifications: expire/archive
- pathfinding queue: bounded/backpressured
- combat temporary state: discarded after meaningful history is recorded

No subsystem should be able to grow forever merely because the campaign lasts forever.

## History compression

The game may run for thousands of in-game years. Do not keep every minor event forever.

Use layered historical detail:

- recent events: detailed
- medium-age history: summarized but still queryable
- old history: compressed into major events, relationships, grievances, cultural memory, and uncertainty
- ancient history: may survive only as legends/rumors if archives/cultural continuity do not preserve reliable records

Preserve consequences, not every low-level event.

For example, an ancient war needs its major participants, outcome, territorial changes, casualties/impact, treaty, and persistent grievances. It does not need every projectile or destroyed scout ship forever.

## Relationship/intelligence aging

Diplomatic and intelligence systems should naturally support stale data.

Store observations with age/confidence rather than treating old information as eternally current.

Relationships can decay from active interaction to historical memory to rumor/legend. This both supports realism and prevents the simulation from requiring detailed active relationship processing for every pair of civilizations forever.

## AI scheduling

- AI does not recompute full strategy every frame.
- Different decision categories run on appropriate schedules.
- AI focuses expensive reasoning on relevant known entities: neighbors, rivals, allies, trade partners, major threats, strategic targets.
- A civilization on the far side of an unknown galaxy should not consume the same AI budget as a known neighboring power.
- Expensive calculations are cached/reused until materially invalidated.
- AI remains fair-information: no hidden authoritative state behind fog of war.

## Adaptive Research scalability

The universe-scale Technology Possibility Graph is **static shared game data**, not a per-civilization live object graph.

Never duplicate the full public/secret possibility catalog into every civilization's runtime state or save.

Each civilization should retain only compact state such as:

- mature/known technology IDs
- currently visible hypotheses/candidates
- active research programs and RP progress
- effective Research Lab capacity/assignments
- Research Pressure values relevant to already known fields
- evidence tokens
- field competence/priorities
- recent meaningful research events

Candidate discovery must be **event/index driven**, not a full scan of every technology every simulation tick.

Build shared indexes from static research data, for example:

- prerequisite -> child candidates
- pressure type -> candidate nodes
- evidence type -> candidate nodes
- applicability/trait -> candidate nodes
- field/solution family/capability -> candidate nodes

Re-evaluate research possibilities only when relevant state changes, such as:

- a prerequisite matures
- evidence arrives
- a Research Pressure crosses a meaningful threshold band
- biology/applicability changes
- research institutions/funding priorities change
- a low-frequency periodic research review is due

Unknown possibilities must not generate per-civilization timers/objects simply because they exist in the master catalog.

Research Pressure should be stored only for meaningful known/current contexts or compactly aggregated by pressure type; expired/inapplicable detail can decay/archive.

Research Labs are effective capacity units, not one simulated scientist/object per real laboratory employee. Later mega-institutes or synthetic research networks can contribute multiple effective units without increasing entity count linearly.

The public catalog must pass `scripts/validate_research_catalog.py` in CI. Once node IDs are used by supported saves, treat them as save-facing stable identifiers and migrate rather than casually renaming/deleting them.

## Population representation

Never simulate one object per individual person.

Use aggregated cohorts/groups suitable for gameplay, such as:

- species
- culture
- location
- economic role/class where needed
- political affiliation where needed

Groups may split/merge as gameplay requires. Billions of people must remain computationally manageable.

## Fleet/ship representation

Strategic AI operates at fleet/task-force level where practical.

Individual ships may exist as state, but do not give every ship empire-level AI.

Large homogeneous groups can use aggregated processing when exact per-ship detail is not relevant, especially outside active combat.

## Pathfinding/logistics

- Calculate routes on demand rather than precomputing every route between every system.
- Cache useful routes within strict bounds.
- Invalidate selectively when infrastructure, access, hazards, or topology changes.
- Logistics networks should be represented in a way that scales with regions/routes/supply nodes rather than producing all-pairs work every tick.

## Multi-galaxy simulation

Leaving a galaxy does not mean the old galaxy is frozen, but it also must not remain fully tactical forever.

Departure workflow should eventually include:

1. exact departure snapshot
2. compressed causal strategic simulation while away
3. major wars, collapses, alliances, revolutions, tech progress, population/economic change
4. reconstruction of detailed current state when the player returns
5. historical summary of meaningful changes

This is required for multi-galaxy scalability.

## Save architecture

- Saves are versioned.
- Migrations are preferred over casually invalidating test/player campaigns.
- Save writes should be atomic with backup/recovery where practical.
- Do not serialize reconstructible caches or temporary presentation state.
- Large histories should be summarized/compressed.
- Save format changes must be tested against prior supported versions.
- Never promote a development version to authoritative baseline until the actual migration/load path and runtime gate pass.

## Diagnostics/support

Diagnostics are first-class product infrastructure, not an afterthought.

Support data should eventually include:

- game/build version
- campaign/session ID
- galaxy seed
- current in-game date
- OS/CPU/RAM/GPU/API
- requested/effective simulation speed
- simulation tick timings
- entity counts
- queue/cache sizes
- memory
- recent meaningful errors/events

Provide player-facing actions such as:

- Export Support Bundle
- Open Logs Folder
- Copy System Information

Support bundles should avoid unnecessary personal information.

## CI / validation gate

Current development discipline uses:

1. checkout
2. Adaptive Research catalog structural validation
3. .NET restore
4. Release build plus the Debug assembly loaded by the ordinary Godot project runner
5. pinned official Godot 4.7.2 .NET Linux download
6. SHA-256 verification
7. headless Godot editor/project-load smoke test with captured output and engine-error rejection
8. headless runtime smoke test requiring the integrated campaign entry point to finish startup and process its first frame, with engine/script/managed-error rejection across the full captured output

Godot can return exit 0 after failing to instantiate C# scene scripts (issue #61). Process success alone is not runtime acceptance. Keep the Release build gate, build Debug explicitly for the ordinary runner, and require both clean smoke output and the positive `STELLAR_RUNTIME_READY IntegratedMain` marker. Smoke log regressions must continue rejecting missing markers, missing/empty logs, and errors appearing before or after the marker. CI retains the editor/runtime logs for diagnosis; benign warning lines do not suppress actual errors.

A milestone is not authoritative merely because source/data was written. It becomes baseline only after the validation gate passes and the branch/PR is intentionally merged/accepted.

## Stress benchmarks

Maintain synthetic stress scenarios throughout development. Initial benchmark families should include at least:

- 500 systems / 30 civilizations / 1,000 colonies / 10,000 ships

Then scale upward as features mature, for example:

- 2,000 systems / 75 civilizations / 5,000 colonies / 50,000 ships

These are engineering probes, not promises of final supported limits.

Track simulation milliseconds/tick and subsystem costs, not only rendering FPS.

Adaptive Research stress tests should later include many civilizations with divergent long-lived trees to ensure active research-state growth remains bounded even when the static possibility graph becomes much larger than the initial 244-node seed.

## Cleanup philosophy

Prefer continuous/incremental maintenance to rare giant cleanup pauses.

Safe cleanup opportunities include:

- periodic maintenance ticks
- save boundaries
- war/combat resolution
- civilization destruction/merger
- galaxy departure/return
- expiry of stale intelligence and temporary work
- research-pressure/evidence aging and archival of mature research detail

Do not indiscriminately clear useful caches; use bounded/selective eviction informed by diagnostics.

## Public repository / secret-content boundary

The repository is public.

Engineering and public design docs may describe the deep-discovery framework, but must not publish:

- exact secret triggers
- exact rare probabilities
- complete hidden artifact chains
- hidden special-AI eligibility details
- secret crisis chances/outcomes intended for player discovery

Public Adaptive Research data follows the same rule. Secret/rare research content can use the same runtime schema from a separate non-public/obfuscated content source later.

## Development continuity discipline

After every accepted milestone or major design change:

- update `PROJECT_STATE.md` if baseline/branch status changed
- append to `DEVELOPMENT_HISTORY.md` after validated gameplay merges
- append/update `DECISION_LOG.md` for new locked design decisions
- update `GAME_DIRECTION.md` if a principle changes
- update `ROADMAP.md` if milestone direction changes
- update `ADAPTIVE_RESEARCH_SYSTEM.md` / `RESEARCH_ECONOMY.md` when research architecture/economy changes
- update `CHAT_HANDOFF.md` if the canonical reload file list changes

These records are part of the development process and should not be allowed to become stale.
