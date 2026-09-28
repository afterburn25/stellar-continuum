# Player ship production briefs

Status: briefs ready, asset production queued. These build on existing designs,
artwork and procedural geometry. The first generation is the Terran player fleet.

## PF-002 — Pathfinder Scout

Design ID: `warp_scout`. Reference: `assets/visual/ships/pathfinder-scout.jpg`.

The player's recognizable exploration workhorse. Retain the compact engineering
body, distinct paired drive units, forward sensor equipment and purposeful crew
spaces. Reconcile the portrait's long pressure hull with the current procedural
tapered scout before choosing the final silhouette. Keep it recognizably lighter
than the escort and less instrument-heavy than the science vessel.

First outputs: silhouette alternatives derived from those references, one coherent
design sheet, the first reusable model/material pack, front/rear orientation
reference, drive/sensor/docking attachment points, portrait and missing scout icon.
This pack establishes the materials and export conventions used by the other five.

## PF-003 — Deep-Space Science Vessel

Design ID: `science_vessel`.
Reference: `assets/visual/ships/deep-space-science-vessel.jpg`.

A clearly scientific working vessel: long laboratory spine, pronounced habitat
structure, communications dishes, separated sensing equipment and radiators.
Retain the open instrument-rich silhouette of its current portrait. Make the
scientific equipment readable at inspection distance and robust at low detail.

First outputs: consistent design views, reusable geometry and materials, sensor and
survey-effect locations, reviewed science icon, and a portrait matching the model.
Rotation or moving instrumentation is a presentation feature to integrate with
the supported animation system, not a newly assumed simulation capability.

## PF-004 — Patrol Corvette

Design ID: `patrol_corvette`.
Reference: `assets/visual/ships/patrol-corvette.jpg`.

A compact armed escort with a protected central hull, restrained armor planes,
clear drive units and readable weapons/point-defense locations. Use the existing
portrait's dense angular family while keeping the ship distinct from a freighter
or scout. Decorative hangars and weapon barrels must not imply unsupported combat
or carrier capabilities.

First outputs: design sheet, model and material pack, named supported weapon/drive
effect points, intact/damaged presentation, reviewed corvette icon and matching
portrait. Existing combat profiles remain the authority for loadout and behavior.

## PF-005 — Interstellar Bulk Freighter

Design ID: `bulk_freighter`.
Reference: `assets/visual/ships/interstellar-bulk-freighter.png`.

A working logistics vessel organized around modular cargo, accessible transfer
points, handling structures and a serviceable propulsion section. Cargo capacity
and unloading are existing gameplay concepts; visible loading should follow the
represented cargo state when that adapter is available.

First outputs: cargo-module design views, loaded/empty presentation variants using
shared geometry, transfer/docking attachment points, model pack, portrait and a
dedicated freighter icon. Avoid presenting it as a colony carrier.

## PF-006 — Sealed Resource Outpost Vessel

Design ID: `resource_outpost_ship`.
Reference: `assets/visual/ships/resource-outpost-ship.png`.

Industrial settlement equipment with a compact sealed-habitat identity: pressure
modules, equipment containers, extraction-support payloads and a clear deployment
section. Its silhouette should communicate establishing a harsh-world operation.

First outputs: design views, deployment-payload breakdown, ship and reusable payload
geometry, model/material pack, portrait and a dedicated outpost icon. Establish
represented payload/population scale before detailed modeling; preserve the current
paid authorization, population reservation and deployment semantics.

## PF-007 — Interstellar Colony Ship

Design ID: `colony_ship`.
Reference: `assets/visual/ships/interstellar-colony-ship.jpg`.

The fleet's settlement carrier: recognizable habitation, life-support, cargo and
industrial-seed sections with a clear arrival/deployment story. Preserve its
distinct civil role and the current art reference's emphasis on sustained settlement.

First outputs: represented-capacity/scale brief, design sheet, modular carrier
geometry, model/material pack, deployment attachment points, reviewed colony icon
and matching portrait. The current 250-million-person reservation is a design
constraint needing explicit treatment, not a dimensional specification for an artist.

## Matching support assets

**Shared parts:** hull finishes, panel/decal set, drive housings, radiators, antennae,
docking collars and service hardware. Ship-specific geometry retains role identity.

**Icons and cards:** complete the six-role set using the existing SVG family. Review
the three existing science/corvette/colony icons and fill scout/freighter/outpost gaps.
Derive portraits and thumbnails from the same selected designs.

**Effects and sound:** propulsion idle/thrust, arrival/departure, survey, docking,
launch and supported damage states. Audit existing launch and other cues first.
Use existing officers for spoken event feedback and the officer tutorial where it
fits; additions require actual event mappings and text equivalents.

**Shipyard:** matching dock/gantry modules, service attachments and staged hull
assembly that corresponds to real production progress. Scale it against the chosen
fleet model plan and keep its reusable components in Engine Assets.
