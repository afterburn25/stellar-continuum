# Species-Compatible Physical Homeworlds

## Purpose

Founding civilizations must begin on real planetary bodies that are naturally viable for their authored Species biology. The old prototype selected home systems from the system-level `HasHabitableWorld` flag before Species identity mattered, so a seeded civilization could conceptually begin on a world whose atmosphere, solvent, pressure, gravity, temperature, or immersion state did not support it.

This milestone removes that contradiction for newly generated campaigns without making the planet generator civilization-aware and without changing save format.

## Ownership boundary

### Generation owns physical worlds

`PlanetaryBodyGenerator` remains the authoritative deterministic seed + system -> planet/moon catalog function.

A species-neutral `PlanetaryEnvironmentalDiversityPolicy` is now part of that canonical generation function. It conditions one existing planet in the first four systems of each eight-system ID cycle into one of four physical environment niches:

1. temperate surface water
2. high-pressure ocean
3. high-gravity surface
4. cryogenic hydrocarbon

The other four systems in each cycle remain entirely procedural.

The diversity policy knows no Species IDs, civilization count, AI archetypes, personalities, or future homeworld choices. It preserves body identity, orbit, resource/anomaly flags, legacy markers, and existing native pre-warp markers; only physical radius/mass/environment are conditioned.

Because the policy runs inside `PlanetaryBodyGenerator`, fresh generation and save/load reconstruction produce the exact same catalog from the same campaign seed and systems.

### Species owns biological interpretation and homeworld selection

`SpeciesHomeworldPlanner` evaluates already-generated physical bodies through `PlanetarySpeciesHabitabilityEvaluator`.

It requires naturally colonizable bodies and excludes bodies already carrying an independent native pre-warp civilization. It assigns the most environmentally constrained founding Species first so an abundant Terran-like candidate cannot consume a rare niche needed by another Species.

Each seeded civilization receives a distinct home star system. Candidate scoring is dominated by natural habitability, with a smaller geographical-spread term to retain separated starts.

### Civilization identity remains independent of biology

`SpeciesAssignmentPolicy` still derives Species identity only from campaign seed + stable civilization ID.

Civilization template/archetype/AI traits do not decide Species identity or homeworld compatibility.

### Colonies own the occupied body

`ColonyState.PlanetaryBodyId` already exists and is persisted by save v8.

For new campaigns, `ColonySeeder` resolves the exact naturally viable body inside each planned civilization home system and stores that body ID on the founding colony. The following identities are therefore required to agree:

- civilization Species ID
- civilization home system ID
- founding colony population Species ID
- founding colony system ID
- founding colony planetary body ID
- authoritative Species habitability evaluation of that body

Legacy migration paths that do not provide a planetary catalog may continue to create a null `PlanetaryBodyId`; this milestone does not rewrite old saves merely to manufacture a historical homeworld.

## Save-format behavior

No save-format bump is required.

- `ColonySaveDto.PlanetaryBodyId` already exists in save v8.
- save v8 persists/restores the field.
- persistence already validates that a colony body exists and belongs to the colony system.
- the planet/moon catalog remains reconstructible from seed + systems.

## Validation

The Species homeworld regression runs twelve campaign seeds using the default 120-system / 10-civilization setup.

For every seed it requires:

- every civilization receives an assignment;
- home systems are distinct;
- home planetary bodies are distinct;
- Species ID equals deterministic `SpeciesAssignmentPolicy` output;
- generated `CivilizationState.HomeSystemId` equals the planner assignment;
- every generated founding colony has non-null `PlanetaryBodyId`;
- colony system/body/Species identity matches the civilization/planner assignment;
- the occupied body is not an existing native pre-warp world;
- authoritative Species evaluation says the body is naturally colonizable;
- recorded natural habitability exactly matches authoritative evaluation.

Separate core persistence regressions verify that the conditioned deterministic planet/moon catalog round-trips exactly through save/load reconstruction.

## Deliberately not enabled here

This milestone does **not** yet make natural habitability multiply population growth or productivity.

It also does not create habitat-support capacity, food/air supply units, environmental-maintenance costs, mortality, health, or medical effects.

The next safe Population/Economy integration can consume the now-authoritative colony body and `ColonySpeciesEnvironmentView` without the old risk of penalizing a civilization for an arbitrarily incompatible seeded homeworld.
