<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Stellar eruption integration and A-class replacements

The 12 replacement A-class rising images are imported. None duplicates an
A-class buildup image. The native game now uses persistent stellar events in
both close Galaxy and System views. Planet artwork and automatic planet rotation
are unchanged by this work.

## ENGINE CAPABILITIES ADDED / EXTENDED

Core owns deterministic stellar activity profiles, exponential event scheduling,
event identity, timing, surface coordinates, CME associations and persistence.
Engine owns reusable four-stage timeline sampling, spherical surface attachment,
curved ribbons, emissive transparency preparation, adjacent-texture interpolation,
UV flow and analytical photosphere occlusion. Application selects LOD, maintains
one shared presentation clock for both views, and provides developer controls.

### 1. Files added

- `core/include/stellar/core/stellar_activity.hpp`, `core/src/stellar_activity.cpp`,
  `core/src/stellar_activity_json.hpp`, `core/src/stellar_activity_data.hpp.in`.
- `data/stellar/stellar-activity-v1.json`.
- Engine `stochastic_timeline.hpp`, `surface_attachment.hpp`,
  `emissive_image.hpp` and `emissive_image.cpp`.
- `app/stellar_eruption_import.cpp`, native `native_stellar_eruptions.hpp/.cpp`
  and `native_stellar_activity_panel.hpp`.
- `tools/stellar-export/audit_stellar_eruptions.py` and
  `match_stellar_eruptions.py`.
- `native-tests/stellar_activity_tests.cpp` and `native_stellar_eruption_tests.cpp`.
- `assets/visual/stellar-eruptions/manifest.json` and 1,116 prepared PNGs.

### 2. Files modified

Core galaxy records, foundation DTO capture/restore, galaxy JSON and integrated
campaign runtime now carry and advance activity. Engine scene/material validation,
GPU resource accounting and the fragment shader support surface effects; embedded
SPIR-V and its compiler manifest were regenerated. Native main, System workspace,
General settings and developer controls consume the shared subsystem. CMake,
native export packaging, developer persistence tests and old celestial-renderer
tests were updated. The capability registry and architecture describe ownership.

### 3. Old flare implementation retired

Removed `NativeCelestialAppearanceRenderer::append_stellar_activity` and its
9.5–13-second presentation-time loop. The fallback star image renderer no longer
emits untracked line arches. There was no separate old persisted scheduler,
flare image set, shader or weather hook to migrate. Reusable photosphere and ring
rendering remains. A regression checks 300 presentation times for double spawning.

### 4–5. Asset audit and sequences

| Folder family | Files found | Accepted | Authored stages |
|---|---:|---:|---|
| A-Class Stars | 48 | 48 | 12 × 4, including all new rising images |
| B-Class Stars | 49 | 48 | 12 × 4; one identical extra rising copy excluded |
| O, F, G, K, M-Class Stars | 240 | 240 | 12 × 4 per class |
| Small prominence loop | 12 | 12 | Single images |
| Super Flares | 12 | 12 | Single images |
| CME | 12 | 12 | Single images |
| Total | 373 | 372 | 84 distinct four-stage sequences + 36 single images |

Normal and major flares share the 84 supplied sequences with different timing
and extent. This produces 204 registered visual sets and 420 usable
class/type/variant combinations. Generic families retain their supplied gold
palette; no class-specific artwork was invented.

Stage UUIDs differ. As authorized by the user, stages were assigned one-to-one
using silhouette/projection/color similarity, then all ten contact sheets were
visually inspected. Pairings are inferred artistic matches, not authored motion
correspondence. Every source path, original hash, decoded pixel hash, type, class,
stage, original variant ID, assigned sequence variant, mapped/complete/prepared/
used status and duplicate rejection is in the runtime manifest and audit JSON.
No unsupported folders or unaccounted source files remain. Originals are intact.

The native importer preserves existing alpha, converts black-backed emission
without a hard brightness cutoff, and filters premultiplied radiance into
256/512/1024-pixel tiers. The shader gently tapers image boundaries when the
source plume is cropped. Prominence orientation adjustments are recorded.

### 6–8. Stellar activity and occurrence rates

Profiles persist spectral class, Quiet/Normal/Active/Very active/Flare star state,
continuous score, magnetic activity, age influence, estimated rotation and
separate flare/prominence/superflare/CME multipliers. Sol uses a 25.4-day rotation
reference. Other periods are deterministic estimates, explicitly shown as such.
Older stars usually receive weaker profiles, with class-specific retention.

The base candidate rate is 3 events/day, multiplied by these weights:

| Class | O | B | A | F | G | K | M |
|---|---:|---:|---:|---:|---:|---:|---:|
| Relative rate | .020 | .025 | .040 | .180 | .300 | .450 | 1.000 |

Activity multipliers are .2, 1, 4, 10 and 20. Corresponding concurrency caps are
1, 2, 4, 6 and 8. Event-type weights make prominences common and superflares rare.
Intervals are exponential and counter-seeded, not periodic. Coherent active
regions last 3–12 simulated days, with variation in event coordinates.

Evolved supported giants use a .35 occurrence modifier; supergiants use .16,
with more prominence weight and less superflare weight. Compact objects,
substellar objects and unsupported hot/evolved classes receive no solar-style
events. These configurable constants model visible activity for gameplay; they
are not a calibrated stellar population census. Configuration is embedded at
build time from the versioned JSON.

### 9–12. Timing and CME association

All ranges below are simulated minutes, sampled independently per event:

| Type | Buildup | Rise | Peak | Decay |
|---|---|---|---|---|
| Prominence | 3–20 | 3–25 | 10–180 | 15–120 |
| Normal flare | 1–10 | 1–10 | 1–15 | 10–90 |
| Major flare | 5–20 | 5–20 | 5–30 | 30–180 |
| Superflare | 10–60 | 10–60 | 10–60 | 60–720 |
| CME | 5–30 | 15–90 | 30–180 | 60–360 |

Base CME association chances are 1%, 10%, 30% and 65% for prominence through
superflare, modified by the profile's magnetic activity and CME multiplier.
Associated natural CMEs receive child event IDs and a delayed launch timeline.
M-class magnetic confinement permits escape with configurable probability .45.
Escaping CMEs expose a timestamped `TravelingCmeLaunch` hook with position,
direction, speed and magnitude. Ship/colony space-weather damage is not implied.

### 13–17. Views, continuity and surface rendering

Both views resolve the same system/component and Core event IDs. A single
`EruptionArtwork` instance owns bounded visual playback across view switches;
System callbacks read the live campaign rather than creating replacement events.
Binary/trinary components each have independent activity.

Detailed effects fade in between projected radii 22 and 48 pixels. Offscreen and
distant stars do not decode or render detailed effects. Latitude/longitude define
a 3D radial normal and tangent orientation. The effect uses a curved mesh with
camera-relative spherical masking; near-side plasma remains visible, far-side
plasma behind the photosphere is hidden, and visible limb extensions survive.

Unequal physical stages combine smooth interpolation, continuous expansion,
bounded UV flow, intensity evolution, subtle flicker and outward CME motion.
Emissive materials preserve filament color without multiplying by direct stellar
illumination. A bounded logarithmic luminosity exposure and magnitude adjustment
preserve visibility across dwarf and hot-star hosts without extreme bloom.
Pause freezes deformation as well as
timeline progression.

High-speed presentation reserves 6/6/9/15/12 seconds for the five event families.
Events crossed entirely within one accelerated simulation tick can still be
presented when the star was already being observed. This never reschedules Core
events. Initial entry/load reconstructs physical progress; presentation slowdown
is transient and is not part of saved physical time.

### 18. Save/load

Optional per-component activity records are included in foundation DTOs and
player/developer galaxy JSON. Old saves initialize once at their current epoch;
existing records retain seeds, counters, schedules, profiles and active events.
Validation checks class/host compatibility, component count, finite parameters,
enums, variants, durations and bounded histories. Paused developer previews keep
their elapsed time. The full developer envelope test replays a 65% event with
the same coordinates and variant. No user campaign was overwritten during QA.

### 19. Developer controls

Open **DEV CONTROLS → STELLAR ACTIVITY**. Force prominence, normal/major flare,
superflare or CME; change activity, magnitude, variant, latitude, longitude and
orientation; pause, restart or scrub. Cycle spectral classes and components.
The panel shows age, estimated/measured rotation, activity, rate, event identity,
type, stage, elapsed/remaining time, coordinates and CME association.

**NATURAL QA · 25X** clears forced events on the selected host and runs the
existing simulation at 25× using its current profile. **COVERAGE · NEXT** advances
through all 420 combinations in a developer coverage galaxy. These controls
require the existing isolated developer-campaign marker and mark tool usage.
Regular gameplay never receives forced coverage events.

### 20. Performance

The scheduler uses a due-event heap and deterministic per-star random streams;
it does not scan every star every render frame. Histories are pruned to 48
retained events before scheduling (up to two newly created events may follow).
The renderer has at most six effects per frame, up to four queued image jobs,
observed/playback caches pruned to 256 entries at frame boundaries,
and 24 normal-tier or 12 ultra-tier cached images. GPU resources use the existing
shared frame/cache budgets. Quality changes mesh detail, texture size and visible
concurrency only. The live replay was a 250-system campaign, not a 50,000-system
performance certification.

### 21–22. Validation

All eight targeted tests passed: `engine_scene3d`, `stellar_activity`,
`developer_fixed_simulation`, `native_celestial_appearance`,
`native_general_settings`, `native_stellar_art`, `native_planet_materials`, and
`native_stellar_eruptions`. The latter covers 1,680 class/type/variant/quality
presentations. GPU comparisons found zero changed pixels inside the far-side
photosphere for all five types, with visible front-side and limb emission.

Validation artifacts are recorded alongside this report in the developer package.
They cover stochastic rates, rarity, 25× chunk equivalence, CME hooks, DTO/JSON and
full developer saves; all 420 combinations at all four qualities; frame-crossed
short events; LOD/offscreen behavior; attachment normals; transparent preparation;
quality persistence; retired-loop absence; and existing planet/star regressions.

The native Vulkan replay captures the same live eruption in the Galaxy and System
views, compares its ID/variant/stage/progress, advances it and changes back without
restart. Additional GPU pixel comparisons verify photosphere occlusion for all
five event types while retaining limb emission. Source and packaged A-class
replacement hashes are independently verified.

The full developer replay also deliberately exercises critical-fault reporting
before the eruption captures. Its resulting saved-diagnostics banner is QA
injection evidence, not an eruption failure or a startup condition in this build.

### 23. Asset issues and ENGINE LIMITATIONS REMAINING

- A-class rising replacements are complete. The one duplicate B-class source
  copy is excluded, with its original file left untouched.
- Generic prominence, superflare and CME variants each supply one authored
  frame. Geometry, opacity and flow animate those assets; four genuinely authored
  stages would improve shape evolution. Generic images have no class labels.
- Cross-stage identity is inferred by visual similarity. Some silhouettes differ
  substantially, so interpolation cannot reproduce a physically continuous
  authored plasma simulation.
- The [flare detail correction](FLARE_SHARPNESS_AND_TIMING.md) restores curved
  textured surfaces after the volume treatment blurred the art. It also separates
  event timing/frequency from accelerated strategic time. Full volume,
  scattering and physical radiative transfer remain unimplemented. The existing
  compositor is SDR; HDR display output is not added.
- Traveling-CME hooks are available; interplanetary propagation and gameplay
  hazards remain future consumers. Estimated stellar rotation and activity
  constants are game-model inputs rather than measured values for every star.

The same Engine attachment/timeline/material interfaces can support future
volcanic plumes, storms, impacts and exhaust without a second event simulator.
