<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> Godot/C#/.NET references below are legacy implementation or fixture provenance,
> not the current runtime or instructions to restore it.
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](../AGENT_HANDOFF.md) and
> [verified project state](../PROJECT_STATE.md).

# Stellar Engine migration contracts — 0.1.0 foundation

Native process diagnostics are owned by Engine `RuntimeDiagnostics`. The client
supplies immutable presentation context and versions; the service has no Core
dependency, writes only local bounded reports and never changes saves. It starts
before application initialization and outlives worker teardown. Windows fault
and termination paths use a preopened report handle and best-effort minidumps.

Asynchronous cooked image jobs reserve complete retained output through
`image_decode_output_bytes`. This reads asset metadata and follows the decoder's
selected-mip policy. Loading budgets must account for the mip tail and any
decoded base fallback, not only width times height times four. Quality and
resolution selection remain caller-owned.

Changed-files update manifests retain the complete target inventory, with a
separate exact base identity and included-payload path list. Existing maintenance
planning validates omitted installed files before opening a transaction; staging,
rollback, repair and registration remain shared with full release setup.

Windows maintenance is a separate native tooling layer. The Engine owns strict
numeric `ProductVersion` comparison and `RuntimeDirectoryLease`; the native
game takes its installation lease before mounting content and refuses an
unfinished transaction. `stellar_maintenance` owns release validation, confined
file planning, staging, durable recovery and uninstall. Its injected `Platform`
boundary owns OS registration, shortcuts, running-process checks and snapshots.
Only verified content can publish a new installation version. The UI consumes
this state on a worker thread and never duplicates maintenance rules. Core
simulation and save schemas remain unchanged. All setup/game PE versions derive
from `export/runtime-config.json`. See [Windows maintenance](../WINDOWS_INSTALLER.md).

Performance queries remain inside Core: `SurveyOperationsBatch` derives the
same canonical profiles once per planning request. `SettlementBodyIndex` indexes
only requested `(system_id, body_id)` pairs and supplies single-body spans to
existing economic/biology rules. Neither owns or serializes a second copy of
world state. World storage and identities remain fixed for the scope; callers
create a new scope after edits. First-match ordering, missing-body errors and
the original accumulation order remain unchanged. See
[performance evidence](../PERFORMANCE_AUDIT_20260920.md).

Engine image consumers declare whether they need a full mip chain or only CPU
pixels. `PixelsOnly` performs one selected-level registry read; RGBA8 moves that
buffer into the image, while compressed formats decode that single level.
Full-chain RGBA8 images expose their immutable first mip directly to CPU
consumers. GPU uploads still use the same pixels/formats and mip selection.
The 2D residency ledger adds retained CPU bytes to its actual RGBA upload size,
rather than doubling compressed/mip-chain storage. Existing bounds and eviction
policy remain in effect; this ledger is not a driver VRAM measurement.

The Engine asset registry is the shared filesystem boundary for cooked runtime
content. Native definitions, image/font/audio loading and research catalogs use
stable aliases resolved by one immutable checksummed index. A shipping marker
requires successful mounting and disables loose-source fallback. Core catalog
validation checks virtual resources, not physical source-directory existence.
The registry owns bounds/checksum validation and synchronized bounded failure
history; consumers retain their asynchronous preparation queues and cache limits.
2D backdrops release unused cooked mip copies before queue admission. Lossless
3D sky inputs use unchanged RGBA pixels and existing GPU mip generation, keeping
their preparation reservation equal to the loose source path. Compressed planet
and moon materials still retain indexed cooked mip tails for direct upload.

Offline cooking follows reviewed allowlists and state overrides, pins source
hashes, fingerprints recursive dependencies and produces deterministic per-mip
products. Runtime-import hashes identify derived native rasters separately from
their source masters. Cache chunks and package generations publish atomically;
the manifest publishes last. Deduplication shares exact chunks without merging
game identities. GPU texture formats and package codecs are separately tagged;
unsupported GPU formats have a decoded RGBA path. No save schema or gameplay
simulation changes are introduced by packaging. See [asset cooker](../ASSET_COOKER.md)
for interfaces, build/verification commands and remaining optimization scope.

Core's `planetary_satellites` owns canonical moon identity, mean physical
elements, procedural Kepler periods and parent relationships. Engine supplies
unit-independent framed orbit evaluation and orthonormal rotation construction.
Observer-safe snapshots carry the same immutable orbit model into native paths,
positions, tidal pose and light/shadow calculation. Chart scaling and bounded
depth remain application presentation; they never feed back into simulation.
Legacy Sol migration appends missing reserved bodies idempotently, retaining
existing planetary state. The globe and system share canonical material IDs.
Cosmetic spin uses real elapsed time around a fixed individual axis; locked
faces use the parent-relative orbital vector. Selection starts camera tracking
without creating an alternate clock or orbit integrator.

Local starfield selection now admits only the audited faint map reference.
Physical galactic environment metadata is preserved independently. The map and
system share one display tint while source pixels remain immutable. Runtime
asset allowlists exclude rejected skies and include the 54 new moon maps. See
the current moon/sky reports and capability registry for limits and evidence.

The [Sol visual correction](../SOLAR_VISUAL_CORRECTIONS.md) places local sky
policy in Core `SystemBackgroundProfile`. Canonical clear presets suppress both
cloud images and their transmission factor in system/battle consumers; they do
not rewrite galaxy simulation phenomena. Clear entry drops obsolete composites
without waiting for invisible layers. All gaseous appearance migration resolves
approved replacement materials before renderer selection, preserving physical
state and existing valid rings. Sol belt reconciliation changes only bounded
visual representative counts. Engine's optional `cubic_magnification` flag is
shared with the shader uniform contract, applies only during magnification, and
clamps interpolation to local samples to prevent bright halos. Default sampling,
queue ownership and save schema remain unchanged.

The [giant/ring pipeline](../GIANT_RING_IMPLEMENTATION_REPORT.md) keeps approved
identity, family eligibility, thermal/Roche rules and legacy migration in Core.
Engine owns oblate geometry, finite annular slabs, radial/angular source
preparation and analytic two-way shadow sampling. Native System/Planet views use
the same saved appearance and immutable assembly. The developer laboratory is
an authoritative Core transaction isolated in developer provenance. Ring
thickness extends the existing annulus API; zero thickness preserves its old
callers. Source art and rejected pools never enter generation implicitly.

The [system starfield pipeline](../STARFIELD_IMPLEMENTATION_REPORT.md) selects
profiles in Core from the saved galaxy seed, systems, structural metadata and
phenomena. The frozen v1 inputs/manifest reconstruct identity deterministically;
future rule changes must version that contract. Engine celestial projection is
independent of system translation/zoom. Native loading and debug controls use
the shared preparation queue, bounded caches and background-first composition.
Generated cloud masks render above the sky; foreground objects are drawn later.
System and battle entry gate on environment readiness. No view generates new
galaxy density, nebulae or background identities privately.

`RgbaImage` optionally carries a validated complete opaque BC1 mip chain plus
RGBA fallback/reference pixels. The GPU owner thread checks device format
support, uploads either the compressed chain or normal RGBA mip path, and
accounts the actual GPU allocation. Low/Medium starfield quality opts into
compression; High/Ultra and all ring materials retain their original format.
This reusable Engine boundary introduces neither an unbounded cache nor an
alternative renderer.

The [ring sharpness correction](../RING_SHARPNESS_REPORT.md) keeps ellipse
rectification in Engine's offline material preparation, preserving both radial
and angular source detail. Application selects the immutable material LOD and
opts ring receivers into bounded anisotropic sampling. GPU owner-thread sampler
and mip ownership stay in Scene3D; no Core rules or save data change. Native
system close allocations are bounded for the larger materials.

The [flare correction](../FLARE_SHARPNESS_AND_TIMING.md) selects Engine curved
emissive surfaces instead of the rejected image-volume treatment. Texture detail,
surface masking and cached geometry remain shared between Galaxy and System.
Core owns the independently persisted `StellarActivityDay`; CampaignFrame advances
it once per running strategic frame from unscaled real seconds. Strategic ticks
cannot multiply eruption rate. Native rendering and developer controls sample
that same clock. Both Galaxy JSON writers preserve it and old payloads initialize
at their saved epoch. CME launch records now belong to activity/frame advancement.
Arrow label baselines follow the broad edge with collision bounds in that basis.

Core planet appearance restoration accepts the frozen legacy v1 class-ordinal
mapping alongside canonical string IDs. Ordered and ordinary JSON writers emit
strings; readers reject unknown, fractional and out-of-range values. This repairs
existing developer autosaves without modifying source files, rerolling artwork or
introducing view-specific save conversion.

Native Galaxy label layout accepts finite positive projected obstacle footprints
even outside the bounded viewport. It intersects them with that viewport in
double precision before collision checks and drops invisible footprints. Distant
central objects at deep zoom therefore cannot exceed label-space bounds and
terminate the client. This presentation-only contract changes neither Core state
nor camera zoom limits, save data, resource ownership or simulation timing.

The [visual motion extension](../STELLAR_VISUAL_MOTION_UPDATE.md) adds optional
bounded emission volumes and cached text rotation to Engine. Application supplies
authored imagery, measured photospheres and quality budgets; Core retains event
timing and uniformly sampled event sites. One real-time presentation clock drives
slow free planetary spin in both views. Core owns optional persisted tidal-lock
metadata and legacy resolution; parent-facing orientation consumes the existing
orbital projection. Camera tracking and fit-relative magnification remain native
presentation state. These changes do not add view-owned orbital simulation or
alter imported texture pixels. Live spin supersedes the earlier static-pose
policy; list portraits retain their reference views.

The [stellar eruption extension](../STELLAR_ERUPTIONS.md) separates authoritative
Core profiles/events from Engine timeline/attachment/material primitives and
application LOD/playback. The runtime advances one due-event scheduler; existing
galaxy DTO/JSON carries optional per-component activity. Both native views resolve
the same campaign events, including companions. Graphics quality, async texture
readiness and minimum visual duration never create or reschedule physical events.
`TravelingCmeLaunch` is a future consumer boundary; no hazard simulation is hidden
in presentation. Surface-effect textures count against existing scene/GPU budgets.

The [Sol orientation correction](../PLANET_ORIENTATION_FIX.md) shares established
reference viewing poses through the canonical native planet material boundary.
Physical saved axis/node/phase fields remain Core metadata, not camera roll.
Both live views and portraits consume the same pose without changing the Engine
transform API, source maps, save schema or resource ownership.

The [lighting/orbit/time extension](../LIGHTING_ORBITS_HOURLY_CLOCK.md) keeps
versioned companion dynamics, planet host bindings and stability rules in Core.
Generation, restore, insertion and native presentation share those records and
analytic Engine orbits. Application owns compressed chart spacing and readable
exposure; Engine owns opt-in linear-light shader/thumbnail evaluation. Native
sessions configure Core's shared clock for an hourly base cadence without changing
the game-day accounting unit or tactical clock. Existing primary-centred fleet
navigation remains a separate limitation, not a consumer of companion gravity.

The [orbital/artwork correction](../ORBIT_ART_TUMBLE_FIXES.md) keeps physical orbital
clearance and legacy cold-world repair in Core, called at generation, restore and
developer insertion. Application uses one shared monotonic projection for planet
and belt positions and their guides. Cosmetic asteroid tumble has a separate,
capped presentation clock and never affects simulation or extraction. Source-facing
planet poses, species-based index filters and stable stellar texture admission reuse
the existing material, Core habitability and bounded image-queue contracts.

The [static planet correction](../PLANET_STATIC_ART_REPORT.md) keeps cloud-free,
fixed-pose presentation in Application, shared by both live views and portraits.
Core retains physical spin and atmosphere metadata without save migration.
Per-object stellar zoom blends use presentation time and bounded owner-thread
state; they do not enter simulation or create additional GPU ownership.

The [texture filtering extension](../PLANET_TEXTURE_FILTERING_REPORT.md) keeps
full mip chains within the Scene3D adapter. A shared generic layout function
accounts for retained CPU base pixels and all GPU levels before scene/frame
admission. Generation occurs once per immutable upload on the GPU owner thread;
trilinear samplers serve all existing consumers. No Core or save changes occur.

The [oriented portrait extension](../PLANET_PORTRAITS_REPORT.md) adds a worker-safe
CPU ellipsoid/annulus preview to spherical material preparation. Immutable maps,
pose and geometry enter through generic options. Application shares canonical
pose/radius helpers with live planet assembly, while the existing bounded worker
cache owns generated icons. There are no simulation, save or GPU ownership changes.

The [mutual ring shadow extension](../PLANET_RING_SHADOWS_REPORT.md) adds generic
ellipsoid/annulus occlusion to immutable `Material3D` descriptions. Application
supplies canonical planet/ring transforms and existing alpha resources; Engine
prepares blocker-relative geometry and light without consulting gameplay state.
Only direct illumination is attenuated. Shadow resources participate in existing
scene/frame admission and GPU-owner-thread uploads. No save or clock changes.

The [canonical planet extension](../PLANET_ART_IMPLEMENTATION_REPORT.md) adds
generic spherical-material preparation, normal/property/relief response,
cloud-shadow sampling, rim shells and temperature-derived illumination to Engine.
Core owns the 16-class/65-subclass taxonomy, physical eligibility, saved appearance
and developer example commands. Both native planet consumers share one immutable
material provider; a bounded owner cache publishes results from one CPU worker.
No planet-generation rules enter Engine, and GPU resources remain renderer-owned.
Missing legacy appearance records migrate without rewriting the old environment.

The [resolved planet registry](../PLANET_TYPE_REGISTRY.md) keeps subclass rules and
image eligibility in Core. Normalized type definitions and accepted/rejected art
metadata resolve once into immutable `PlanetTypeRecord` entries. Generation,
developer inspection and the catalog exporter consume those entries; Application
does not duplicate classification rules. Orbital bands derive from equilibrium
temperature at the actual star, with surface climate/heat and retention checks
applied separately. Rejected IDs retain provenance but never provide runtime maps.
Selection remains seeded and existing saved appearances remain unchanged.

The [ice optics extension](../NATIVE_ICE_OPTICS_REPORT.md) adds optional generic
dielectric materials to the existing immutable Scene3D boundary. Engine validates
optical coefficients and counts surface/environment resources in per-scene and
combined-frame budgets before allocation. Shaders evaluate view-dependent Fresnel,
GGX reflection and Snell environment refraction with absorption. Application owns
ice/frost maps and appearance selection; Core remains independent of rendering.
Existing materials retain their diffuse behavior. No extra scene target, save
schema, simulation clock, or worker-side GPU ownership is introduced. Environment
refraction does not imply ray-traced interactions between scene objects.

Status: foundation in progress, not full game migration. Normal gameplay expansion is paused until the migration completion gates pass. Baseline: integration `97091aee84b782bdc917185307cd42b97b8fd7d0`, Stellar Continuum 0.1.7 Alpha, tag `migration-baseline/stellar-continuum-0.1.7`. Pending feature branches are preserved separately. Never remove Godot implementation based solely on a compiling native placeholder.

The sections below record the original foundation contracts and gates. Current
native extensions are tracked in [the capability registry](../ENGINE_CAPABILITIES.md).
The [native C++ 3D extension](../NATIVE_3D_ENGINE_REPORT.md) now implements immutable
mesh/camera/material submission and GPU depth rendering alongside the existing
SDL/Vulkan 2D adapter, with the planetary globe as its first real consumer.
The [3D consumer and scale follow-up](../NATIVE_3D_SCALE_INTEGRATION_REPORT.md)
integrates ship geometry, radial terrain and generic motion/collision queries,
adds indexed large-catalog navigation and streaming atomic writes, and supports
50,000-system campaigns. The new planetary screen is the sole native colony
interface; the obsolete surface UI and fallback are deleted. Render geometry
remains presentation-only, while authoritative XYZ combat movement and optional
saved depth remain in Core. Engine contains no gameplay rules. These extensions
do not replace the remaining full-migration gates.

The [campaign loading follow-up](../NATIVE_CAMPAIGN_LOADING_REPORT.md) moves
ordered JSON parsing into Engine and adds validated deferred-array traversal.
Core selects system/body arrays and retains schema, validation and activation
ownership. Complete input bytes still remain in memory; this is not a streaming
file reader or a change to campaign save formats.

## Layers and directories

| Layer | Directory | Dependencies and responsibility |
| --- | --- | --- |
| Stellar Engine | `engine/include/stellar/engine`, `engine/src` | C++23 standard library first; platform, ticks, identity, jobs, events, logging, coordinates, renderer/asset contracts |
| Stellar Core | `core/include/stellar/core`, `core/src` | Engine; migrated game data/rules only |
| Application | `app` | Core and Engine; headless command line now, original player UI retained until real replacement |
| Native checks | `native-tests` | Maintained CTest programs, old/new fixtures and benchmarks |
| Export tooling | `tools/stellar-export`, `export/stellar-presets.json` | Development-side build/package validation; no Python/CMake/compiler requirement in exported runtime |
| Reference game | existing `src`, `assets`, `data`, `project.godot` | Preserved C#/Godot application and tests throughout parity work |

Engine must not include civilizations, research IDs, colonies or specific missions. Engine events carry generic typed payloads; gameplay event types belong to Core. There is no dependency from Engine to Core/Application.

## Foundation interfaces (shared ownership contract)

All Engine types below are in `stellar::engine`. Public header `engine/include/stellar/engine/foundation.hpp`; implementations `engine/src/foundation.cpp`. Changing these contracts requires coordinator review before consumers diverge.

- `using Tick = std::uint64_t`. A tick is a monotonic simulation sequence number. Persist tick duration with saves; do not equate a tick with a rendered frame.
- `EntityId { std::uint32_t index; std::uint32_t generation; }`, equality and `value()` returning generation in high 32 bits. Registry owns generations/free slots, validates stale IDs, retires a slot before generation wraps. `EntityRegistry::create()`, `destroy(id) -> bool`, `contains(id) const`, `size() const`. Scene-node addresses and dense array offsets are never durable identity. Import legacy game IDs with an explicit mapping later, not reinterpretation.
- `FixedClock(std::chrono::nanoseconds step)`, `set_paused(bool)`, `set_speed(std::uint32_t)` (1..64), `advance(elapsed,max_ticks=4096) -> std::uint64_t`, `tick()`, `backlog()`. Integer wall-time accumulation multiplied by speed; paused time is discarded. Bounded catch-up retains backlog, never skips simulated time. Invalid/overflow inputs throw. No wall-clock reads inside simulation rules. Headless work can drive explicit ticks directly.
- `DeterministicRandom(std::uint64_t seed)`, `next_u64()`, `unit_double()`. SplitMix64 with fixed constants and high-53-bit conversion. This is a new engine utility, not automatic parity with System.Random. Existing galaxy generator keeps its legacy RNG until an explicit parity adapter is validated. Independent jobs receive streams derived from stable scenario/system IDs, never scheduling order.
- `EventQueue<T>::publish(T)`, `drain() -> std::vector<T>`. Simulation-owner queue; publication order retained, drain swaps the current batch so later events wait for next batch. Worker tasks return events in result buffers; owner merges by job index. No unsynchronized cross-thread publishing.
- `JobSystem(std::size_t workers)`, `submit(std::function<void()>) -> std::future<void>`, `wait_idle()`, `worker_count()`. Bounded worker count, persistent threads, queued jobs. Exceptions travel through futures. Destructor drains accepted work then joins. Dependency orchestration is owner-side barriers/futures; jobs must not synchronously wait on work in the same pool. Concurrent mutation of campaign state is forbidden: immutable inputs, partition-owned output, deterministic owner merge.
- `log(std::string_view level,std::string_view message)` writes synchronized diagnostics. `require(bool,std::string_view)` throws a useful exception. App top level catches standard/unknown exceptions, reports type/message/build/context and returns nonzero without uncaught exception dialogs. Signal/access-violation dump collection is a later Windows crash-service gate.

## Universe and persistence boundaries

Core's first real migrated component is the existing `InterstellarDistance` separation rule. `stellar::core::StarPosition { float x, y; std::optional<double> depth_light_years; }`; functions `distance_light_years(a,b)` and `squared_distance_light_years(a,b)` preserve legacy float 2D behavior when both depths are absent, otherwise double 3D behavior with missing depth=0. Golden fixtures come from the actual preserved C# implementation. This is distance parity, not galaxy-generation or full simulation parity.

Future hierarchical positions use a stable frame entity ID plus double local SI offset; resolve relative positions through a validated acyclic frame graph, then convert camera-relative doubles to floats only at render submission. Never store the entire universe in one GPU float frame. Analytical orbital propagation retains existing element/epoch semantics before extending precision. Physical light-year imports are converted explicitly; gameplay never reads presentation scale.

Native snapshots have magic, version, content/schema IDs, tick semantics, stable identity records and checksum/validation. First fixture/checkpoint format is explicitly a foundation scenario, not a loadable migration of save-v16. Keep the original JSON save reader until all state, once-only events/rewards and IDs can round-trip. No replay of completed commands after restore. Filesystem backend commits temporary files atomically and preserves a previous valid save.

## Contracts reserved for later adapters

| Contract | Boundary |
| --- | --- |
| Render submission | Immutable camera-relative instance/line/mesh batches + generational asset references; no authoritative writes |
| Assets | Stable content ID + content version/hash; explicit load state and lifetime; sources compiled into runtime manifests |
| Input | Timestamped device/window events normalized to drawable coordinates; UI capture precedes world orders |
| UI data | Immutable observer-filtered snapshots; validated commands return result IDs and costs, no direct campaign writes |
| Audio | Cue/voice IDs and gains through an audio backend; no simulation timing dependence |
| Events | Tick + stable sequence + typed Core payload, deterministic owner dispatch; save relevant pending events |
| Jobs | Read snapshot, write disjoint result buffer, merge in stable index order at a tick boundary |
| Profiling | Monotonic scoped spans and counters; tick durations separate from render CPU/GPU, no invented GPU timings |
| Filesystem/export | Scoped project inputs, explicit runtime allowlist, hash manifest, build metadata, clean-directory smoke tests |

SDL3 and Vulkan are the preferred Windows renderer/platform foundation, deferred until the headless boundary works. SDL uses zlib licensing ([official license](https://www.libsdl.org/license.php)); no SDL binary is linked in foundation 0.1.0. Review and pin each actual third-party addition with its redistribution notice. No full Vulkan SDK ships to players. Unsupported GPU/driver errors must be handled before declaring renderer parity. Scripting, general editors and graphics feature expansion remain deferred.

## First milestone and future gates

First milestone: audited baseline, these contracts, native C++23/CMake build, logging/errors, stable entities, clock, events/jobs, headless executable, real distance parity, meaningful tests/benchmarks, versioned portable headless Windows export with manifest and relocated launch. It is deliberately not a replacement playable game.

Subsequent migrations preserve rules in this order unless dependency evidence requires otherwise: galaxy/bodies, clock/civilizations, economy/logistics/colonies, fleets, research/diplomacy/AI, territory, missions/events/artifacts/combat, then renderer/input/audio/player UI parity. New framework utilities are not marked migrated gameplay.

Full integration requires every existing game-loop and save/recovery capability to run without Godot, measured acceptable performance, equivalent player UI/audio/assets/views, all regressions passing and a standalone Windows x64 release export working without development tools. Graphical presets must fail clearly while graphical parity is absent. No engine-complete announcement or production merge before these gates.

## Small-body fields and presentation-space orbits

Engine's `AnalyticOrbit` and `AnalyticSpin` are immutable, unit-independent
elements evaluated at caller-supplied time. Core supplies AU/day/mass semantics,
field seeds and saved epochs; Application passes canonical simulation days.
Engine billboard batching and opaque-cutout preparation remain generic rendering
utilities. They do not read campaign state or select resource/art categories.

Core owns `SmallBodyField`, deterministic `SmallBodyInstance` reconstruction,
versioned JSON records, generation tables, cracked-world state, depletion and
environment queries. Developer commands require campaign provenance and commit
validated records. Native legacy activation fills only absent catalogs. Explicit
empty catalogs and saved fields retain their meaning. Queries and draw preparation
must not mutate the authoritative world.

Application projects planets and fields through the same monotonic AU-to-display
mapping. Enlarged planet radii, camera transforms, artwork and LOD do not enter
simulation calculations. Render samples retain stable field/body identities across
LOD changes. Motion is analytic; no independent UI clock or duplicated simulation
is permitted. Resource/supply hooks leave cargo, access and effort transactions to
their authoritative gameplay callers. See the capability registry and
`docs/NATIVE_SMALL_BODY_FIELDS_REPORT.md` for limits and verification.

## Solid small-body views

Engine `directional_solid_mesh` accepts a caller-owned continuous directional
surface, samples immutable triangles and computes the deformed surface normals.
`Material3D` optionally overrides the scene's camera-space light direction per
object; validation and normalization precede the existing GPU draw uniform.
Existing materials preserve their shared scene lighting by leaving it unset.

Application owns the asteroid shape/material library, projected-size LOD and
presentation-size quantile mapping. Core's saved generator and resource scale
remain unchanged. A single depth-tested scene renders the visible solids, with
bounded geometry, texture and draw budgets. Frozen fields use discrete solids and
unresolved markers without image haze. Orbit and spin still read canonical time.
Solar-system zoom and its indicator read the same camera state, through 55x for
both planets and small bodies. These are presentation changes and never mutate
campaign state. See `docs/NATIVE_SMALL_BODY_3D_REPORT.md` for current limits.
