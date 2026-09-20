# Technical debt — 2026-09-20

Current C++ branch only. [Known issues](KNOWN_ISSUES.md) tracks reproducible
failures; [roadmap](ROADMAP.md) tracks expansion. These are evidence-based gaps,
not permission to replace working systems wholesale.

| Debt | Evidence / owner | Consequence and safe next action |
| --- | --- | --- |
| Large application orchestration | `app/native_client/main.cpp`, `native_campaign_session.cpp`; many header-defined screens and collectors | Separate lifecycle/input/render adapters incrementally, keeping commands and snapshot contracts. |
| Repeated native compilation | Root CMake compiles the same workspace sources into many test executables | Extract well-scoped libraries/object targets; measure clean build time and preserve test isolation. |
| Foundation is not unified World | `EntityRegistry` exists; Core systems/bodies/fleets retain separate typed records and IDs | Design adapters/generation-safe handles before any ECS migration. Save identities must survive. |
| Domain-specific scheduling | `CampaignFrame`, strategic/tactical/developer clocks and service loops | Add explicit phase contracts and simulation LOD incrementally; preserve deterministic debt/pause behavior. |
| Renderer/UI coupling | `native_map_platform.cpp`, scene adapter and App workspaces | No render graph or general retained UI framework. Avoid renderer-local simulation and duplicated layout/state rules. |
| Content import duplication | Python/C++ importers, loose export manifests and cooker recipes | Loose export still requires unsampled cloud/thumbnail maps; cooker excludes them. Consolidate contracts before deleting inputs. |
| Asset cook increases current release size | Full mip chains and many lossless quality fallbacks; [cook report](ASSET_COOKER_REPORT.md) | Optimize formats/cropping only with image-quality evidence; do not trade away ring/flare/sky detail to meet a size target. |
| Fixed residency limits | Image queues and scene caches are bounded, not automatic GPU memory budgets | Add measured VRAM pressure, texture streaming and budget policy without removing admission accounting. |
| Expensive persistence | Large Player17 JSON snapshots, validation and migrations | Measure 25k/50k save/load memory and latency; design incremental snapshots with compatibility tests. |
| Historical parity baseline drift | Expanded generation payload vs old fixture digest | Review semantic differences before updating expected fingerprints. Do not replace hashes just to make tests pass. |
| Outdated Python test scaffolding | Fake runtime roots lack newly required planet manifests; galaxy-art key and Sol ordering assertions also fail | Update fixtures/identity assertions after semantic review and preserve failure-detection coverage. |
| Incomplete CI ownership | Native push filters omit `cpp/**`; retired surface targets persist | Establish one native CI contract, keep legacy parity generation explicitly separate. |
| Historical architecture remains in tree | `src/`, `tests/`, `scenes/`, `project.godot`, `.csproj` and older docs/workflows | DEPRECATED runtime; preserve useful fixture provenance. Remove only after dependency/ownership audit, never revive as current architecture. |
| Hand-entered presentation/physical tuning | Chart orbit scales, visual counts, quality limits and registry parameters | Keep unit/ownership comments; avoid claiming observational ephemeris accuracy or calibrated astronomical rates. |
| Full audio decode | Windows media decode retains current clip PCM with bounded output queues | True incremental streaming and spatial buses remain future work. |
| Partial diagnostics | Local reports/dumps, phase timings, bundles; no integrated GPU timeline or allocator census | Add symbols-aware analysis/profiling interfaces; never upload user diagnostics implicitly. |
| Release distribution limitations | Unsigned offline dev setup; exact-base whole-file updates | Signed metadata/network update service and post-success historical rollback are unfinished. |
| Whole-game late-game evidence incomplete | Scoped 50k spatial/generation/save tests and colony benchmarks | Benchmark combined AI, research, fleets, combat, assets and autosave. Isolated speedups are not a global lag guarantee. |

No broad refactor or obsolete-data deletion was performed during the handoff
audit. Existing intentional removals were preserved and published. Original
source art, local test evidence and user saves remain outside source control.
