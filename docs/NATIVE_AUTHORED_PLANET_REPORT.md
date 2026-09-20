<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Jupiter and Mars in-game artwork trial

The 18 September 2026 trial connects the two maps from the standalone planet
preview to the actual native game. In Sol, Jupiter and Mars now use the same
authored full surface map for the solar-system disc, inspection portrait and
rotatable planetary screen. The first globe orientation exposes the central
reference landmarks. Dragging reveals the inferred far hemisphere. The original
source photographs are not wrapped around the globe.

## ENGINE CAPABILITIES ADDED / EXTENDED

- Application asset projection extends the existing Engine image and 3D material
  APIs; no parallel renderer or simulation was added.
- `native_planet_surface_assets.hpp` owns an allowlisted body/layer lookup shared
  by the system-disc loader and planetary provider. Earth retains its existing
  albedo, cloud and night layers. Unsupported keys do not become file paths.
- `NativePlanetGlobe::set_maps` now accepts `(body_key, layer)`. Binding includes
  the artwork key and survey visibility in its invalidation checks, so switching
  identity/discovery cannot retain another planet's texture or disclose a hidden
  surface. Authored colour maps do not invent height geometry.
- Albedo maps below the 4096-pixel preparation limit retain their original pixel
  bytes and immutable image object. These two maps remain 1774 by 887. They are
  loaded lazily into a fixed five-layer application cache alongside Earth's
  layers; each new map is about 6 MiB decoded. GPU upload uses the existing
  renderer cache. Solar discs use the existing worker queue and 16 MiB LRU.
- Build/export allowlists and SHA-256 checks include both PNGs and their
  provenance. Package consumers need no Python, image generator or browser.
- Save compatibility: no new campaign fields, identity changes or save migration.
  Existing fully surveyed Sol bodies resolve the new art by their canonical keys.
  Ordinary observer visibility and developer read-only authority remain intact.

## Validation

- Planet-disc regression exercises both real PNGs with no legacy JPEGs available,
  verifies synchronous/worker pixel parity, central-map colour, independent
  lighting, survey gates, cache budgets and existing photographic crops.
- Planetary regression verifies exact source pixels in the submitted 3D material,
  180-degree rotation, same-body key changes, hidden-world invalidation, cached
  reuse, spherical geometry, Earth layer preservation and responsive input.
- Developer graphical replay double-clicks both actual Sol bodies, captures focused
  system views and each globe's front/far side, and verifies inspection leaves
  the canonical campaign unchanged. Run at 1280 by 720 and 1920 by 1080.
- Final build/test/package outcomes are recorded with the supplied test build.

## ENGINE LIMITATIONS REMAINING

- This is a two-planet artwork trial. Other Sol planets and procedural worlds
  retain their existing pipelines; their cross-view appearance is not unified.
- Far sides are inferred artwork, not measured geography. Fine baked shading,
  possible polar distortion and minor seam mismatch remain. No measured height,
  normal or material-response maps are provided.
- First planetary map decode runs on the UI owner, as the Earth layers already
  did. Each lighting-specific system disc may decode its source again on the
  worker. General asynchronous shared source-map caching remains future work.
- Galaxy-size, simulation, navigation and save-memory limits are unchanged by
  this visual integration. This test package does not complete the engine.

## Trying the build

Extract the whole ZIP before launching. `Developer Game.cmd` starts an isolated
developer session using this folder's executable. Choose Sandbox and enable
the entire galaxy explored/surveyed option, or reveal the existing developer
campaign through Dev Controls. Use the celestial index to find Sol, open its
system view, then double-click Jupiter or Mars to open its 3D view. Drag the
globe to compare the near and far sides; the mouse wheel changes magnification.
For ordinary play run `stellar-continuum-native.exe`. The older installation is
left intact. Developer and ordinary save handling stays as in the previous build.
