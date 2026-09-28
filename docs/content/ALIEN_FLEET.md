# Alien fleet — metal construction collection

Updated 2026-09-14 UTC. Version 0.2.0. **Library candidates; final art review and game integration pending.**

This revision follows the user's request for realistic metal construction like the
human ship artwork. The 0.1.0 collection is retained as a superseded blockout.
The actual 3D hulls and equipment now have segmented metal plating, fasteners,
service hatches, vents and textured metal surfaces. Large decorative glowing
features have been removed. These models are an improved construction pass;
they still need further detailing to match the generated design reference.

The three current nonhuman species each receive six original role-specific hulls.
Each family has different geometry, materials, habitat structure and proportions.
An offline fitting workshop attaches separate equipment models to real named
hardpoint nodes, checks slot class and proposed fitting budgets, and saves or
loads blueprints. It can export the fitted ship as a GLB model.

Open `assets/models/alien-fleet-v2/Alien-Fleet-Workshop.html` in a browser. No server,
installation or Internet connection is needed after delivery. Choose a species and
ship, then **Role loadout** for an example. Select a port, choose compatible
equipment, and attach, replace or remove it. **Focus port** makes equipment on the
very large carriers inspectable; **Whole ship** restores the overview. Switching
ships starts a fresh bare hull; save a blueprint first to retain a custom fitting.

## Species and silhouettes

| Species | Canonical body and habitat | Authored design language | Proposed clearance allowance |
| --- | --- | --- | --- |
| Pelagic High-Pressure | Radial swimmer, 2.10 × 0.80 m, four primary manipulators; immersion at 282 K / 350 kPa; 0.85 g | Broad protective mantle, rounded pressure vessels, circulation trunks and swept keels; pearl, slate and teal | 3.2 m swimways, 2.8 m turning volume |
| Compact High-Gravity | Horizontal quadruped, 1.35 × 0.75 m, two primary manipulators; 300 K / 160 kPa; 1.75 g | Squat broad wedge, stepped armor, reinforced cross-members and paired heavy drives; charcoal, copper and amber | 2.0 m clear height, 2.6 m turning width |
| Cryogenic Hydrocarbon | Radial multiped, 1.20 × 1.00 m, six primary manipulators; 94 K / 150 kPa; 0.14 g | Sixfold faceted cold habitats, long thermal separation, isolated drive booms and radiator fins; frost ceramic, graphite and violet | 2.4 m radial bays, 2.8 m turning diameter |

Biology comes from `src/Game/Simulation/Species/SpeciesCatalog.cs`. Hull names,
clearance allowances, dimensions and material choices are new content proposals.
The GLBs contain exterior geometry; they do not contain modeled walkable interiors
or establish pressure, thermal, structural or radiation performance.

## Hull catalogue

Dimensions are **length × beam × height in metres**, measured on the base hull
excluding optional equipment and small attachment plates. Carrier sizes are
provisional and unusually large because of the existing population reservations.

| Existing role ID | Pelagic | High-Gravity | Cryogenic |
| --- | --- | --- | --- |
| `warp_scout` | Tidefinder — 155 × 94 × 54 | Cairn — 116 × 96 × 36 | Pale Thread — 184 × 98 × 78 |
| `science_vessel` | Songweaver — 260 × 170 × 95 | Deep Anvil — 185 × 168 × 52 | Aurora Lens — 330 × 224 × 150 |
| `patrol_corvette` | Reefguard — 210 × 142 × 72 | Bulwark — 168 × 156 × 49 | Frost Lance — 248 × 142 × 110 |
| `bulk_freighter` | Tidehold — 520 × 280 × 178 | Loadstone — 390 × 320 × 102 | Cold Vault — 650 × 380 × 248 |
| `resource_outpost_ship` | Shoalforge — 4,200 × 2,400 × 1,500 | Foundry Seed — 3,200 × 2,100 × 1,000 | Rime Seed — 4,500 × 2,600 × 1,700 |
| `colony_ship` | Great Current — 12,000 × 8,000 × 3,600 | Mountainhome — 9,500 × 6,500 × 2,400 | Long Winter — 13,000 × 7,600 × 4,200 |

The registry currently assigns 24 / 72 / 85 / 60 / 180 / 320 crew to these roles.
It reserves **8 million people for an outpost carrier and 250 million for a colony
ship**. This collection retains those values and uses a literal large-ark visual
interpretation. It does not silently change them to smaller populations or invent
a convoy mechanic. Carrier scale must be reviewed with shipyards, docking,
navigation and campaign presentation before runtime adoption.

As a rough early volume check, the design allowances are 90 m³ per Pelagic,
55 m³ per High-Gravity and 65 m³ per Cryogenic passenger, including a share of
habitat support. Multiplying the exterior bounding box by a provisional shape
allowance (0.12 / 0.18 / 0.10 respectively) leaves more volume than that passenger
allowance in each carrier. These are planning estimates, not measured usable
interiors; bounding boxes contain voids and cannot prove capacity. Actual packing,
stores, immersed water mass, thermal isolation and transit duration remain open.

## Hardpoints and visible equipment

| Role | Weapon | Utility | Defense | Cargo | Thermal | Total | Proposed points / power |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Scout | 2 | 2 | 1 | 0 | 1 | 6 | 14 / 18 |
| Science | 2 | 4 | 2 | 0 | 2 | 10 | 24 / 32 |
| Corvette | 6 | 2 | 2 | 0 | 2 | 12 | 36 / 44 |
| Freighter | 2 | 2 | 2 | 4 | 2 | 12 | 26 / 30 |
| Outpost | 4 | 4 | 4 | 4 | 2 | 18 | 42 / 50 |
| Colony | 6 | 4 | 4 | 6 | 2 | 22 | 56 / 70 |

There are 12 equipment families, each with three matching species treatments:
beam turret, kinetic turret, missile battery, point defense, sensor array, command
relay, shield emitter, armor plating, cargo pod, habitat support pod, radiator and
auxiliary power unit. Mount classes S/M/L use 6/12/24-metre nominal interfaces.
Smaller compatible equipment can use a larger mount; equipment is never stretched
to the host ship's length. Over-budget and wrong-kind fittings are rejected.

Nodes are named `HP_W01`, `HP_U01`, `HP_D01`, `HP_C01`, `HP_T01`, etc. Each records
kind, maximum size, position and rotation in the manifest and GLB extras. The
matching integral plate stays on the hull when equipment is removed. Base hulls
start with **zero optional equipment**; essential engines, habitat shells, thermal
structure, command enclosure and docking collars are built in. Decorative lights
and structural shell plates do not grant shield or armor gameplay bonuses.

Units are metres, +Y up, −Z ship forward. Module local +Y points out of the mounting
face and −Z is its forward direction. Ventral ports rotate the outward direction;
their forward remains −Z. Both detail levels preserve identical attachment nodes.
Weapon, sensor and relay subparts are named meshes, but this version contains no
animation rig, turret tracking, firing effects or collision hulls. Attachment is
visual and budget-checked; physical clearance, recoil paths and firing arcs still
require integration review. Integral docking collars are visible mesh details,
not a completed animated docking or crew-transfer system.

## Files and reproducibility

- `assets/models/alien-fleet-v2/Hulls`: 18 bare GLBs plus 18 reduced-detail GLBs.
- `Modules`: 36 separate GLBs with matching materials and mount origins.
- `fleet-manifest.json`: semantic asset IDs, role/species mappings, dimensions,
  sockets, examples, file hashes and integration status.
- `Alien-Fleet-Workshop.html`: bundled offline 3D inspection and fitting tool.
- `Renders`: original model renders, fleet sheet and fitting before/after images.
- `References`: generated realistic metal construction illustration, explicitly
  a design reference rather than a finished model render.
- `Textures`: original generated metal surface and derived runtime albedo,
  roughness/metalness and normal maps, embedded in the exported models.
- `ART-PROVENANCE.md`: input references, exact generation prompts and limits.
- `example-blueprint.json` and `example-fitted-corvette.glb`: tested roundtrip.
- `validation-report.json`, `workshop-test-report.json`: completed checks.
- `tools/alien-fleet`: editable procedural mesh source, viewer source, exporter,
  validation script, exact dependency lock and licenses.

Rebuild with Node.js: `npm ci --prefix tools/alien-fleet`, then
`node tools/alien-fleet/build.mjs`. Run `node tools/alien-fleet/verify.mjs` with
Playwright available (set `PLAYWRIGHT_MODULE` if needed). Rendering requires WebGL.
The source is editable code plus portable meshes, not a Blender scene.

Three.js 0.180.0 supplies geometry, rendering and GLB export/import. Esbuild 0.25.9
bundles the offline workshop. Khronos glTF Validator 2.0.0-dev.3.10 checks exports.
The texture preparation/exporter uses @napi-rs/canvas 0.1.80. Third-party notices
travel with the collection. Models are original project-authored geometry.
Images in `Renders` show those actual models. The illustration in `References`
and source image in `Textures` were created with the built-in image-generation
tool; exact prompts and provenance are included. No borrowed spacecraft meshes
are used. Model and material-source hashes are in the manifest.

The 72 core GLBs total approximately 114 MiB; each is below the library's 25 MiB
file limit. The full collection exceeds the editor's 100 MiB embedded-project
budget. Keep the complete master set in Engine Assets and select the required
hulls, detail levels and equipment for each project. Runtime optimization remains
part of integration.

## Remaining integration work

1. Resolve visuals using crew species plus the existing ship design ID. Do not
   replace persistent gameplay design identities with art asset IDs.
2. Connect GLB loading, material handling, LOD selection and strategic scale to
   the native renderer; measure crowded scenes and consolidate draw calls.
3. Bind equipment to authoritative research, ownership, construction, power,
   combat, refits and saved loadouts. Workshop blueprints are design files, not
   campaign saves, research unlocks or construction orders.
4. Review carrier scale and real habitat budgets, docking clearances, module
   overlap, weapon arcs, thermal separation and collision meshes in context.
5. Complete the final art/detail pass, construction/damage states, tracking and
   effects, small map icons and shipyard/fleet UI adapters.

This collection is reusable in Engine Assets now. Its availability does not mean
that the C++ migration or playable native ship customization is complete.
