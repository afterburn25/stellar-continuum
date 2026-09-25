# Visual engine handoff — Scene3D renderer APIs

Branch: `engine/space-graphics-overhaul`. Last updated: 2026-09-25.
Companion docs: `docs/SPACE_RENDERING_OVERHAUL.md` (audit/plan),
`docs/ENGINE_CAPABILITIES.md` (registry record). This file is the
integration surface — what a consumer (RuntimeHost, game client,
editor) sets to drive the new renderer features.

All types live in `stellar::native_map` unless noted.

## Materials — `Material3D` (native_scene3d.hpp)

Per-instance material. Existing fields unchanged; new opt-in fields:

```cpp
Material3D m;
m.albedo = my_texture;                      // existing authored art
m.pbr = PbrSurface3D{};                     // opt-in PBR block
m.pbr->metallic = 0.9f;                     // [0,1]
m.pbr->roughness = 0.35f;                   // [0,1] GGX alpha^2
m.pbr->metallic_roughness = tex_mr;         // optional packed map
m.pbr->emissive = tex_glow;                 // optional emissive map
m.pbr->emissive_tint = {1.f, .5f, .2f};     // [0,1] each
m.pbr->emissive_strength = 3.f;             // [0,64]
m.pbr->night_emissive = 1.f;                // [0,1] nightside/terminator gate
m.pbr->environment = env_equirect;          // shared equirect env map
m.pbr->environment_strength = 0.6f;         // [0,64]
m.atmosphere = Atmosphere3D{};              // opt-in limb scattering
m.atmosphere->tint = {0.3f, 0.5f, 0.9f};    // wavelength tint
m.atmosphere->strength = 1.2f;              // [0,8]
m.atmosphere->power = 3.f;                  // (0,8] limb exponent
m.atmosphere->night_floor = 0.05f;          // [0,1] nightside floor
m.alpha_threshold = 0.5f;                   // [0,1], 0 = off (discard)
m.texture_tiling = {2.f, 2.f};              // each in [0.01,64]
m.surface_response = SurfaceResponse3D{};   // opt-in authored detail maps
m.surface_response->normal = tex_n;         // tangent-space normal map
m.surface_response->properties = tex_p;     // packed rough/liquid/ice/height
m.surface_response->cloud_shadow = tex_c;   // alpha=cloud cover, RGB=deck
m.surface_response->normal_strength = .8f;  // [0,2]
m.surface_response->relief = 0.01f;         // [0,0.02] height-of-instance
m.surface_response->cloud_opacity = .6f;    // [0,1] surface shadow strength
m.surface_response->cloud_albedo = .7f;     // [0,1] visible deck brightness
m.surface_response->cloud_offset = {.1f,0}; // UV drift, each |≤2|
m.terminator_wrap = 0.4f;                   // [0,1] wrap-diffuse softening
m.limb_darkening = 0.6f;                    // [0,1] N.V radiance falloff
m.band_shear = -0.2f;                       // [-0.5,0.5] latitude-weighted
                                            // longitude shear (giants)
```

`band_shear` is material-level, not tied to `surface_response`: every
equirect surface sample — albedo, normal, properties, cloud deck — shifts
`u` by `s·cos(2πv)`. The profile is equator-symmetric and zero-mean, so
authored maps stay registered and net longitude is preserved; with a
nonzero `cloud_offset` the deck additionally shears against the surface
underneath it.

Per-instance distance culling lives on `MeshInstance3D`:

```cpp
MeshInstance3D inst;
inst.visible_range = 2500.f;  // world units; 0 = visible at any range
```

`prepare_instance3d` marks the instance invisible once the camera is
farther than `visible_range` + the scaled bounding radius — the same
reject path as the frustum test, so culled instances skip the draw call
*and* their TextureStreamer residency demand. Validate: finite, ≥ 0,
≤ 1e12; invalid values throw at `Scene3D::create`.

Screen-space mesh LOD chains also live on `MeshInstance3D`:

```cpp
inst.lod_meshes = {mid, low};  // spec-resolved Mesh3D chain, ≤ 8 levels
inst.lod_pixels  = 32.f;       // projected diameter that engages level 1
```

`select_lod3d_level(instance, diameter_px)` picks the level — a pure
function of the projected bounding-sphere diameter, halving the
threshold per step (`lod_pixels/2^i`). The GPU backend applies the same
pick in the streamer demand pass *and* the draw submission, so only the
level a view submits holds residency. Selection is per-view screen
space, not distance, so zoomed-out fleets shed vertex throughput without
an authored distance table. Shadow casters always take the full mesh —
the shadow volume is camera-independent, and a near receiver's shadow
must not degrade with the camera's zoom. Validation: ≤ 8 levels, all
non-null, `lod_pixels` in [1,4096]; `lod_instances` on
`Scene3DStatistics` audits the substitution count per frame.

A fleet-scale benchmark runs inside `native_scene3d_gpu`: a 1024-ship
grid spread over a depth sweep submits 60 timed frames and reports
`fleet3d frames cpu_submit_mean_ms frame_wall_mean_ms draw_calls
lod_instances`. On this CI host (Vulkan) it measures ~0.75 ms CPU
submission per frame with 1024 instances collapsed to one instanced
draw per LOD level — the number to watch as the renderer evolves.

- All PBR/atmosphere strengths default to 0 — absence of the optional
  blocks renders exactly as before (authored art untouched).
- Metallic raises specular albedo tint and removes diffuse response;
  dielectrics keep diffuse irradiance from the environment map.
- `night_emissive` gates emissive output to the nightside/terminator —
  colony lights, city windows, engine glow.
- Environment map gives ordinary materials diffuse irradiance + GGX
  specular (previously IBL existed only inside `Dielectric3D`).
- `surface_response` accepts any subset of maps — presence flags gate
  shader sampling, so a cloud-only material needs no placeholder art.
  The cloud map's alpha shadows the surface at `cloud_opacity` and its
  RGB composites as a lit deck at `cloud_albedo` — after surface
  emissive (clouds occlude night lights) and before the atmosphere rim.
- `terminator_wrap` widens the diffuse lobe — `(N·L+w)/(1+w)` — applied
  identically to the key light, additional directionals and point
  lights; 0 is exact Lambert.
- `limb_darkening` applies linear limb darkening `1 - u(1 - N·V)` to the
  body's outgoing radiance — the Sun's photosphere profile (u ≈ 0.6) —
  so HDR emissive star discs keep a physical edge instead of clipping
  flat. Applied after the cloud deck; the additive atmosphere rim is
  exempt. Uses the geometric normal, not normal-map detail.

## Scene lights — `PointLight3D`

```cpp
auto scene = Scene3D::create(camera, instances, key_light, point_lights,
                             shadow_map);
```

- Up to `maximum_scene3d_point_lights` = 4 per scene.
- Fields: `position` (world), `color`, `intensity` [0,64],
  `range` ≥ 0 — finite range uses a smooth windowed inverse-square with
  hard cutoff; `range == 0` means unbounded inverse-square.
- Point lights are independent of the key/fill directional lights and
  are unshadowed.

## Directional shadows — `ShadowMap3D`

```cpp
ShadowMap3D shadow;
shadow.extent = 64.f;       // ortho half-extent, world units
shadow.distance = 64.f;     // box centre along camera forward
shadow.depth = 256.f;       // light-axis depth of the shadow volume
shadow.strength = 1.f;      // [0,1] darkness applied to the key light
shadow.bias = 0.0005f;      // receiver-side depth bias, shadow-NDC units
shadow.resolution = 0;      // 0 = tier default (Medium 1024 / High 2048 / Ultra 4096)
```

- Strategy-scale fitting: instead of covering the camera frustum, the
  ortho box centres `distance` units along the camera forward axis, so
  the authored `extent` picks how much of the scene is shadowed —
  receivers outside the box stay lit. The light direction and camera
  orientation both track the scene's key light each frame.
- Rendered as a depth-only pass (`scene3d_shadow.vert/.frag`) before the
  scene pass through the RenderGraph; the scene fragment shader applies
  a fixed 8-tap PCF kernel at High (1-texel radius) and Ultra (1.5),
  and a single depth tap at Medium.
- Low tier skips the pass entirely (no depth target, no shader work);
  `visible_range`-culled and non-casting volumes are excluded.
  `Scene3DStatistics::shadow_casters` reports the per-frame caster
  workload.
- Shadow darkness scales the key light only — ambient, point lights,
  emissive and the analytic `AnalyticShadow3D` blockers are independent.
- `bias` is a receiver-side constant in NDC space; the rasterizer
  additionally applies a fixed slope-scaled bias (1.5) at cast time.
  Raise `bias` if grazing self-shadows band, lower it if shadows detach.

## Per-view post — `RenderOptions3D` (native_map_platform.hpp)

```cpp
Scene3DView view;
view.options.quality = Quality3D::High;     // Low/Medium/High/Ultra
view.options.exposure = 1.2f;               // linear HDR multiplier
view.options.bloom = 0.35f;                 // HDR mip-chain bloom
view.options.bloom_threshold = 0.9f;
view.options.contrast = 1.05f;
view.options.saturation = 0.95f;
view.options.sharpen = 0.25f;               // unsharp mask
view.options.debug_view = DebugView3D::Normals;  // see table below
```

Quality policy: Low = tonemap only — bloom/sharpen off, anisotropic and
cubic magnification sampling off, emission-volume ray-march capped at
16 steps; Medium = HDR mip bloom, volumes capped at 32 steps; High =
sharpen; Ultra = 4x MSAA when the device supports it. Bloom composites
over transparent background (halo spills past geometry); keep `bloom`
≤ ~0.5 and `sharpen` ≤ ~0.4 to stay inside the cinematic-but-readable
house style.

`DebugView3D` is a per-view diagnostic shading override — it reuses the
production material path (not a second renderer), so it stays faithful:

| Value | Output |
| --- | --- |
| `Lit` (default) | production shading |
| `Unlit` | tinted surface texture, no illumination |
| `Albedo` | sampled surface before tint |
| `Normals` | view-space normal ×0.5+0.5 |
| `Roughness` | active GGX roughness (PBR/response/optics scalar) |
| `Metallic` | active metallic factor |
| `Emissive` | emissive map × tint × strength + atmosphere rim |
| `LightingOnly` | shading with albedo divided out |

## Authoring path — `Scene3dDocument`

Entity fields: `metallic`, `roughness`, `metallic_roughness`,
`emissive`, `emissive_strength`, `emissive_r/g/b`, `night_emissive`,
`environment`, `environment_strength`, `alpha_cutout`, `uv_tile_x/y`,
`atmo_strength/power/night/r/g/b`, `range` (per-entity
`visible_range`), `terminator_wrap`, `limb_darkening`, `bandShear`
([-0.5,0.5]), `lods` (array of
mesh specs, ≤ 8) with `lodPixels`, and a `surface`
block —
`{normal, properties, cloud, normalStrength, relief, cloudOpacity,
cloudAlbedo, cloudOffset:[x,y]}`; `surface` requires at least one map.
Non-array `lods`, oversized chains, and `lodPixels` outside [1,4096]
are rejected.
Scene fields: `point_lights[]` (max 4), `exposure`,
`bloom`, `bloom_threshold`, `contrast`, `saturation`, `sharpen`,
`quality` ("low|medium|high|ultra"), `debug` in the `render` block
("lit|unlit|albedo|normals|roughness|metallic|emissive|lighting"), and
`render.shadow` — `{extent, distance, depth, strength, bias,
resolution}`; `extent ≤ 0` (or the key absent) disables the map.
Negative `range` and unknown `debug`/`quality` strings are rejected, as
are nonpositive `depth`, `strength` outside [0,1], negative `bias`, and
`resolution` outside [64,8192].

`spawn_scene3d` attaches `MaterialPbr`/`AtmosphereShell`/`MaterialSurface`
components (binary codec round-trips), a `VisibleRange` component when
`range > 0`, a `MeshLods` component when `lods` is non-empty,
`scene3d_from_world` exports them back, and `RuntimeHost`
maps them onto `Material3D`/`MeshInstance3D`/`PointLight3D`/
`Scene3DView::options` (unresolvable LOD specs drop just that level).
Documents without the new keys load identically.

## Editor controls — `stellar-engine.exe` Scene3D tool

Entity rows: PBR map paths + metallic/roughness scalars, emissive
path/tint/strength/night gate, environment path/strength, alpha cutout,
UV tiling, atmosphere tint/strength/power/night floor, visible range,
surface maps (normal/properties/cloud), surface scalars (normal
strength/relief), cloud deck (opacity/albedo/offset), terminator wrap,
limb darkening, band shear, mesh LOD chain (csv specs) and LOD switch
size.
Scene rows: exposure, bloom + threshold, contrast/saturation/sharpen,
quality tier, debug view, point lights (pos/color/intensity/range),
shadow map (extent/distance/depth/strength/bias/resolution).
The preview runs the real `Scene3D` + GPU path, so edits are WYSIWYG.

## Performance notes

- +2 samplers per PBR material (metallic-roughness, emissive) +1 env
  sampler, all through `TextureStreamer` budgets and mip residency.
- HDR bloom mip chain ≈ +33% of target bytes, allocated only when
  bloom > 0 and tier ≥ Medium.
- MSAA targets allocated lazily on first Ultra view, retained per
  target; `Scene3DStatistics::target_bytes` still reflects view-driven
  budgets.
- Instance cap unchanged (`maximum_scene3d_instances`); per-instance
  CPU record build remains the submission bound.
- `visible_range` culling runs before texture demand declaration —
  culled instances submit nothing and hold no GPU residency. Mesh LOD
  selection shares that pass's footprint math — only the selected
  level's geometry is charged to the frame budget.
- Low tier skips aniso/cubic samplers entirely (linear clamp/repeat
  samplers bound instead) and caps emission-volume marching at 16
  steps (Medium: 32; authored `volume_steps` applies at High+).
- The shadow pass is one extra depth-only draw set per frame (casters
  already culled by `visible_range` and the shadow volume); the depth
  target is `resolution`² D32 — tier-scaled, allocated lazily per
  target and shared across views.

## Known limitations

- `ShadowMap3D` is a single ortho cascade for the key light only —
  no CSM splits, no point-light shadows, no spot lights; receivers
  outside the authored box stay lit (by design) so extreme zoom-outs
  need a larger `extent`.
- Analytic ellipsoid/annulus blockers remain the ring↔planet shadow
  path and are evaluated independently of the map.
- Atmosphere = single-scatter limb approximation, no multi-scatter or
  aerial perspective.
- The cloud deck is a texture-space composite — no volumetric cloud
  shells or self-shadowing; `band_shear` is a single-cosine longitude
  warp, not per-band zonal winds or animated turbulence.
- Limb darkening is the single-coefficient linear law — no quadratic
  two-term coefficients or wavelength-dependent profiles.
- One shared equirect env map per material — no probe grid.
- Bloom blur kernels are box-blitted HDR mips (narrow halo reach).
- Debug views are developer tooling — no LOD/residency visualization
  modes yet, and LightingOnly divides by sampled albedo so untextured
  or near-black surfaces clip to black.
- `visible_range` is distance culling and `lod_meshes` a flat halving
  chain — no hierarchical LOD trees, screen-door fading, or billboard
  impostors yet, and shadow casters always take the full mesh.
- No indirect draw / GPU culling — CPU record build is the scale bound.
