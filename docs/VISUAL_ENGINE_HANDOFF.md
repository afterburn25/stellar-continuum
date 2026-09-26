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
m.surface_response->cloud_height = .04f;    // [0,.1] deck altitude — parallax + displaced shadows
m.surface_response->cloud_offset = {.1f,0}; // UV drift, each |≤2|
m.terminator_wrap = 0.4f;                   // [0,1] wrap-diffuse softening
m.limb_darkening = 0.6f;                    // [0,1] N.V radiance falloff
m.band_shear = -0.2f;                       // [-0.5,0.5] latitude-weighted
                                            // longitude shear (giants)
m.band_waves = 0.7f;                        // [0,1] zonal-jet harmonic
                                            // layered on band_shear
m.band_drift = 0.08f;                       // [-0.25,0.25] uv/s deck
                                            // scroll over scene time
m.orbital_beaming = 0.8f;                   // [-1,1] orbital doppler
                                            // asymmetry (accretion discs,
                                            // ring forward-scatter)
m.forward_scatter = 0.6f;                   // [-1,1] HG phase asymmetry:
                                            // +backlit boost (dusty
                                            // rings), -opposition surge
```

`star_photosphere3d(kelvin)` builds a spectral-class star material in
one call: blackbody disc tint (sRGB bytes + `linear_light` decode),
emissive-dominant response (`ambient=1` bypasses `light_color`, so the
disc shows the true Planckian color rather than a squared tint), the
star's own blackbody as `light_color`, and a temperature-graded limb
coefficient — `clamp(2.762 − 0.55·log10 K, 0.2, 0.95)`, a Claret-style
monotone falloff (convective cool stars darken more; Sun ≈ 0.69 at
5778 K). Games map O/B/A/F/G/K/M letters to kelvin at the call site —
the engine stays spectral-class agnostic. Authored scenes use the
`starKelvin` entity key; it seeds the material and explicit fields
(e.g. `limbDarken`, textures, atmosphere) still override.

`band_shear` is material-level, not tied to `surface_response`: every
equirect surface sample — albedo, normal, properties, cloud deck — shifts
`u` by `s·cos(2πv)`. The profile is equator-symmetric and zero-mean, so
authored maps stay registered and net longitude is preserved; with a
nonzero `cloud_offset` the deck additionally shears against the surface
underneath it. `band_waves` [0,1] layers a `cos(6πv)` harmonic on top —
w = 0 is the single-cosine pole-vs-equator profile, w → 1 adds
Jupiter-style alternating mid-latitude jets; the mix stays zero-mean
and equator-symmetric. `band_drift` [-0.25,0.25] scrolls the whole
warp in longitude at uv/s under `Scene3DView::options.time` — a
super-rotating deck sliding over a fixed lit limb; the same view time
advances `SurfaceEffect3D::flow_rate` [-64,64], churning emission-volume
filaments. Hosts accumulate `options.time` per frame (the runtime uses
`dt·time_scale`); at 0 every term sits at its authored phase, so
captures and save determinism are unaffected — animation is
render-side only.

`orbital_beaming` is also material-level: fragments recover their
object-space position through the stored model-view inverse, take the
tangential velocity about local +Y (`v ∝ (z,0,−x)`), and scale emitted
+reflected radiance by `1 + s·(v̂·V̂)`. A face-on disc stays symmetric —
the orbital velocity is perpendicular to the view; edge-on peaks. The
additive atmosphere rim stays exempt (it is a scattering shell, not
orbiting material). Real lensing is out of scope for the forward path —
this is the authored approximation that gives accretion discs their
asymmetric bright side.

`accretion_disc_material3d(inner,outer,kelvin,beaming)` builds the disc
itself: a Shakura–Sunyaev thin-disc radial texture (`T(r) = T_inner ·
(r/inner)^(−3/4)`, each texel mapped through `blackbody_light_color`
with emitted flux ∝ T⁴ so the inner edge burns hot while the outer rim
cools and dims), emissive-dominant response, double-sided, anisotropic
filtering, and `orbital_beaming` for the approaching-lane asymmetry. The
texture is 256×1 — authored for an `annulus:i,o` mesh at matching radii
(annulus U is radial, so the column maps straight onto the disc). The
implementation lives in `spherical_material_preparation.cpp` because
`RgbaImage::create` lives in `stellar_native_image`, which already links
`stellar_engine` — keep generated-texture factories on that side of the
dependency edge. Authored scenes use the `accretion:[i,o,k,beam]` entity
key (or the `AccretionDisc` component); an authored `texture` still wins
over the generated column. A black hole is an authoring composition —
a dark sphere inside the annulus — not an engine concept.

`SurfaceEffect3D::volume_scatter` [0,1] adds directional single-scatter
to the emission-volume march: each sample's emission scales by a limb
gradient `mix(1, .35+1.3·facing, scatter)` where `facing` measures the
sample's proxy-center direction against the object-space key light —
the star-lit side brightens ~1.65×, the far side dims to ~0.35, so
nebulae read illuminated rather than uniformly self-glowing. It rides
the `atmo_shape.z` lane: `main()` early-returns into `emission_volume`
whenever `volume_depth > 0`, so atmosphere lanes are inert on volume
materials and free to carry it. Authored scenes use the `volume` entity
block (`{depth,density,seed,steps,scatter,flow,distort,blend,image2,
occlude}` — `flow` is the filament animation phase in radians and
`distort` the spatial warp amplitude [0,.1], both re-posing the
ray-marched filaments; `image2` names a second emission texture and
`blend` [0,1] mixes it against the primary at the same warped UV, a morph
between two authored nebula silhouettes — `blend` without `image2` is a
parse error; `occlude` is an opaque sphere radius in object units
centred on the entity origin, filled per draw as a view-space sphere, so
a corona stops shining through its own star) or the `EmissionVolume`
component; the entity's own `texture` supplies the
emission image and the material turns transparent automatically — a
`volume` block without a texture is rejected at parse and dropped at
runtime, and an unloadable `image2` keeps the primary. With the camera
inside the proxy's bounding sphere the draw switches to the
double-sided pipeline and the shader marches from the camera (bit 7 of
`volume_options.y` lifts the front-face gate), so nebula fly-throughs
render the interior instead of popping to black.

Per-instance distance culling lives on `MeshInstance3D`:

```cpp
MeshInstance3D inst;
inst.visible_range = 2500.f;  // world units; 0 = visible at any range
inst.visible_fade = .15f;     // [0,.5] fraction of range; 0 = hard cut
```

`visible_fade` dithers the object out over the last fraction of the
range through the same screen-door mask the LOD crossfade uses — the
fade completes exactly at the range+radius cull edge, so the authored
disappearance distance is unchanged and a fully faded instance still
costs a (fully discarded) draw inside the sliver.

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

For extreme zoom-out a chain can end on an impostor card:
`Mesh3D::billboard_card(w,h)` (or the `card:w,h` mesh spec) builds a
quad whose `billboard()` flag makes the draw collapse its view-space
rotation to uniform scale — it always presents its face regardless of
instance or camera orientation, while position, scale and depth stay
correct. The flag is per-mesh, so a fading pair can mix a card with
solid geometry, and a primary `card:` mesh doubles as a sprite marker.

`lod_fade` (default .15, [0,.5]) widens each switch threshold into a
screen-door transition band: inside the band the view submits both
adjacent levels and the fragment shader keeps exactly one per pixel via
a signed interleaved-gradient-noise mask (`uv_options.w` — the selected
level keeps `1-p`, the coarser `p`, so the two discards partition the
silhouette with no blending and no depth fight). `lod3d_fade_share`
returns the coarser level's share `p`; `lod_fades` counts the dual
submissions, and the streamer charges the paired level's residency only
while the band is engaged. Low tier and `lod_fade=0` keep the hard
switch (one draw, zero fade cost).

For whole-cluster zoom-out, `lodGroup`/`lodProxy`/`lodProxyPixels`
(`MeshLods::group`/`proxy`/`group_pixels`) collapse a named group's
contributing members into a single view-aligned proxy draw once their
merged view-space bounding sphere projects below the authored pixel
size — a fleet or asteroid-field impostor. The first contributing
member's material shades the proxy, `lod_groups` audits replaced
members, and emission volumes are excluded (a marched volume cannot
collapse into a surface proxy). The representative's `lod_fade`
widens the collapse into a screen-door band — members thin by `1-p`
while the proxy keeps the complementary `p`; `lod_fade=0` or Low tier
keeps the hard switch. `DebugView3D::Lod` (`render.debug = lod`, class
carried on `texture_options.w`) tints each submitted draw for visual
threshold tuning — gray full mesh, a level ramp, magenta group proxy;
draws inside a screen-door band (`|keep|<1`) lift toward white so an
in-transition partition reads differently from a hard pick.

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
- Casters submit the same screen-space LOD the lit pass picks (chain
  levels, one merged-sphere proxy per collapsed group) and carry the
  signed screen-door keep mask — `ShadowCast{mat4,keep}` in the
  transform SSBO — so LOD bands, collapse bands and `visibleFade`
  dither the silhouette instead of popping it; in-band transitions
  submit each transition partner on its complementary share.
  Billboard `card:` casters ignore authored rotation and face the
  light the way they face the camera, so an impostor never shadows
  as an edge-on line.
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
| `Lod` | per-draw LOD class — gray full mesh, blue→green→yellow→orange for chain levels 1–4+, magenta group proxy; transition bands show their dithered member/proxy partition |
| `Residency` | per-draw surface-texture residency — green mip-0 resident, lime/amber/orange/red deeper tails, magenta pinned fallback |

## Authoring path — `Scene3dDocument`

Entity fields: `metallic`, `roughness`, `metallic_roughness`,
`emissive`, `emissive_strength`, `emissive_r/g/b`, `night_emissive`,
`environment`, `environment_strength`, `alpha_cutout`, `uv_tile_x/y`,
`atmo_strength/power/night/r/g/b`, `range` (per-entity
`visible_range`) with `visibleFade` ([0,.5] dithered fade-out),
`terminator_wrap`, `limb_darkening`, `bandShear`
([-0.5,0.5]) with `bandWaves` ([0,1] jet harmonic) and
`bandDrift` ([-0.25,0.25] uv/s scroll),
`orbitalBeam`/`forwardScatter` ([-1,1]), `starKelvin`
([100,100000]), `accretion` ([inner,outer,kelvin,beaming]), `volume`
(`{depth,density,seed,steps,scatter,flow,distort,blend,image2,occlude,flowRate}`
— requires a `texture`), `lods` (array of
mesh specs, ≤ 8) with `lodPixels`, and a `surface`
block —
`{normal, properties, cloud, normalStrength, relief, cloudOpacity,
cloudAlbedo, cloudHeight, cloudOffset:[x,y]}`; `surface` requires at
least one map.
Non-array `lods`, oversized chains, and `lodPixels` outside [1,4096]
are rejected.
Scene fields: `point_lights[]` (max 4), `exposure`,
`bloom`, `bloom_threshold`, `contrast`, `saturation`, `sharpen`,
`quality` ("low|medium|high|ultra"), `debug` in the `render` block
("lit|unlit|albedo|normals|roughness|metallic|emissive|lighting|lod|residency"), and
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
limb darkening, band shear, orbital beaming, starKelvin photosphere
preset, accretion disc preset (inner,outer,kelvin,beaming csv),
forward-scatter phase, mesh LOD chain (csv specs), LOD switch size and
LOD fade width.
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
- The cloud deck is a texture-space composite with a bounded altitude
  term (`cloud_height` gives limb parallax, sun-displaced ground shadows
  and zenith-gated self-shading) — still no volumetric shell or
  per-layer thickness; `band_drift` scrolls the deck over scene time
  but the warp stays a fixed two-harmonic profile, and volume
  `flow_rate` re-poses filaments without evolving their shape.
- Limb darkening is the single-coefficient linear law — no quadratic
  two-term coefficients or wavelength-dependent profiles.
- `orbital_beaming` is a first-order brightness asymmetry — no doppler
  color shift, gravitational redshift, or lensing.
- `accretion_disc_material3d` is an azimuthally uniform thin-disc
  profile — no spiral fluctuations, no relativistic ray-bending; the
  annulus radii must be re-stated in the `annulus:i,o` mesh spec.
- `volume_scatter` is a limb-gradient approximation — no real
  light-path extinction march inside the volume.
- `forward_scatter` is a single Henyey-Greenstein lobe — no
  multi-term phase functions or wavelength-dependent scattering; it
  scales radiance only, not alpha.
- One shared equirect env map per material — no probe grid.
- Bloom blur kernels are box-blitted HDR mips (narrow halo reach).
- Debug views are developer tooling — `Lod` tints the submitted
  level/proxy class and `Residency` the bound mip state; LightingOnly
  divides by sampled albedo so untextured or near-black surfaces clip
  to black.
- `visible_range` is distance culling and `lod_meshes` a flat halving
  chain; `lodGroup` collapse shades the proxy with the representative
  member's material (groups should share materials, and members still
  pay CPU prepare work). Shadow casters share the lit pass's screen-
  space pick — a chained instance casts its selected level and a
  collapsed group casts one light-facing proxy from the representative
  (the fade band's screen-door mask doesn't apply to the depth pass,
  and a fading-out instance keeps casting until the cull edge).
  Impostor
  cards (`Mesh3D::billboard_card`, `card:w,h`
  spec) face the camera but carry no baked view-dependent shading — the
  impostor image is whatever texture the instance maps onto it. The
  screen-door fades (`lod_fade`, `visible_fade`) are per-pixel dithers
  (fine up close on stills; read as noise if a coarse proxy differs
  sharply); inside a `visible_fade` band the LOD pair degrades to the
  selected level's single thinned draw.
- No indirect draw / GPU culling — CPU record build is the scale bound.
