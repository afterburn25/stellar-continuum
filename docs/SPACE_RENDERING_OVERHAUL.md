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
    liquid / ice / height), cloud-shadow map with animated UV offset.
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
3. **Shadows** — analytic ellipsoid/annulus only; no general shadow
   mapping for ships/stations, no cascade/distance policy.
4. **IBL** — environment map reachable only through `Dielectric3D`;
   ordinary materials get no diffuse irradiance or specular environment.
5. **Post** — tonemap only; no exposure, bloom, contrast/saturation
   grading, sharpen, or AA (pipelines are all SAMPLECOUNT_1).
6. **Atmosphere** — only the flat `rim_power` alpha shell; no
   wavelength-weighted scattering, no day/night limb behavior.
7. **Planet features** — cloud shadow+offset exists but no independent
   cloud albedo layer, no night-lights emissive, no terminator softening.
8. **Quality tiers** — landed: Low/Medium/High/Ultra gate bloom,
   sharpen, MSAA, aniso, cubic magnification and emission-volume steps.
9. **Editor** — scene3d tool exposes tint/texture/opacity/double_sided
   only; no material/lighting/post controls, no preview debug modes.
10. **Fleet scale** — `maximum_scene3d_instances=4096`, CPU-side uniform
    fill per instance, no LOD selection or impostors. Instancing is real
    but bounded by per-frame CPU record build. `visible_range` distance
    culling landed (phase 16) — it is a visibility cutoff, not LOD.

## Top wins (ordered)

Status 2026-09-25: items 1–6 are landed and GPU-verified, including the
preview debug views and the per-effect quality gates (Low disables
aniso + cubic magnification and caps emission-volume marching at 16
steps, Medium at 32; bloom Medium+, sharpen High+, MSAA Ultra).
Per-instance `visible_range` distance culling is landed: culled
instances skip both the draw and their TextureStreamer residency demand.
Also fixed: streamer registrations keyed by `RgbaImage*` are now
liveness-verified (`weak_ptr` owner), closing a stale-TextureId reuse
bug that intermittently skipped mip-tail promotions. See
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

- General shadow mapping (CSM + PCF): needs a depth pass + atlas plumbing;
  analytic blockers already cover planet↔ring. Documented limitation.
- Indirect draws / GPU culling: SDL_GPU does not yet expose
  `SDL_DrawGPUIndexedPrimitivesIndirect` paths here; CPU record build is
  the known bound. Not a blocker at strategy scale (4096 cap).
- True atmospheric multi-scatter, volumetric nebulae, ray-traced
  occlusion: out of scope for SDL_GPU forward renderer; approximations
  land per-phase with quality budgets.
