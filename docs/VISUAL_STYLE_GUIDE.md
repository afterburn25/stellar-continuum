# Stellar Continuum Visual Style Guide

Status: **Early-release visual standard — cinematic revision, 2026-09-10**

Owner: `work/visual-style-assets`

Working direction: **Cinematic Strategy**

## Current player reference

The primary reference is 1920×1080. At 1280×720 the interface reflows: research uses one column, operations cards use two columns, resource spacing tightens, and longer pages scroll. Text and mouse targets retain readable logical sizes. At 1440p and 4K the interface retains the 1080p composition while celestial scenes render at native viewport resolution.

Use the user's Stellaris screenshots as references for hierarchy, atmosphere and restraint. All shipped interface artwork, shaders and geometry must retain project provenance. The current palette uses dark graphite-green panels, ivory text, compact icon navigation, fine illuminated frames and limited gold emphasis. Remove bottom command strips; the left rail and contextual right inspector provide mouse navigation.

Galaxy overview shows a complete galaxy with a full-frame deep field of distant galaxies. Its camera fits both artwork and the generated system extent in the remaining map area. Local system and planetary orbital views never show distant galaxy images: each system receives a deterministic star configuration and nebula palette, retained when revisited. Planet details must respect survey knowledge.

Orbital structures are selectable 3D models. Their appearance and construction stages read authoritative infrastructure progress. Surface colonies use a right-side construction and operations panel, with separate labeled population, employment, power, reserve, housing and hub facts. Building upgrades keep the old facility operating until their timed work completes.

The broader principles below remain guidance where consistent with this revision.

This document is the shared visual reference for early-release presentation work. UI layout, navigation and interaction remain owned by `work/ui-player-experience`; this guide defines the visual language and reusable graphical resources those screens should consume.

## 1. Overall visual philosophy

Stellar Continuum should look like a civilization-scale scientific command environment rather than a decorative sci-fi dashboard.

The visual identity is **Deep-Space Instrumentation**:

- deep navy-black space as the visual field;
- layered graphite/navy interface surfaces rather than pure black cards;
- precise keylines, restrained radii and compact spacing;
- cool cyan/blue interaction highlights;
- semantic subsystem accents used sparingly;
- information-rich screens with strong hierarchy and generous local contrast;
- astronomical atmosphere coming from scale, celestial rendering and negative space, not excessive chrome;
- uncertainty and fog of war represented intentionally rather than visually “filling in” unknown data;
- a technological feel that can plausibly begin in 2050 and become more advanced without requiring a full art-style reset.

The player should feel that they are operating a serious strategic information system overlooking an enormous physical universe. Ornament must never compete with actionable data.

### Anti-goals

Avoid:

- neon on every edge;
- hologram noise behind ordinary text;
- fake scanlines or CRT degradation that reduce legibility;
- heavy beveled “space metal” frames;
- excessive gradients;
- constant pulsing/animation;
- color-only state encoding;
- unrelated icon styles;
- generic stock-space art as production UI;
- embedded fake UI text inside backgrounds;
- decorative details too fine to survive 16–24 px use.

## 2. Color system

Canonical raw values live in `assets/visual/ui/visual_tokens.json`.

| Role | Hex | Use |
|---|---|---|
| Canvas | `#050B12` | Deep-space background/canvas |
| Primary surface | `#07131F` | Main panels and menus |
| Secondary surface | `#0D1D2B` | Nested panels and grouped controls |
| Raised surface | `#13283A` | Hover/raised emphasis where a surface change is useful |
| Keyline | `#274359` | Borders, separators, inactive outlines |
| Primary text | `#E6F0F6` | High-priority labels and values |
| Secondary text | `#A9BBC8` | Explanatory and secondary labels |
| Muted text | `#6F8494` | Low-priority metadata |
| Selected / exploration | `#58CFFB` | Selection, player-driven exploration, active map focus |
| Focus / hover | `#93E2FF` | Keyboard focus and short-lived hover emphasis |
| Success | `#64D6A5` | Completed/healthy/confirmed |
| Caution | `#EBCB67` | Attention without immediate failure |
| Danger / hostile | `#FF716C` | Failure, hostile threat, destructive action |
| Unknown | `#8B82A2` | Unresolved/unknown/fogged information |
| Disabled | `#526574` | Unavailable controls or inactive data |
| Science | `#AF8FFF` | Research/science category accent |
| Economy | `#E9B65C` | Economy/resource category accent |
| Construction | `#F1975B` | Construction/industry progress accent |
| Diplomacy | `#5FD2C0` | Diplomatic/contact category accent |
| Military | `#FF776E` | Military/combat category accent |

### Color rules

1. Do not use subsystem accents as large panel backgrounds. They are signals, not wallpaper.
2. Selected state uses cyan plus a shape/border/ring change.
3. Success, caution, danger, unknown and disabled states must also use icon shape, text, pattern or position. Never rely on hue alone.
4. Red and green must not be the only way to distinguish opposing map states.
5. Unknown information should be visually restrained, not merely “dark red” or “gray enemy”.
6. On dense maps, preserve luminance separation between object class, selection state and fog/survey state.
7. Natural celestial colors are allowed to depart from the UI palette when physically motivated, but strategic overlays should remain in the shared system.

## 3. Typography

No third-party font is bundled in v1. Godot's fallback font remains a **temporary runtime dependency** until the project vendors a font with explicit redistribution terms (preferably SIL Open Font License or equivalent).

### Hierarchy

| Role | Target size | Rules |
|---|---:|---|
| Working-title / major screen title | 28 px | Sparse use; not for ordinary panel headings |
| Primary heading | 20 px | Major screen or modal hierarchy |
| Section heading | 16 px | Grouped information or subsystem headings |
| Normal UI | 14 px | Buttons, labels, list rows |
| Secondary data | 12–13 px | Only where density requires it and contrast remains sufficient |

12 px is the practical lower bound for normal desktop early-release UI. Do not solve layout pressure by shrinking critical text below that threshold.

### Typography rules

- Use sentence case for ordinary controls and explanatory text.
- Short navigational/system labels may use uppercase when they remain easy to scan.
- Avoid long all-caps paragraphs.
- Use moderate tracking only for short titles or category labels.
- Numeric values should align consistently within a given data column.
- Use stylistic display type only for branding/title treatment, not simulation readouts.
- Do not introduce fonts without documented redistribution provenance.

## 4. Shapes and geometry

The visual system is precise, compact and restrained.

- Standard panel/card radius: **6 px**.
- Small control radius: **4 px** where needed.
- Standard keyline: **1 px**.
- Spacing scale: **4 / 8 / 12 / 16 / 24 / 32 px**.
- Primary panels use dark filled surfaces plus a single keyline.
- Nested panels should usually change surface tone before adding another heavy border.
- Separators are low-contrast keylines, not glowing bars.
- Selected map objects use a distinct ring/bracket/outline in addition to color.
- Critical hostile/unknown states should have distinguishable silhouettes or glyphs.
- Buttons should read as controls before they read as decoration.

Avoid excessive cut corners, asymmetrical sci-fi framing and repeated ornamental notches unless they communicate a real state.

## 5. Icon language

The early-release production icon family uses:

- `24 x 24` SVG viewBox;
- approximately `2 px` safe padding;
- `1.8` stroke weight;
- round line caps and joins;
- flat orthographic symbols;
- no perspective;
- minimal detail;
- neutral light source color `#E6F0F6`;
- transparency around the symbol;
- tinting/modulation at runtime when a semantic state requires color;
- recognizability at 16, 20, 24, 32 and 48 px.

### Icon state rules

- **Neutral:** light primary glyph.
- **Selected:** selected cyan plus a selection outline/ring or contextual selected state.
- **Disabled:** reduced-contrast disabled token; preserve recognizable silhouette.
- **Hostile:** hostile accent plus hostile/target/crossed shape where appropriate.
- **Unknown:** unknown accent plus question/hex/dashed/obscured symbol rather than color alone.

Do not create a second representation for an existing concept without updating `docs/ASSET_MANIFEST.md`.

## 6. Imagery

### Space and stars

Space should be deep and spacious. Star fields are not wallpaper noise; density and brightness must preserve strategic object visibility.

Stars should retain physically motivated variation while interaction overlays use the shared strategic palette.

### Planets and moons

Prefer coherent procedural/material treatments that can vary atmosphere, surface class, temperature and illumination without requiring thousands of hand-authored bitmaps. Strategic thumbnails should preserve silhouette and broad class recognition at small sizes.

### Ships

Ship imagery should communicate role and technological era before decoration. Ship-role iconography must follow authoritative roles from Shipbuilding/Fleets. The visual branch does not invent ship classes.

### Colonies and orbital infrastructure

Use recognizable structural silhouettes and status overlays. A colony, outpost, shipyard and orbital facility must not be differentiated only by color.

### Scientific imagery

Favor diagrams, sensor language, spectra, topographic/field cues and clean technical abstraction over fantasy circuitry.

### Factions and civilizations

Faction hooks should be readable without turning every screen into a faction color wash. Unknown civilizations remain visually ambiguous until player knowledge supports stronger identification.

## 7. Galaxy/system-map visual language

The map must remain legible under object density.

### Required state distinctions

- **Unknown:** subdued unknown symbol/treatment; do not reveal class details.
- **Detected:** point/symbol with limited information.
- **Partially surveyed:** distinct partial ring/arc or segmented marker.
- **Surveyed:** complete class marker and allowed data.
- **Selected:** cyan selection ring/brackets independent of object color.
- **Colony:** colony glyph/ring.
- **Fleet:** role glyph or marker rather than only a colored circle.
- **Hostile:** hostile shape plus hostile accent.
- **Claim/dispute:** boundary/pattern treatment plus label/legend where needed.
- **Mission active:** directional/mission marker that does not expose hidden destination information beyond legitimate knowledge.

Fog of war is an information contract. Presentation must not visually reveal star class, deposits, civilization identity, colony viability or other facts the simulation has not exposed.

## 8. Motion and effects

Motion should confirm change, not continuously demand attention.

- Micro transitions: roughly **120 ms**.
- Standard UI transitions: roughly **180 ms**.
- Selection can use a brief settle/highlight, not endless pulsing.
- Scanning animation is appropriate only while an actual scan/survey action is active.
- Notifications may pulse briefly when urgent, then settle.
- Combat feedback may use short directional flashes, impact markers and damage-state changes.
- Avoid flashing patterns.
- Respect reduced-motion preference when implemented.
- Do not animate large background layers continuously unless the cost and readability impact are proven acceptable.

## 9. Accessibility

Accessibility is part of the production standard, not a later reskin.

- Important text/background pairings must maintain strong contrast.
- Do not use color as the sole critical state indicator.
- Map markers need distinguishable silhouettes.
- Success, caution, hostile, unknown and disabled states need shape/icon/text reinforcement.
- Keep critical normal UI at 14 px where possible.
- Avoid unnecessary flashing.
- Maintain visible keyboard focus.
- Dense map overlays must remain distinguishable for common red/green color-vision deficiencies.
- Tooltips/labels should be available for unfamiliar icons in UI implementations.

The automated gate checks normal-size primary text on primary/secondary/raised surfaces,
and secondary/muted/pressed-focus text on the primary surface at a minimum 4.5:1 ratio.
Muted text is reserved for the primary surface or darker backgrounds: it is not a
general-purpose small-text color for raised panels. These token calculations do not
replace review of composited backgrounds, text scaling or actual keyboard navigation.

## 10. Performance and technical asset rules

- Prefer SVG for compact UI icons.
- Keep SVG path complexity low.
- Avoid external links, embedded fonts, scripts or raster data inside production SVG icons.
- Raster backgrounds should be sized for their actual presentation rather than committed as oversized source exports.
- Avoid unnecessary transparency layers and particle overdraw.
- Prefer reusable procedural/material systems for celestial variety where practical.
- Runtime assets should be optimized; source-quality art belongs in-repo only when it is useful and reasonably sized.
- Large binary source files require an explicit repository/LFS decision.

## 11. Godot integration

Runtime visual resources live under `assets/visual/`.

Current reusable resources:

- `assets/visual/ui/visual_tokens.json` — human/tool-readable raw design tokens.
- `assets/visual/ui/stellar_continuum_theme.tres` — production-candidate Godot Theme for core panel/button/label treatment.
- `assets/visual/icons/core/*.svg` — the first production icon family.

`project.godot` binds the shared Theme project-wide through `gui/theme/custom`. Current and future Control nodes therefore inherit the common visual treatment unless a deliberate local override is required.

The UI branch should consume these resources rather than copying subtly different values into scenes. Layout and interaction remain UI-owned. Local Theme variants are acceptable for genuinely different components, but they should derive from the same token system.

The production SVG contract is validated by `scripts/validate_visual_assets.py`, and the repository's normal `work/**` build additionally runs Godot headless editor/runtime smoke tests.

That validator compares every runtime palette RGB value and the Theme's role colors,
intentional alpha values, text sizes, panel/control geometry and focus bindings with the
shared contract. `scripts/test_validate_visual_assets.py` exercises rejection of unsafe
SVG/style/reference content, unregistered assets, palette/Theme drift and low-contrast
text even when its three color representations agree. All canonical color roles,
including `Exploration`, are available through `VisualPalette`.

## 12. Working-title branding

`Stellar Continuum` remains a working title unless `docs/BRANDING.md` records completed commercial clearance.

Development title treatment should be:

- uncluttered;
- legible at 1280x720 and above;
- serious rather than pulpy;
- usable over deep-space canvas without a boxed logo;
- free of third-party marks.

Do not use trademark symbols until clearance is documented.

## 13. Change discipline

When a new visual standard or production family is introduced:

1. update this guide when the rule changes;
2. update `docs/ASSET_MANIFEST.md`;
3. validate assets locally/CI;
4. post `[ASSET AVAILABLE]` or `[VISUAL STANDARD]` to the workstream and coordination issue;
5. tell the consuming workstream exactly which paths are stable.

If a temporary asset is replaced, mark the old asset as superseded in the manifest rather than silently creating duplicates.
