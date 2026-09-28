# Cinematic asset provenance

Generated for Stellar Continuum on 2026-09-10 through the OpenAI image generation tool. No Stellaris screenshot was embedded or shipped. Those screenshots serve only as the user's visual references. Both assets are candidates pending further art review, not representations of a completed production-art pipeline.

## Deep field

Destination: `assets/visual/space/deep-field-v2.png`.
Generation output: `exec-a4678ad5-1dbd-4b62-b09a-e7ef01e2cc56.png`.

Prompt: Use case: scientific-educational. Asset type: full-screen photographic deep-space environment for a professional space strategy game. Create a wide 16:9, extremely high-detail astronomical deep field that fills EVERY part of the frame with fine pin-sharp stars and dozens of small realistically rendered distant galaxies: face-on spirals, inclined barred spirals, edge-on dust-lane galaxies, elliptical galaxies, irregular galaxies, in varied small sizes and distances. Dark blue-black space with faint wisps of indigo dust, subtle cool blue and warm ivory stellar colors. Galaxies distributed across left, right, top, bottom, and center, never clipped by frame edges, no huge central galaxy (the interactive main galaxy is rendered separately). Most galaxies tiny, a handful about 3–5 percent frame width with resolved spiral structure and golden cores. The space should feel vast, calm, realistic and expensive, like a deep astronomical exposure. Avoid cartoon shapes, Saturn-like ringed icons, geometric ellipses, opaque cloud walls, text, labels, UI, watermarks, borders. Final output landscape, max available resolution.

## Barred spiral layer

Destination: `assets/visual/space/milky-way-layer-v2.png`.
Generation output: `exec-4f0e00fa-1397-4f8e-939d-3f81b9b45e9b.png`.

Prompt: Use case: scientific-educational. Asset type: isolated astronomical galaxy layer for an interactive space strategy map. Create ONE complete centered FACE-ON barred spiral galaxy resembling the Milky Way, with four coherent winding arms, viewed straight down from its north galactic pole. The core must be at the EXACT CENTER of the image. Entire galaxy visible with comfortable margin all around, circular disk occupying 85 percent of square frame. A small intense pearl-gold core, subtle elongated central bar, magnificent fine blue-white star clouds, warm brown dust lanes, delicate violet nebula knots. Photorealistic telescope-quality luminous detail, natural irregular filament structure, sophisticated color, no solid outlines, no rings like Saturn, no graphic symbols. Transparent background, smoothly feathered tenuous outer halo fading to genuine alpha, no rectangle, no star field outside the galaxy. Very sharp detail for game zoom levels. No planets, no text, no UI, no labels, no watermark. Square composition.

## Runtime composition

The galaxy layer provides astronomical texture; selectable stars and fleet icons render separately. Local skies use a shader and seeded star positions rather than this image. Planet spheres retain the existing credited Sol material sources. Orbital stations are original programmatically constructed 3D meshes with material lighting and four build stages. These meshes are deliberately documented as temporary visual assets even though their construction behavior is maintained gameplay.

## Galactic dust detail material

Destination: `assets/visual/space/galactic-dust-detail-v1.png`.

Created September 11, 2026 with the built-in OpenAI image-generation tool. It is original generated game artwork, not an astronomical observation or licensed third-party photograph. Actual output dimensions are 1254 × 1254 pixels (the requested size was 4096 × 4096). SHA-256: `281662F113633DD807E68BCD97D94A9BE0ED48182A4CE9776E6007CD2523EF48`.

The live galaxy shader samples this as repeat-enabled, mip-filtered fine cloud material in two transformed texture spaces, then masks it by the generated galaxy frame. It does not create selectable stars, routes, or simulation data; catalogue markers remain separately drawn and procedural detail retains native zoom clarity.

Prompt: Use case: stylized-concept. Asset type: original high resolution galactic dust material for a real-time space strategy game. Generate ONE square 4096x4096 texture, full bleed. Subject: a detailed astronomical field of interstellar stellar haze and irregular dark dust filaments, as if a very high resolution exposure of the diffuse light in the Milky Way's spiral arms. Dense overlapping, wispy, branching charcoal dust lanes against ivory and very restrained slate-blue stellar haze; delicate warm amber traces. Natural multi-scale fractal turbulence, pinprick unresolved stars integrated into haze, realistic photographic fine detail, no obvious smooth artificial swirls. Composition: evenly varied texture across entire square; no central galaxy or spiral shape, no center focal point, no large isolated bright star; this material will be masked into the live generated galaxy shape by a shader. Medium contrast with detail in dark regions. Seamless/tileable edge continuity where possible. No text, borders, planet, recognizable galaxy silhouette, UI, logo, frame or watermark. Original game production material, astrophotographic realism.
