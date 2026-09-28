# Alien fleet metal construction — provenance

Created 2026-09-13 local time (2026-09-14 UTC), following the user's explicit
rejection of the first ships as cartoonish and requirement for real metal
construction at the quality of the established human ship artwork.

The v1 model collection is a superseded blockout. The v2 collection retains the
three species, six roles, proposed sizes and detachable equipment interface while
replacing the simple finishes with assembled metal plates, machined edges,
recessed service hardware and textured physically based materials.

## Construction reference

`assets/models/alien-fleet-v2/References/realistic-metal-construction-reference.png`
was generated with the built-in image-generation tool. It is a **design reference**,
not a screenshot of the workshop or a rendered GLB. It sets the intended metal
construction quality; the actual model previews are under `Renders` and remain
subject to visual approval and final detailing. Original output was copied intact.

Inputs: `assets/visual/ships/pathfinder-scout.jpg` and
`assets/visual/ships/deep-space-science-vessel.jpg` are human quality/style references.
`assets/models/alien-fleet-v1/Renders/alien-fleet-collection.png` supplies the alien
family identities and rejected simple shapes, not the requested finish.

### Exact reference prompt

```text
Use case: stylized-concept.
Asset type: realistic spacecraft material and construction reference sheet for Stellar Continuum, a companion to editable 3D ship models.
Input images: Image 1 and Image 2 are the established HUMAN spacecraft visual quality references only: copy the realism of assembled metallic construction, fine panel seams, structural depth, machinery density, realistic light and scale. Image 3 is a layout and ALIEN DESIGN IDENTITY reference only, showing three alien fleets; its simplified plastic appearance is explicitly rejected. Do not reproduce its cartoon rendering.
Primary request: create a wide three-panel sheet of THREE extremely realistic, detailed ALIEN PATROL CORVETTES, one per species. They must look physically assembled from metal plates and industrial components, at the same realism level as the human reference ships. Preserve three substantially different alien construction languages rather than recoloring a human ship.
Left: PELAGIC / REEFGUARD — broad swept protective armored mantle over distinct cylindrical pressure vessels and immersed habitats, four-way access trunks, dark titanium and aged pale gray metal, restrained teal identification; segmented overlapping metal shell plates, pressure bulkhead bands, mechanical access gaps, subtle salt-stain-like service discoloration (in a sealed engineering sense), no smooth inflated toy blobs.
Middle: HIGH-GRAVITY / BULWARK — squat wide armored wedge, thick transverse load-bearing frames, short structural spans, stepped layered steel and gunmetal armor, burnished copper heat-resistant edges, heavily braced paired engine blocks; believable hard-surface machining, bolt patterns, recessed mechanical trenches, low wide horizontal crew passages; no plastic blocks.
Right: CRYOGENIC / FROST LANCE — faceted sixfold cold habitat structure forward, long thermally isolated mechanical spines and six individual drive booms aft, physically constructed insulated metal panels and reflective multilayer thermal foil, dark steel trusses, fine radiator fins, subtle violet identification; functional cold/heat separation, no glowing fantasy crystals.
Each ship has clear physical weapon hardpoint base plates, with two matching realistic turret assemblies fitted and at least two empty mounting plates visible. The turrets have separate bases, housings, barrels, cable and cooling connections, and are plausible removable mechanical assemblies. Small support modules and shield emitters are external detachable pieces with mounting interfaces, not painted markings. Tiny practical windows and service lights establish 150–250 metre scale. All three ships rendered same three-quarter camera direction, complete silhouettes, generous margins, no crop.
Materials: physically based real titanium, steel, anodized metal, layered armor, ceramic heat shielding and thermal foil. Fine roughness variation, restrained scratches and wear at panel edges, thin seams, access hatches, tiny fasteners, radiators, recessed vents, pipe runs. Mostly matte/satin metal with localized realistic specular highlights. NO toy smoothness, bulbous plastic, saturated candy colors, glossy clay, simplistic cubes, white glowing polka dots, oversized bridge windows or fantasy spikes.
Lighting: cinematic neutral cool key with a warm reflected fill and natural dark crevices, believable metal reflections; dark sparse space backdrop without bright nebula distractions. Dense, purposeful detail at several scales, like the human references. High-fidelity film spacecraft miniature photography, not illustration or low-poly game art.
Text only small headings at top of each panel exactly "PELAGIC", "HIGH-GRAVITY", "CRYOGENIC". Subtitle at bottom exactly "REALISTIC METAL CONSTRUCTION — DESIGN REFERENCE". No other text, logos or watermark.
```

## Metal material

`assets/models/alien-fleet-v2/Textures/hull-metal-source.png` is the original
built-in generated base-color surface scan. The 256-pixel runtime albedo retains
its texture; the ORM map derives roughness variation and a constant metal channel,
and the shallow normal map derives small gradients. These are appearance maps,
not measured material properties. The source stays unchanged. Runtime preparation
is reproducible with `tools/alien-fleet/prepare-metal-texture.mjs`.

### Exact texture prompt

```text
Use case: product-mockup.
Asset type: original seamless PBR BASE-COLOR material texture for metallic spacecraft hull plates, to be used directly on 3D models.
Create one square 1024 x 1024 tile of realistic unpainted medium-gray aerospace titanium / steel sheet surface. Orthographic straight-on surface scan, perfectly flat even diffuse illumination, no perspective, no objects, no environment, no specular highlights or baked shadows.
Very fine brushed-metal directional grain, subtle gray value mottling, restrained hairline machining marks, fine small scratches, a few faint darker maintenance smudges and slight oxidation variation. It must look like a real metal surface, not plastic or concrete. Predominantly neutral medium gray (approx #92999d), subtle cool gray variation only. Texture detail should be fine and understated at spacecraft panel scale. Keep all edges seamlessly tileable.
NO panel borders, bolts, seams, labels, logos, large damage, rust holes, graphics, saturated colors, weapons, spacecraft, perspective or lighting gradients. This is an albedo material scan; reflective highlights will be provided by the renderer.
```

## Actual geometry and limits

Mesh plates, fasteners, recesses and equipment are original procedural geometry
in `tools/alien-fleet/geometry.mjs` and `metal-construction.mjs`. Standard PBR
materials and the three runtime texture maps are embedded in GLB exports. Room
lighting is generated by Three.js RoomEnvironment and is used only by the viewer.
It is not baked into the base-color texture. No third-party spacecraft meshes
are used. The texture preparation and exporter use @napi-rs/canvas 0.1.80.

Full source images, derivative maps, source code, build/verification records and
dependency licenses accompany the versioned asset pack. Native/game integration,
final art approval, interior design, collisions, weapon arcs, animation and damage
states remain pending. The generated reference must never be represented as a
finished 3D asset or a screenshot from the game.
