<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Planet artwork, orbital clearance and steady asteroid tumble

September 19, 2026

Imported planet globes now default to their authored front hemisphere. The saved
random phase previously froze some globes facing reconstructed material on the
unseen back. This change preserves the existing approved image files and image
IDs. Planets remain static, manual globe inspection remains available, and no
cloud layers or cloud shadows are added. Imported ring giants keep a small fixed
tilt so their rings remain visible. Inspected worlds show their canonical subclass.

The developer planet index can find existing imported artwork and unsettled worlds
naturally habitable for the player's species. These filters use authoritative
planet, colony and species data. They do not increase generation percentages or
spawn substitute examples. Navigating from the index closes the pause menu.

Core places ordinary belts in clear stellar orbital gaps, excluding the complete
eccentric orbit plus a margin for each planet. Only a shattered world's own debris
may share its orbit. Ice fields also respect the stellar snow-line boundary.
Creation, developer insertion and restored campaigns use the same placement rule.
Field IDs, seeds, counts, composition and mining depletion stay unchanged. Repair
is deterministic and idempotent. In Sol the ice field stays centered on the Sun
beyond the ordinary planetary orbital envelopes, including Pluto, following the
requested noninterference rule. It is a game-layout belt, not a precise model of
the real Kuiper belt, which includes dwarf planets and crossing populations.

The chart now sorts by actual stellar distance and applies one monotonic distance
mapping to planetary positions, orbital guides, and belt particles. Display gaps
reserve space for enlarged planets, rings, moons and large asteroids. Camera and
navigation boundaries include the complete outer belt.

Frozen primary worlds require a cold stellar orbit. At creation/load, existing
frozen primaries in warm or thermally contradictory orbits move outward to a clear
cold orbit. Their moons follow; existing environments, colonies, body IDs and art
remain. Equilibrium temperatures and heat budgets update. Already valid orbits
and the measured Sol planet catalog are preserved. This is a bounded compatibility
repair, not simulated migration, a new climate solver, or a reroll of inhabited
worlds. Atmospheric heat budgets of existing families are retained as compatibility
state. Cold moons continue to use their existing absorbed-flux and atmosphere rules.

Asteroid and ice tumble uses visible-frame presentation time, independent of the
strategic clock multiplier. Typical full turns take about 90–180 real seconds.
Seeded axes and wobble remain; pause stops tumble and orbital movement. Orbital
positions still use authoritative campaign time. A long frame contributes at most
0.25 seconds of cosmetic time, avoiding a catch-up jump. This presentation time is
not serialized and starts again when the application restarts. There are no extra
simulation substeps or per-body timers: only the visible system is updated, with
the existing 768-solid limit and cached meshes/materials. Slower visual spin does
not by itself lower the cost of shading those same visible objects.

Stellar detail slots remain assigned across frames instead of being reassigned
by draw order. Pending/old images cannot evict admitted visible types; the four
close-image/two-decode budgets remain. A slow frame no longer resets an already
resolved giant's blend. Independent stars keep their own transition weights.

Validation covers saved-campaign repair, unchanged imported source identities,
idempotence, eccentric orbital clearance, Sol display mapping, constant-speed
and paused tumble, native menu navigation, authored portraits, cloud absence,
crowded/reordered star rendering and delayed frames. The package includes the
actual regression and native smoke replay logs.

The September 19 existing-campaign audit covered 7,173 bodies and retained all
6,867 assigned imported surfaces. It corrected 82 family orbits, checked 1,473
fields for orbital clearance, and found 117 unsettled worlds naturally habitable
for that campaign's species. Repeating the repair made no further changes.
Twelve focused native/Core regression suites and eight export checks passed.
The native replay exercised Sol belts, movement/pause, eight imported planet
classes in both views, developer navigation and save/reload on an isolated save.

Remaining limits: hidden hemisphere reconstruction still exists when a globe is
manually turned; image-less subclasses still use their defined procedural fallback.
Ice reflections/refraction use the existing environment-map approximation, not
full scene ray tracing. Orbital spacing is a schematic presentation with analytic
motion, not an N-body simulation. No late-game throughput improvement is claimed
without a workload-specific performance measurement.
