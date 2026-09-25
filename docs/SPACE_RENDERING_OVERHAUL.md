# Space rendering overhaul — renderer audit and plan

Branch: `engine/space-graphics-overhaul`, based on
`engine/space-strategy-simulation-specialization`.
Audit date: 2026-09-24. Scope: `engine/` rendering systems, `engine/shaders/`,
`app/engine_main.cpp` / `app/editor_main.cpp` visual tooling, rendering tests.
This document is the phase-1 audit; landed work is tracked in
`docs/VISUAL_ENGINE_HANDOFF.md` and `docs/ENGINE_CAPABILITIES.md`.

## Current implementation

**API** (`stellar::native_map`, `engine/include/stellar/engine/native_scene3d.hpp`):

- `Mesh3D` immutable vertex/index resource; `uv_sphere`, OBJ loader,
  `annulus_mesh`, `directional_solid_mesh`, `surface_emission_volume`
  generators; bounding sphere + local AABB; validated CPU-side.
- `MeshInstance3D`: double-precision position + quaternion + uniform scale;
  `Material3D` per instance.
- `Material3D`: albedo texture, tint, ambient/diffuse/opacity,
  `dark_side_strength` (night-side alpha mask), transparent/double_sided,
  per-object `light_direction` override, `light_color`/`light_intensity`,
  `rim_power` grazing shell, `two_sided_diffuse`, `linear_light` sRGB decode,
  `anisotropic_texture`, `cubic_magnification`, and optional blocks:
  - `Dielectric3D` — IOR refraction/reflection against a world-fixed
    equirect environment map, Beer-Lambert absorption, roughness cone filter.
  - `SurfaceResponse3D` — normal map + packed properties (roughness /
    liquid / ice / height), cloud map with animated UV offset whose alpha
    shadows the surface and whose RGB composites as a lit deck
    (`cloud_albedo`); any map subset binds. `Material3D::terminator_wrap`
    adds wrap-diffuse terminator softening to every light type, and
    `Material3D::limb_darkening` applies linear N·V limb darkening to
    self-luminous discs (the Sun's photosphere profile, u ~= 0.6).
  - `AnalyticShadow3D` — ellipsoid or annulus blocker of the directional
    light, annulus alpha-map with footprint integration (ring→planet and
    planet→ring shadows, bias-free by construction).
  - `SurfaceEffect3D` — emissive image sequences, flow distortion,
    photosphere occlusion sphere, bounded emission-volume ray march.
  - `additional_lights[2]` — extra camera-space directional lights.
- `Scene3D`: camera + instance list + one camera-space key light;
  validation of all ranges; instance/resource budgets.
- `prepare_instance3d`: conservative sphere/frustum cull, camera-relative
  double-precision transform narrowing, perspective + orthographic.

**GPU backend** (`engine/src/native_scene3d_gpu.cpp`, SDL_GPU / Vulkan SPIR-V):

- `engine::DrawBatcher` groups opaque by (material,mesh), transparent
  back-to-front; identical runs merge into instanced draws — per-instance
  transform/material records live in SSBOs, indexed by `gl_InstanceIndex`.
- `engine::RenderGraph` declares frame resources and schedules
  `scene3d` → `tonemap`; compile failure aborts the frame.
- HDR: RGBA16F scene target when supported (UNORM fallback), fullscreen
  tonemap with C1-continuous knee + headroom on unpremultiplied color.
- `engine::TextureStreamer` residency: per-mip byte budgets, screen-footprint
  mip demand, partial mip-tail residency, pinned white fallback; cooked
  BC4/BC5/BC7 upload paths, BC1 mips, CPU box-downsampled RGBA tails.
- `Scene3DStatistics`: uploads, draw calls, culled instances, cache bytes,
  streamed fallback/partial/evicted counters, MemoryTracker attribution.

**Tooling**: `stellar-engine.exe` Scene3D tool authors `editor/scene3d.json`
(`Scene3dDocument`) — entities (mesh spec incl. OBJ, transform, tint,
texture, opacity, double_sided, physics tags, vfx), camera, key light,
two fill lights, gravity/bounds/bg/music; live preview with orbit, fov,
pick, drag transform modes, undo/redo. `RuntimeHost --scene3d` runs the
same document headless-tested.

## Gaps vs the mission

1. **PBR materials** — no metallic response, no scalar roughness, no
   emissive map (only dark-side alpha masking), no alpha cutout, no UV
   tiling, no metallic-roughness map. Ships/stations cannot express metal.
2. **Lighting** — directional only, ≤3 per material, no point/spot lights,
   no distance attenuation (engine glow, station floods impossible).
3. **Shadows** — landed: key-light directional shadow map (authored
   ortho volume centred ahead of the camera, depth pass + 8-tap PCF,
   tier-scaled resolution, Low skips). Remaining: no CSM splits for
   extreme zoom ranges, point lights stay unshadowed; analytic
   ellipsoid/annulus blockers remain the ring↔planet path.
4. **IBL** — environment map reachable only through `Dielectric3D`;
   ordinary materials get no diffuse irradiance or specular environment.
5. **Post** — tonemap only; no exposure, bloom, contrast/saturation
   grading, sharpen, or AA (pipelines are all SAMPLECOUNT_1).
6. **Atmosphere** — only the flat `rim_power` alpha shell; no
   wavelength-weighted scattering, no day/night limb behavior.
7. **Planet features** — landed: `SurfaceResponse3D` is authorable
   end-to-end (normal/properties/cloud maps, any subset; `surface` doc
   block + `MaterialSurface` component + editor rows), `cloud_albedo`
   composites the cloud map's RGB as a lit deck over surface emissive
   and under the atmosphere rim, `terminator_wrap` softens the
   day/night edge across key/fill/point lights, and `limb_darkening`
   gives self-luminous bodies the photosphere's edge falloff.
   `band_shear`+`band_waves` (two-harmonic longitude warp — differential rotation + alternating jets)
   and `orbital_beaming` (first-order doppler asymmetry about local +Y —
   accretion discs get their approaching-lane brightening)
   landed on top. Ring/scattering physics followed:
   `accretion_disc_material3d` + `accretion`/`AccretionDisc` generate a
   Shakura–Sunyaev radial disc for `annulus` meshes (black holes compose
   with a dark sphere), `forward_scatter`/`forwardScatter` adds a
   Henyey–Greenstein phase function (backlit dusty rings brighten, icy
   opposition surges), and `SurfaceEffect3D::volume_scatter` makes
   emission volumes read star-lit; volumes are document-authored via the
   `volume` block / `EmissionVolume` component (entity texture supplies
   the emission image; `flow`/`distort` re-pose the marched filaments so
   sibling nebulae don't repeat). Remaining: the deck is
   a texture-space composite — no volumetric cloud shells or cloud
   self-shadowing; banding is a static two-harmonic warp (`band_shear`
   + `band_waves` jets), not animated turbulence.
8. **Quality tiers** — landed: Low/Medium/High/Ultra gate bloom,
   sharpen, MSAA, aniso, cubic magnification and emission-volume steps.
9. **Editor** — scene3d tool exposes every material field (PBR, surface
   maps, atmosphere, limb/beaming/shear/scatter, presets, volume, LODs),
   render options, and preview debug modes. Remaining: no live lighting
   or per-camera authoring inside the tool.
10. **Fleet scale** — `maximum_scene3d_instances=4096`, CPU-side uniform
    fill per instance. Instancing is real but bounded by per-frame CPU
    record build. `visible_range` distance culling landed (phase 16) and
    screen-space mesh LOD chains (`lod_meshes`/`lod_pixels`, ≤8 halving
    levels, shared streamer/draw selection, `lod_instances` stat) landed
    later, along with the `lod_fade` screen-door band, `card:w,h`
    billboard impostor meshes and the `visible_fade` dithered range
    fade-out — still no hierarchical LOD trees.

## Top wins (ordered)

Status 2026-09-25: items 1–6 are landed and GPU-verified, including the
preview debug views and the per-effect quality gates (Low disables
aniso + cubic magnification and caps emission-volume marching at 16
steps, Medium at 32; bloom Medium+, sharpen High+, MSAA Ultra).
Per-instance `visible_range` distance culling is landed: culled
instances skip both the draw and their TextureStreamer residency demand.
`visible_fade` (fraction of `range`, [0,.5], default .15) dithers the
draw out through the same screen-door mask ahead of the cull edge —
ranged objects fade instead of popping; `visibleFade` authors it per
entity and `visible_fades` counts thinned draws.
Directional shadow mapping is landed: `ShadowMap3D` ortho coverage ahead
of the camera, depth-only `scene3d_shadow` pass through the RenderGraph,
8-tap PCF at High/Ultra, tier-scaled resolution, Low-tier skip,
`shadow_casters` workload counter. Planet surface detail is landed:
`SurfaceResponse3D` is reachable from authored documents/components
(any map subset), `cloud_albedo` turns the cloud map into a lit deck,
and `terminator_wrap` applies wrap-diffuse to all light types.
Limb darkening (`Material3D::limb_darkening`, linear N·V law) keeps
self-luminous star discs from clipping flat; `star_photosphere3d(kelvin)`
+ `starKelvin`/`StarPhotosphere` derive a full spectral-class star
material (blackbody tint, emissive-dominant, temperature-graded limb
coefficient). `accretion_disc_material3d(inner,outer,kelvin,beaming)`
generates a Shakura–Sunyaev thin-disc radial column (`T ∝ r^(−3/4)`,
flux ∝ T⁴, per-texel blackbody) for `annulus:i,o` meshes with the
orbital-beaming lane asymmetry — black holes compose it with a dark
sphere rather than needing an engine concept. Screen-space mesh LOD
chains are landed: `lod_meshes`/`lod_pixels` swap to coarser meshes by
projected bounding diameter (halving per level), with the streamer
demand and draw submission sharing `select_lod3d_level` so only the
submitted level holds residency. `lod_fade` [0,.5] widens each switch
into a screen-door transition band — inside it the view submits both
levels and a signed interleaved-gradient-noise mask (`uv_options.w`)
keeps exactly one per pixel, an opaque crossfade with no blending or
z-fight; Low tier and `lod_fade=0` keep the hard switch, and
`lod_fades` audits dual submissions. A fleet benchmark block in
`native_scene3d_gpu` times a 1024-instance depth-sweep fleet over 60
frames (`fleet3d` line: submission/wall means, draw calls, LOD picks —
one instanced draw per level). Also
fixed: streamer registrations keyed by `RgbaImage*` are now
liveness-verified (`weak_ptr` owner), closing a stale-TextureId reuse
bug that intermittently skipped mip-tail promotions; and the per-frame
`Scene3DStatistics` counters (`draw_batches`, `submitted_instances`,
`lod_instances`) now actually reset each `prepare()` — they were
documented per-frame but accumulated. See
`docs/VISUAL_ENGINE_HANDOFF.md`.

1. **PBR material block**: metallic + scalar/map roughness driving the
   existing GGX, emissive map × tint × strength with optional
   night-side gate (colony lights, engine glow, windows), alpha cutout,
   UV tiling, and a shared equirect environment giving diffuse
   irradiance + specular reflection to ordinary materials (phases 2,5,7).
2. **Post stack**: per-view `RenderOptions3D` — exposure, mip-chain bloom
   (generate HDR mips by blit chain, sample in tonemap — no extra pass or
   shader pair), contrast, saturation, unsharp sharpen; MSAA resolve gated
   by quality tier (phase 6, 19).
3. **Point lights**: up to four view-space point lights with windowed
   inverse-square attenuation feeding the same diffuse+GGX path (phase 3).
4. **Atmosphere rim**: wavelength-tinted limb scattering, day-side
   weighted, parameterized density/exponent per material (phase 8).
5. **Editor**: entity material fields (metallic, roughness, emissive,
   tiling, cutout, atmosphere) + document render options; preview debug
   modes (Lit/Unlit/Normals/…) (phase 17,18).
6. **Quality tiers** LOW/MEDIUM/HIGH/ULTRA gating bloom taps, sharpen,
   MSAA, aniso, cubic magnification, emission-volume steps (phase 19).

## Explicitly deferred / blockers

- Cascaded shadow maps (CSM splits for extreme zoom ranges) and
  point-light shadows: the single `ShadowMap3D` ortho volume covers
  authored mid-zoom strategy scenes; analytic blockers still cover
  planet↔ring. Documented limitation.
- Indirect draws / GPU culling: SDL_GPU does not yet expose
  `SDL_DrawGPUIndexedPrimitivesIndirect` paths here; CPU record build is
  the known bound. Not a blocker at strategy scale (4096 cap).
- True atmospheric multi-scatter, ray-traced occlusion: out of scope
  for the SDL_GPU forward renderer; approximations land per-phase with
  quality budgets. Volumetric nebulae land as the existing bounded
  emission-volume march plus `SurfaceEffect3D::volume_scatter` — a
  directional limb gradient that makes the cloud read star-lit.
