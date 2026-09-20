<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> Godot/C#/.NET references below are legacy implementation or fixture provenance,
> not the current runtime or instructions to restore it.
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Native 3D consumers and large-campaign integration

Date: 2026-09-18. This extends the existing C++23 Engine and authoritative Core;
it is not a separate engine or a claim that the complete migration is finished.

The obsolete native surface interface is removed. Colony entry from the system
view, roster, ordinary play and diagnostics now always uses `NativePlanetaryScreen`.
There is no smoke-only fallback or switch to the retired flat surface. The old
workspace, terrain renderer, relief/building presentation, art loader, build
targets, asset manifest and runtime validators were deleted. Its packaged ground
image and credit were also removed from the preview. Original source artwork and
canonical colony/construction state remain available. The preserved reference
Godot game is not a runtime fallback for this native interface.

## ENGINE CAPABILITIES ADDED / EXTENDED

### Ownership and reusable interfaces

| Layer/module | Interface | Integrated consumers |
| --- | --- | --- |
| Engine `physics3d.hpp` | `PhysicsVector3`, `KinematicState3D`, `move_toward_velocity`, `segment_sphere`, `segment_triangle` | Fixed-step Core combat movement; nearest-hit globe surface picking |
| Engine `native_geometry3d.hpp` | `extruded_convex_mesh`, `heightfield_mesh`, `radial_terrain_mesh`, `intersect_mesh_segment` | Tactical corvette hulls; displaced planetary surface mesh and selection |
| Engine `spatial_index3d.hpp` | `SpatialIndex3D`, partition labels and nearest queries | Large-catalog lane backbone/neighbors; native map knowledge frontier |
| Engine `atomic_file_write.hpp` | `AtomicTextSink`, `AtomicTextProducer`, `write_file_atomically_stream` | Core player and developer saves, including background writes |
| Engine `image_clipping.hpp` | `clip_image_to_viewport` | Territory/fog atlases at large map zoom; reusable unrotated image cropping |
| Core campaign/combat/persistence | Existing command and snapshot interfaces extended with optional depth; streamed save entry points | Campaign encounters, tactical movement orders, Player17 and developer save/load |

Engine types contain no colony, civilization or game-specific rules. Simulation
still runs on its existing owner and fixed ticks. Immutable snapshots go to save
workers; rendering never writes the campaign. Meshes are immutable and share the
existing GPU resource cache. A pointer/camera cache avoids rescanning globe
triangles when the pointer and view have not changed.

### Ship, terrain and physics consumers

Combat positions, velocities, objectives, distance queries and neighborhood
hashing support XYZ. Native campaign encounters request spatial deployment, and
the tactical view projects depth consistently for fit, markers, picking and
orders. Alt+wheel issues an authoritative `Advance` command changing the selected
owned formation's depth by 100 units; ordinary field orders preserve its current
depth. The snapshot is never moved locally to fake command completion.

Tactical corvettes use an extruded solid hull and the approved transparent ship
texture on a deck mesh, rendered through `Scene3DView` with depth and lighting.
Heading/pitch/depth derive from observed movement. The existing 32-corvette art
budget remains, with two mesh instances per rendered ship. Other formations retain
their existing observer-safe symbols; unidentified enemies do not gain ship art.

The new planetary globe uses radial displaced geometry for known generated solid
bodies, with normals derived from the same seeded field as its procedural surface.
Region projection and hit testing follow the displaced mesh. Clouds remain above
the highest relief. Authored Sol textures without elevation data, gas giants and
unknown bodies retain smooth geometry. This relief is presentation, not new
authoritative mineral/terrain simulation. The reusable heightfield constructor is
tested, but no retired ground-screen consumer remains.

Physics provides bounded acceleration/velocity integration and continuous
segment/sphere and segment/triangle queries, including initial sphere overlap and
two-sided triangle hits. The globe uses the exact rendered mesh for nearest-hit
selection. These are working motion/query primitives, not a rigid-body solver.

### Larger galaxies and persistence

Setup adds 25,000 and 50,000 systems to the existing six sizes. Content-driven
generation settings cover the same six morphologies. Catalogs through 10,000
retain the previous exact lane algorithm and tie rules. Larger catalogs use
partition-aware indexed Boruvka construction plus nearest-neighbor links. Input
IDs sort canonically, and shuffled-input tests prove stable graph output. The lane
network stores compact IDs/positions rather than copying full systems. Route-tree
cache capacity adapts to catalog size (five trees at 50,000), bounding its retained
entry budget. The native large-catalog knowledge frontier also uses the index.

Large catalogs render unvisited, unselected stars below 16x zoom with compact
cropped cores: four vertices instead of the full marker's 26. Every visible system
is still submitted and participates in selection; no sampling or catalog cutoff
is introduced. Known/selected systems retain their full markers and observer
rules. The 50,000-marker test accounts for all 200,000 vertices and 300,000 indices
in four bounded batches and checks that selection restores the complete halo.

Normal player and developer save writers now serialize a leaf record at a time
into a 64 KiB atomic-file buffer. Existing validation, backups, flush and replace
semantics remain. A producer exception preserves the destination. Full player and
developer stream outputs are byte-compared against their existing encoders,
including Unicode. Old combat saves default missing Z to zero; zero Z is omitted
on write so legacy planar save bytes and simulation golden fixtures remain valid.
No save schema increment is needed for these optional fields.

The 50,000-system fresh-start check exposed oversized territory images at the
initial local zoom. Cropping now limits the destination to the viewport and maps
the corresponding source texels without moving or stretching the territory.
The renderer keeps its existing validation limit; invalid geometry is not accepted
by increasing that limit. Offscreen images are culled. New tests cover a 400,000
pixel projection, source subrectangles, invisible images, rotation rejection and
the actual magnified territory consumer.

The supplied fixed star background is still regional-map scenery, behind nebulae
and game objects. It covers the viewport independently of pan/zoom. The galaxy
overview keeps its original artwork; system and planetary views do not inherit it.

### Measurements on this Windows host

One CTest run, seed 8057, fresh six-civilization worlds; elapsed time is not a
universal hardware guarantee. Route/repeat includes three routes and building a
second graph from reversed input, not just one path query. Capture/write includes
full snapshot capture; reload includes validation.

| Systems | Bodies | Lanes | Generation | First lane build | Routes + repeat graph | Capture + streamed write | Reload + checks | JSON bytes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 25,000 | 177,381 | 44,967 | 8.718 s | 0.710 s | 0.740 s | 4.661 s | 3.300 s | 219,894,326 |
| 50,000 | 355,519 | 89,639 | 33.553 s | 1.574 s | 1.656 s | 9.070 s | 6.680 s | 440,690,990 |

Separate 10,000-system save processes measured peak working set through Windows
`GetProcessMemoryInfo`. Both generated and captured the same world and wrote
87,945,352 bytes with identical UTF-8 formatting. Streaming peaked at 93,036,544
bytes (88.73 MiB), versus 422,703,104 bytes (403.12 MiB) for the previous document
plus string path: about 78% lower in this isolated benchmark. This is process peak
for this fixture, not a claim about total late-game client memory. The experiment
does not include loading or the renderer. Evidence: `work/save-memory-measurements.json`.

The final actual 50,000-system map navigation run averaged 17.277 ms per frame,
p95 18.061 ms, with mean CPU scene preparation 6.783 ms. It recorded 699 frames
including cold artwork, save service and navigation transitions; capture waits
affect its sample count. The largest save-service update was 249.494 ms, so the
average must not be interpreted as hitch-free gameplay. Log and captures:
`work/scale50000-runtime/compact-navigation.log`, `compact-result.json`,
`compact.png` and `compact-regional.png`. The prior full-marker diagnostic averaged
53.442 ms, but its 243-frame sample differs; use this as an observed improvement,
not a controlled universal speedup ratio.

### Validation evidence

- Full native build and 209/209 CTest cases: `work/engine-final-tests.log`.
  Retiring the obsolete surface tests changes the historical test count.
- Final nine focused geometry, image cropping, markers/labels/backdrop, territory, input, planetary and platform
  regressions: `work/final-focused-tests.log`.
- Actual planetary construction, cancellation, save and paused reload at 720p,
  1080p and 1440p; navigation modal/keyboard ownership; freight review, dispatch
  and paused reload: `work/current-*-runtime.json`.
- Actual tactical battle order/art/observer filtering and paused reload at 720p
  and 1080p: `work/scale3d-battle-runtime-result.json`.
- Export allowlist/tool checks: 68 passed, `work/surface-removal-export-tests.log`.
- Actual 50,000-system setup, asynchronous creation, independent save slot and
  paused reload through overview, regional zoom and system entry pass:
  `work/scale50000-runtime/result.json`. Fresh input-to-completion took 48.83 s;
  reload/navigation verification took 21.25 s. Those totals include UI automation,
  artwork preparation, saving and captures; they are not just generation timings.
- 3D movement save round trips, legacy planar golden parity, fixed-step partition
  invariance, mesh picking/normals and indexed-versus-brute-force results are
  covered by the native suite. These test real state and geometry, not screenshots
  standing in for gameplay.

## ENGINE LIMITATIONS REMAINING

- Campaign support now stops at 50,000 systems. Generation and some full-catalog
  work remain expensive; unlimited worlds and long-running late-game campaigns
  are not certified. A 50,000-marker overview is not a guaranteed 60 FPS view.
- Save capture still copies the complete immutable DTO. Loading still reads and
  parses a complete document. Disk output is still large, uncompressed JSON;
  streaming writes reduce transient memory, not file size or every save pause.
- No rigid-body/contact response, ground vehicles, terrain collision dynamics,
  gravity/N-body integration, physics broad-phase world or multiplayer rollback
  is added. The terrain query is a bounded triangle scan with cached pointer hits.
- Authored elevation datasets and imported detailed 3D meshes for all ship classes
  are absent. Tactical geometry is an extrusion of existing approved artwork.
  Procedural relief is visual and is not saved as new gameplay geography.
- Existing 3D cache/view/geometry budgets, center-sorted transparency, one
  directional light, absent shadows/PBR/animation and nebula detail limits remain.
  Province resources, ground combat and broader migration/release gates remain
  separate unfinished capabilities.

Future consumers can reuse the same geometry, motion, collision queries, spatial
index, cropped-image and streaming-file contracts without duplicating Core rules
or reviving the removed surface interface.
