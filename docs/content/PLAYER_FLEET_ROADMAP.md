# Immediate player fleet and asset roadmap

Updated 2026-09-13. The user selected **player ships and matching assets** as the
first content priority while Stellar Engine conversion continues.

The initial station instruction put the **Human Modular Orbital Station first**,
covering hub, defense and shipyard/industry roles through modules and visible
upgrades, a bare-core start, modules unlocked by completed research and upward
stacking at Tiers 5 and 6 followed by a solid starbase enclosure at Tier 7.
See [the station design](HUMAN_MODULAR_STATION.md). The player-fleet
tasks below remain queued and will reuse compatible station materials and docking
interfaces after that first station asset.

The latest request adds **unique ships for every other current race** now. The
[alien fleet collection](ALIEN_FLEET.md) delivers 18 original 3D hull candidates
(three species × six roles), 18 reduced-detail variants, 36 detachable equipment
models and an offline fitting workshop. The race-specific shapes and proposed
dimensions follow the species biology; the hardpoints are real named model nodes.
The human station and Terran fleet requirements remain in the queue.

## Starting point

At the initial fleet audit, the six current designs all have dedicated presentation images and procedural
3D geometry in the game. Three dedicated ship SVGs are present: science, corvette
and colony. Before the station concept import, the editor library contained 174 entries: 130 images, 35
audio files and nine data files. It has no imported model files at this audit.
Procedural game geometry and an imported reusable model are separate assets.

This roadmap schedules ship production work. The station concept pack and alien
model collection are delivered at their stated stages. The Terran models and
other unfinished deliverables listed below remain queued.

## Production queue

| Task | Priority | Deliverable | Current state |
| --- | --- | --- | --- |
| AF-001 | Current | Three alien families, six roles each, with biological scale proposals | 18 hulls and 18 reduced-detail variants; library candidates |
| AF-002 | Current | Compatible physical hardpoints and matching equipment | 36 module models and offline fitting workshop; preview validated |
| AF-003 | Integration | Native renderer, research, refits, combat and shipyard/fleet adapters | Pending; no campaign or save changes in this asset pass |
| PF-001 | First | Fleet reference sheet, consistent materials and scale plan | Ready to start; existing references available |
| PF-002 | First | Complete Pathfinder Scout pack | Brief ready; first ship to produce |
| PF-008 | First | Shared hull, radiator, drive, docking and decal kit | Queued with the scout |
| PF-009 | First | Six matching ship icons and thumbnails | Three existing SVGs to retain/review; three gaps |
| PF-003 | Next | Deep-Space Science Vessel pack | Brief ready; reuse the scout's established materials |
| PF-004 | Next | Patrol Corvette pack | Brief ready |
| PF-005 | Following | Interstellar Bulk Freighter pack | Brief ready |
| PF-006 | Following | Sealed Resource Outpost Vessel pack | Brief ready |
| PF-007 | Following | Interstellar Colony Ship pack | Brief ready; capacity/scale review before final modeling |
| PF-010 | Alongside | Shared propulsion, survey, docking, launch and damage presentation | Audit existing cues, then create missing assets |
| PF-011 | Following | Orbital shipyard, docking and staged-assembly assets | Queued after the first ship's scale is established |
| PF-012 | Each pack | Library registration, project packaging and preview checks | Required for every completed pack |
| PF-013 | Integration | Shipyard card, fleet inspector and system-view hookup | Follows the relevant native renderer and game adapters |

After the station, the first ship batch is **PF-001 + PF-002 + PF-008**: establish the Terran
fleet's shared design and complete the Pathfinder Scout from reference to a
reusable asset pack. Fleet icons can progress alongside that batch.

## What each ship pack contains

- A short role brief and consistent top, side, front and three-quarter design views.
- Editable model source plus a portable runtime export, with documented scale,
  orientation, origin and named docking/drive/effect attachment points.
- Shared hull materials and ship-specific textures, with a complete dependency list.
- A detailed inspection model and reduced-detail versions for crowded views.
- A clean presentation render, transparent thumbnail and readable strategic icon.
- Intact, construction and damage presentation where the current game supports it.
- Reusable effects and audio cues connected through the actual game events.
- An Engine Assets entry containing a stable content name, content hash/version,
  existing ship design ID, preview, source/provenance and stage of completion.

Model size and texture/detail budgets are recorded after the scout is measured in
the intended views. A generated illustration establishes appearance; the model
deliverable still requires actual editable geometry and a checked export.

## Player-fleet identity

Use the established human family: titanium and slate hulls, dark glass, modular
pressure vessels, exposed engineering, radiators, credible docking/service access,
restrained warm identification lights and cool propulsion accents. Keep ships
recognizable in silhouette and give their working equipment visible purpose.

Use the current six portraits and `ShipGeometry` as references. Resolve differences
between those references in the scout's first design sheet, then carry the selected
motifs through the remaining ships. Concept sheets and renders keep labels and
interface elements separate from the artwork. Existing leader/officer identities
and voice assignments carry forward.

The current colony ship reserves 250 million people and the outpost ship eight
million in the game rules. These values require an explicit represented-capacity
and scale treatment before final large-carrier modeling. The art pass does not
silently rebalance population, price, speed, cargo, weapons or research unlocks.

## Engine Assets and project storage

The reusable local library is `Documents/Stellar Engine/Assets`. Import keeps the
source and stores a separate library copy; **Add to project** embeds a copy in the
editor project. Keep editable masters and approved derivatives as separate versions.

The editor currently categorizes files as Images, Audio, Models and Data and uses
content hashes internally. The queue's `designId` and `contentFamilyId` are semantic
production mappings; they are not replacements for those file hashes. Preserve
both identities when the future content registry connects the library to runtime.

Model imports with external textures require all dependencies. The first editor
stores model files but does not yet offer a complete model/scene preview or
dependency-aware import. The current preview limit is 25 MB per asset and 100 MB
of embedded assets per project; larger source masters remain in the source archive
until that pipeline is expanded. These are editor limits, not production art budgets.

For each pack, track: **Brief ready → Concept → Model/materials → Library candidate
→ Integrated candidate → Ready for release**. A completed import or attractive
render alone does not establish in-game readiness.

## Acceptance for the first fleet

1. The six ships remain distinguishable in neutral lighting, thumbnails and small
   map icons, including without color cues.
2. Every pack resolves to its existing ship design ID and all dependencies travel
   with its exported project/package.
3. Models open with correct orientation, scale, materials and attachment points.
4. Shipyard cards, inspection views and system scenes use the same recognizable
   ship, with appropriate detail for each view.
5. Effects and sound respect pause, visibility and actual ship events; captions and
   existing officer identities remain consistent.
6. Crowded-scene cost is measured before setting final model and texture budgets.
7. Changed visuals preserve fleet identities, orders, cargo, population and saves.

Reference briefs: [PLAYER_SHIP_BRIEFS.md](PLAYER_SHIP_BRIEFS.md).
Machine-readable queue: [PLAYER_FLEET_QUEUE.json](PLAYER_FLEET_QUEUE.json).
Existing asset provenance: [ASSET_MANIFEST.md](../ASSET_MANIFEST.md).
