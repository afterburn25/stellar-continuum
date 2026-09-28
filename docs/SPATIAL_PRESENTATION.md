# Galaxy and Star-System Spatial Presentation

Status: **early-release milestone 1 implementation contract**

Owner: `work/galaxy-star-system-visuals`

## Purpose

The spatial presentation layer turns authoritative simulation state into a strategically readable Godot view. It does not own star systems, planetary bodies, fleets, colonies, survey state, claims, logistics, movement or combat.

The first implemented hierarchy is:

`STELLAR REGION -> STAR SYSTEM`

A selected system with at least reconnaissance-grade knowledge can be opened by double-clicking it. Double-clicking empty system space returns to the stellar map while preserving the selected-system ID plus the stellar map's existing pan/zoom context.

Pointer navigation and map orders run after GUI controls consume input. Double-clicking a panel or a different/empty map location cannot open the previous selection. The system canvas consumes pointer commands, including the science-order shortcut, and celestial-object double-clicks do not count as empty-space return gestures. Entering/returning ends any active middle-button drag without changing the saved pan/zoom values.

## Fair-information boundary

System visualization consumes `ExplorationReadModel`, not raw `GalaxyState.PlanetaryBodies`.

That distinction is deliberate:

- **Detected:** star target only; system spatial view is not available.
- **Partially surveyed / reconnaissance:** basic orbital catalog is available (body identity, parentage, orbit order, broad body kind and approximate radius), but body and stellar visual classes remain explicitly unknown. Positive resource/anomaly/activity signatures may be shown only where the read model exposes a positive reconnaissance signature. Absence is never inferred.
- **Fully surveyed:** the presentation may derive broad visual classes from the now-legitimate environment fields exposed by the read model.

The system projection does not retain mass, gravity, temperature, pressure, resource values or native-civilization facts in its render marker records. It retains only geometry, an allowed visual class and positive signature flags needed to draw the current view.

## Simulation distance vs display distance

Stellar Continuum spans incompatible physical scales. Milestone 1 therefore uses explicit schematic display scaling.

### Simulation distance

Authoritative travel/movement remains whatever the simulation and logistics systems calculate. This branch does not change it.

### Stellar-map display distance

The existing strategic map continues to use simulation-owned star positions transformed by the existing pan/zoom camera.

### Star-system display distance

Planetary orbit spacing is **schematic**:

- first planet display orbit: 94 presentation units;
- subsequent planet display orbits: +68 units by authoritative orbit index;
- moon display orbits: 17 units plus 8 units per moon orbit index, with a small allowance for the displayed parent radius.

These values are not kilometers, AU, transfer windows or travel time. They exist only to make orbital hierarchy readable.

### Object display scale

Planet/moon screen radii use a bounded square-root transform of the observer-safe approximate Earth-radius value. A body is therefore recognizable without claiming that rendered diameter and rendered orbital radius share one physical scale.

### Icon / marker scale

Strategic signatures use fixed-size presentation shapes so they remain legible when celestial objects are small. A marker never changes the underlying simulation fact.

## Determinism

Body placement angle is deterministically derived from stable body ID. The same known system therefore keeps the same schematic layout across redraws and reloads as long as the authoritative body identity/catalog is unchanged.

The presentation does not serialize orbital display geometry. It is reconstructible.

## Rendering and performance

Milestone 1 renders only the currently opened star system in body detail. The distant stellar map remains the lower-detail strategic representation. While the opaque system canvas is open, the underlying stellar draw pass is skipped and science-fleet labels at stellar coordinates are hidden. No system-local fleet placement is implied.

The system snapshot is bound to the exact campaign object, observer and selected system. A context change closes it immediately; campaign reset clears it synchronously. Survey confidence changes refresh immediately (or close below reconnaissance). Otherwise the authoritative read model refreshes at most once per second, including when survey progress has not changed, so later visible signatures/environment changes cannot stay cached forever. Ordinary frames do not rebuild the full read model. This bounded refresh is an interim correctness measure: Exploration should later own a selected-system read/revision contract to avoid full-catalog work for one opened view. Neither the cache nor projection reads raw planetary facts.

Rendering and hit testing share one viewport transform. Large schematic systems can shrink below the former minimum scale to keep their outer bodies accessible. Moon-parent lookup is retained only for the current snapshot instead of allocated during every redraw.

Future scale work should continue toward:

- batched/instanced strategic star markers for very large visible catalogs;
- zoom-dependent label density;
- pooled fleet/colony markers;
- region culling / spatial indexing;
- authoritative operational-range, claim and combat overlays supplied by their owning systems;
- an Exploration-owned selected-system read/revision interface and measured 500/2,000-system refresh cost;
- selectable orbital facilities and closer station detail after location-specific simulation/read interfaces are stable.

The player's home-system view now draws the established orbital construction projects close
to the star as distinct launch-complex, shipyard, and asteroid-network silhouettes. Their labels show whether
each project is locked, available, active, or complete; an active project also carries a
progress arc. This is a read-only view of the existing construction state and does not create
new orbital locations, bonuses, or save data. Clicking either silhouette opens the Industry
operations page, where the existing authoritative construction action remains available.
Locked markers report every missing technology and prerequisite facility by player-facing
name through the standard command-feedback strip.

## Visual language

The current system canvas follows the `work/visual-style-assets` **Deep-Space Instrumentation** standard: deep navy canvas, restrained keylines, cyan selection/focus, explicit unknown treatment and shape-based positive signatures.

Until the visual-assets PR is integrated, this milestone uses matching in-code presentation colors and simple geometric temporary markers rather than copying or vendoring unmerged assets. Once the shared theme/icons are present on `integration`, this workstream should consume those stable resources directly.

## Catalog follow-up

Issue #221 remains a separate astronomy data milestone. It requires a versioned 1,000-real-system snapshot with provenance and a strict separation between observed astronomy and any simulation-completed unknown bodies/environments. The September 8 abundance guidance is provisional gameplay tuning, not observed astronomy. This spatial milestone adds no catalog records and changes no simulation or save schema.
