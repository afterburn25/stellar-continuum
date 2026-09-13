# Fresh campaign composition boundary

The next integration step after native knowledge and legacy technology state is a real fresh-game seed. It composes the migrated Core rules, not another invented headless simulation. Authority is `Generation/GalaxyGenerator.Generate`, especially the sequence after nearby-world guarantees. The existing physical/founding/colony previews retain their current schemas and behavior.

## Scope and ownership

Add a Core-owned fresh campaign value containing seed, systems, explicit planetary bodies, civilizations, fleets, colonies, economies, legacy technology states, construction states, shipyards, player civilization ID, knowledge and the optional galactic-core landmark. Reuse the existing native types; do not introduce reduced fleet/colony duplicates or retain JSON as simulation state. The landmark is not a routable system. Fresh core access/exploration remain empty. This is fresh initialization, not a save-v16 import or a running campaign scheduler.

Generate into a local owner and return it only after every stage succeeds. Failed generation must not publish a half-formed campaign to the application. Collection order and actual mutable colony population are significant. Borrowed subsystem views are built for each operation and never retained across vector replacement. Lane graphs remain independently owned immutable geometry copies; do not attach a reference to a local generation buffer, and do not serialize graph caches as authoritative game state. A later runtime campaign owner will own/rebuild its graph explicitly.

## Generation sequence

1. Generate the existing full-galaxy physical catalog and apply the reviewed founding/home guarantee pipeline.
2. Seed colonies from final civilizations and final body catalog.
3. Seed fleets using those same mutable colonies as population sources. A starter colony ship reserves actual people from their source colony before later output/projection. Do not call the optional no-population compatibility overload.
4. Seed economies, legacy technology states, construction states and shipyards, in that order. Do not run an economy tick or production advancement to manufacture a populated starting state.
5. For each civilization in source order, mark its home fully surveyed, then reveal nearby systems using its source sensor range. The existing full-galaxy profile uses float 8 light-years for ordinary civilizations and float 25 for `IsSeededAncient`, not development-stage inference. Sensor contacts are detection-level, not fully surveyed.
6. Resolve the first player civilization by source semantics and return the composed state and non-routable core metadata. Empty or malformed player selection is not repaired by inventing a player.

Preserve the already-supported profile: 250/500/1000/2500 systems, default six ordinary and one ancient civilization, supported player species and current founding count limits. Capture options explicitly. `GalaxyGenerationMetadata.FullGalaxy500(...).ToSettings()` is the C# settings authority, including 8/25 sensor ranges. Broader galaxy-creation options and development-stage UI overrides are a separate gate.

## Legacy technology dependency

Port `TechnologyState`, the five category values, six complete `TechnologyRegistry` definitions, ordinal lookup, prerequisite/project-based availability and `TechnologySeeder`. Preserve source catalog order, completed-ID set uniqueness and observable insertion order. AncientSpacefaring starts with all six legacy technologies; other stages start with none. Active ID is absent and progress zero. This does not implement Adaptive Research, paid research progression, new unlock logic or a replacement technology tree. Legacy capabilities must not silently become Adaptive Research capabilities.

## Application and validation

Expose a separate explicit CLI initialization mode, for example `--seed-campaign`, with a versioned `stellar-fresh-campaign-v1` diagnostic schema and `gameplayParity: false`. Preserve old modes. Include full seeded fleets, actual post-reservation colonies, economies, technology/construction/shipyard states and complete knowledge observation. State clearly that the diagnostic file is not a player save. Unknown flags, invalid seeds/settings, missing catalog data and output write failures must exit cleanly with useful terminal diagnostics. Never launch a graphical loading screen or silently overwrite an existing output.

Use actual complete C# `GalaxyGenerator.Generate` results for the supported full profile; do not merely repeat the native composition in an oracle. Cover all four map sizes and multiple player species/count configurations, default hidden core knowledge, first-player ID selection, exact home-vs-neighbor survey levels, seeded ancient technology and empty shipyard queues. Independently exercise a crafted WarpCapable composition with colony population reservation because normal full-profile civilizations are PreWarp/Ancient and may not exercise that branch. Compare full relevant state and source failure results; conservation must be checked against the same source populations used by fleet seeding.

Time the whole initialization, including final knowledge/state assembly, rather than ending the measurement at physical generation. Repeated generation must reset all mutable collections and produce identical state for identical inputs. Relocated exported runtime validation should execute the new mode from an unrelated working directory using packaged data. Keep full campaign ticks, travel advancement, survey completion, AI, diplomacy, combat, saves, graphics and player UI explicitly open until their own gates pass.
