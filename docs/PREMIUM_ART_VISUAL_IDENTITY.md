# Premium art and visual identity

Branch: `work/premium-art-visual-identity`. Base: integration 5763a1e. This branch implements the rendering and identity foundation directly in Godot 4.7.2 .NET. It does **not** declare the entire game commercially finished.

## Direction and implementation

Human design uses exposed modular frames, docking gantries, radiators, pressure vessels, dark glass, titanium/slate hulls and restrained warm identification marks. CivilizationVisualStyles selects species palettes and design motifs without touching species capabilities or game state. Pelagic pressure-shell, compact armored, cryogenic crystalline and unknown-neutral hooks reuse the same role silhouettes. Complete bespoke alien fleets and architecture remain an art-production gap.

Six ShipGeometry designs distinguish scouts, science vessels, colony ships, corvettes, resource outpost ships and bulk freighters. Native 3D meshes appear in the fleet inspector and system scene. System ships use bounded detail and projected clickable role markers. Their cosmetic stationkeeping offsets never change travel distances, destinations or mission progress. At most 64 local ship models/icons are rendered.

OrbitalStructureGeometry provides the same staged engineering model for the inspector and real system world. Shipyards use large open gantries, lateral platforms, docks, radiators and service structures. The actual active ship order and progress control an assembly hull/scaffold and at most four service craft; inactive/completed orders remove that activity. Work effects stop with the simulation clock.

SystemScene3D replaces disc cutouts with perspective-rendered spheres and annular rings. Left-drag pans, middle-drag orbits, wheel approaches the focused body and enters the atmosphere of an eligible owned colony. Actual observer-visible moons and facilities remain in the scene. Star spectra drive the photosphere and corona. The planet shader shades from the stellar direction without treating compressed navigation distances as physical light attenuation.

Earth restores the earlier NASA photograph at the user's request, replacing the strongly blue global-map treatment. The observed hemisphere is projected from the view direction onto native globe geometry, preserving depth and occlusion without claiming the photograph is a global surface map. It retains the photograph's clouds and lighting, so the additional Earth cloud shell and mapped city emission are disabled. The 3D camera, moons, stations and surface descent remain. NASA lunar mapping appears in orbit and in the Earth surface sky. Saturn retains cream/tan bands, oblateness and tilted geometric rings.

Each system sky uses its own stable star/nebula configuration; distant external galaxies belong only to the galaxy-level deep field. Galaxy dust is rendered at native display resolution from a bounded shader field, with layered arms and lanes around existing catalog positions. Typed catalog stars display their apparent spectral hue, including before planetary survey. Names, class labels, hazards, planets and special markers retain their existing knowledge gates. Untyped legacy-save stars remain neutral, and the 3D system scene still consumes the observer-scoped snapshot.

The surface has richer capital towers, dark facade/glass detailing, distinct researched module tiers, ground albedo/normal/roughness maps, conforming avenues, pads, vegetation, rocks and utilities. Ground traffic follows point-to-point routes, capped at five vehicles, and avoids construction footprints. These are presentation objects; placement validation and terrain collision remain authoritative. Surface skies show only actual visible moon markers, with restrained apparent size, daylight fading and phase lighting.

The orbital descent is a continuous camera/render transition through a globe, atmosphere, regional terrain and the local colony heightfield. The regional/local landscape is representative terrain. It is **not** geospatially streamed Earth geography or a complete planet simulation.

## UI, resolution and settings

The primary reference is 1920×1080 with a responsive 1280×720 minimum and native 1440p/4K rendering. The existing compact left rail, grouped right inspectors, graphical orders and timed queues remain. Campaign confirmation now uses the game's framed modal treatment. Selected ship/station previews are native 3D, with viewport sizing following display pixels.

The cursor repair coalesces resize updates and avoids applying an additional aspect transform to an already fitted logical canvas. A native Windows message probe verified the real left rail and drawer hitboxes at 720p, 1080p, an odd window size, 1440p, 4K and maximized.

Video settings enumerate Windows modes and expose windowed/borderless/fullscreen, V-Sync, MSAA and 3D render scale. Fullscreen uses desktop resolution. Apply/Keep/Revert and a 15-second timeout recover bad choices. NVIDIA Control Panel is an optional detected launch action; no driver settings, DLSS or ray tracing are claimed.

## Validation and evidence

The full native Windows candidate run in `work/premium-full-4` exited 0 with empty stderr: 32 maintained rendered screenshots and 120 actual GUI-input checks. The established validator accepted the screenshots and semantic engine logs. It covers ordinary research/construction, timed surface work, save/recovery, separate Player/Developer campaigns, ship orders and routes, orbital infrastructure, and responsive 720p/1080p/1440p/4K behavior.

That local run identifies its base commit plus the working candidate; it must not be mistaken for a committed-head release artifact. The PR screenshot workflow now runs for integration PRs and records its tested SHA. Release build also passed locally with zero warnings/errors. Simulation 69/69, Core Runtime 70/70 and Quality 18/18 passed during the original rendering work. The subsequent star-color correction below also assigns physical stellar classes during new galaxy generation.

### Star-color correction, September 10

The default numeric campaign used LegacyDisk generation, which never assigned the physical stellar deck outside BarredSpiral. A fresh 100-system map therefore had 99 untyped stars and only typed Sol. Survey gating then neutralized other catalog hues, and a 62% white core blend plus a white center dot washed out the remaining colors.

All new galaxy layouts now receive the existing deterministic balanced stellar deck. Sol remains a G-type yellow star. Existing positions, naming policy and independent strategic archetypes are retained. Apparent hue uses only physical stellar class, never a hidden strategic archetype fallback. The map now retains colored cores/coronae, and the 3D renderer covers every physical class with less white light dilution. Old saves are not migrated or assigned invented physical properties: start a new campaign to get classes where an old save contains null values.

The corrected working candidate passed the game Debug build with zero warnings/errors, Core Runtime 70/70 (including new default-layout completeness, Sol and seed-repeatability regressions), and Simulation 69/69. Native `work/premium-star-colors-final` exited 0 with empty stderr; the nine-body immersive walkthrough covered camera rotation, wheel descent, atmosphere, ground and return. Its `screenshots/immersive-02-galaxy.png` was visually reviewed and shows red, orange, yellow, white and blue catalog stars in a fresh 100-system campaign. These focused checks complement the original broad rendering gate; they do not replace it.

Additional native evidence:
- immersive-review-9: complete orbit/atmosphere/ground/return journey.
- premium-saturn-2: reviewed planetary quarter lighting; final ring framing adjusted afterward.
- premium-surface-final: bird's-eye and street review, module-tier geometry and bounded city-detail checks.
- moon-sky-focused-4: mapped visible Earth Moon and world-switch/close lifecycle.
- final-video-720 and final-video-1080: seven settings/persistence/revert checks each.
- native-pointer-current: physical window-coordinate regression proof.

The maintained screenshot set includes menu, research, construction, relations, colonies, galaxy/region, Earth focus, surface placement, capital/Mars terrain, shipyard selection, fleet routing and orbital facility inspector. A curated warp close-up, first-colony cinematic, fully assembled shipyard close-up and human 30-minute subjective play review remain additional art gates; the current captures do not prove those are finished.

## Sources and licensing

### Galaxy footprint and Earth rollback, September 10

First-run startup now requests the same Standard100 barred-spiral settings as New Game. The general legacy generator default remains unchanged, preserving callers and saved catalogs. The dust shader now matches the generator's 900-unit radius inside the 2200-unit frame, .72 Y compression and `radius * pi * 2.35` arm winding. The previous art winding was roughly half as tight as the playable layout. Existing legacy-disk saves use disk-shaped dust sized to their actual star extent; no saved coordinates are moved.

Core Runtime 70/70 passed after this scoped correction, including every arm's middle/outer occupancy and exact saved-position continuity. An attempted global default change exposed incompatible legacy-generation assumptions and was removed before publication; the startup adapter supplies the correct settings explicitly. `work/galaxy-earth-final` then exited 0 with empty stderr, nine captured views and the complete immersive camera/atmosphere/ground/recovery walkthrough. Its fresh galaxy and restored-Earth screenshots were visually inspected. Earth is intentionally the previous photographic appearance; a new rotatable global Earth treatment requires a separate art review.

See [Immersive texture provenance](IMMERSIVE_TEXTURE_PROVENANCE.md) for exact NASA/Poly Haven source URLs, credits, SHA-256 values and transformations. NASA imagery is credited to its source project; Poly Haven ground maps are CC0. Existing original ship/portrait/splash assets remain covered by their established provenance. No Stellaris screenshot or Stockcake reference image is included in game assets. New geometry and shaders are authored project code.

## Performance and remaining quality work

3D system/surface worlds use separate viewports updated only while visible. Hidden worlds are cleared when closed. Native preview sizes are capped, map dust caps at 4096 pixels, reusable materials and bounded instance sets avoid per-frame model regeneration. Profile/style changes rebuild only relevant visuals. Shader surfaces use mipmaps and anisotropic sampling. No effect changes a command outcome, resource budget, save format or observer knowledge.

This is a substantial 3D presentation step, with remaining visible gaps: custom production meshes/material baking, diverse alien architecture, believable pedestrians and vehicles, finer cloud/atmosphere scattering, geographically coherent globe-to-ground terrain, natural ring/planet shadow interaction, cinematic staging, and richer combat/warp effects. The current surface still reads as procedural architecture rather than photoreal commercial environment art. The branch is reviewable code and evidence, not a declaration that those gaps have vanished.
