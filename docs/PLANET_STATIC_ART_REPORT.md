<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Fixed planet presentation and gradual star zoom

The September 19 correction keeps the 3D globes and the existing imported surface
textures. All planets hold their saved reference pose as simulation time advances.
Additional cloud shells, extracted/procedural cloud overlays and cloud shadows
are removed from all planet types, including Sol and legacy portraits. Manual
globe inspection, zoom, star illumination, rings and planetary surface identity
remain. Artwork is not rerolled or recolored for additional variety.

## ENGINE CAPABILITIES ADDED / EXTENDED

The shared application material assembly in `native_planet_materials.hpp` supplies
the same static pose to System View, Planetary Screen, portrait creation and globe
picking. It no longer loads or allocates cloud maps. The legacy globe and disc
consumers also stop requesting clouds. Core retains physical rotation periods
and atmospheric rules in existing saves; this presentation correction neither
rewrites those records nor stops the campaign clock or asteroid motion. Generic
Engine cloud shaders remain available to other future consumers.

Star artwork in `native_stellar_art.hpp/.cpp` blends from distant light to detail
over an 8–48 pixel core radius (compact black holes retain their 8–24 pixel range
so central horizons resolve at overview). Optional stable presentation keys identify each
galaxy-map primary or companion. A bounded 1,024-entry LRU tracks their individual
blend weights; changes are limited to a full transition over at least 0.65 seconds
of presentation time, including zoom reversals and already-cached textures.
Close imagery remains requested while fading out below the distant threshold.
This fixes the old radius-only switch on subsequent wheel movements. The existing
four close textures and two pending decode jobs remain bounded. The transition
does not alter physical stars, campaign time, saved data or shared artwork colors.

Planet validation covers all 65 subclasses at three LODs, time-invariant poses,
old nonzero cloud opacity, unchanged imported albedo bytes, cloud-free portraits,
Sol, rings, manual inspection, picking and bounded streaming. Star regressions
cover gradual red-giant zoom in/out, cached revisits, quick direction reversal,
independent stars sharing an image and transition-cache limits.

Completed validation: all 11 selected C++ regression suites and all eight export
tests passed. The 3,993 approved planet asset files retained their recorded
hashes. The packaged Vulkan replay passed at 1280×720 with eight imported planet
classes in both views, canonical portraits, ring shadows, asteroid orbit/tumble
and pause/resume, and save/load. Its source save remained unchanged. The replay's
final critical-pause diagnostic is an intentional fault-injection check. Globe,
ringed-planet and star-zoom captures were reviewed visually.

The current source directory inventory is 49 folders and 1,107 image files;
the previously supplied 47-folder audit contains 1,087 files (665 accepted and
422 rejected). This correction preserves that audit and selected images, rather
than claiming that all 1,175 mentioned images have been found or accepted.

## ENGINE LIMITATIONS REMAINING

Cloud features painted into a supplied surface image remain part of that artwork;
removing them would edit the image. Existing 3D conversions still contain inferred
hidden hemispheres, and unsupported physical subtypes retain their existing
procedural fallback. This correction does not regenerate or expand the art pool.
Four close star types can be resident at once; crowded views retain distant art
for additional types. The crossfade blends distinct supplied images rather than
reconstructing a continuous physical stellar surface.
