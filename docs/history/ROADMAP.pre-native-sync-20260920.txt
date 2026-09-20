# Stellar Continuum — Development Roadmap

This is the public roadmap for **Stellar Continuum**, the working title for the real-time space civilization strategy game. Commercial naming clearance remains pending; see `BRANDING.md`. Exact hidden discoveries and secret outcomes are intentionally omitted.

For durable design rules, current baseline status, and engineering constraints, also read the continuity records linked from the repository README.

## Local interplanetary travel — open simulation gap

The expanded orbital map and free zoom in 0.1.2 Alpha use schematic display distances.
An early Earth–Mars journey of roughly six months still requires authoritative body-to-body
travel: orbital distances in AU, departure/arrival phases, local propulsion and transfer windows,
saved progress, visible ETA, and interruption/recovery. Keep this separate from the existing
light-year route/fuel rules. Enlarging the map must not silently change simulation time.
See `handoffs/system-scale-0.1.2.md` for the current boundary and validation receipts.

## Immediate roadmap — Sandbox generation setup

Status: in progress. The ordinary New Game flow now opens a dedicated Sandbox setup page
with random and custom numeric/text seeds, deterministic internal seed resolution, Copy Setup,
Restore Defaults, a spoiler-free summary and a recommended fixed 100-system profile. The entered
seed, resolved seed, generator version and initial option snapshot survive save/load while legacy
numeric seeds and saves retain their established behavior. The recommended profile now uses a
seeded four-arm barred-spiral coordinate field with a dense bar, a star-free central landmark,
and sparse outer edge. The 100 ordinary systems remain the complete strategic quota; the separate
supermassive-black-hole landmark has no current route or destination. The setup
page renders a live vector preview from that profile, and Randomize redraws it from the new seed. Its system
markers fill the fitted overview while the legacy disk remains available to established numeric
callers. A sharp seeded vector layer now adds hundreds of matching arm/bar lights outside the
cinematic base, and 24 non-interactive distant galaxies vary in morphology, size, color, rotation
and parallax outside the playable galaxy. The Balanced profile now separates physical stellar
class from resources/anomalies and enforces the exact 100-star quota. It also enforces 18
planetless, 22 sparse, 42 medium, 14 large and 4 very large planetary systems while retaining
authored Sol. Every non-ancient major civilization now receives two separately reserved natural
expansion candidates within the bounded opening neighborhood, evaluated against its actual species
biology and kept away from all home systems and unstable compact/hot stars. Deeper dust lanes and
optional controls remain next.

Priority: build this after the current graphical New Game selector and before expanding
the 100-system Sandbox with additional content. Keep the ordinary path simple: choose
Sandbox, review the generated setup, then start. Put optional controls behind Advanced
Settings.

Recommended initial configuration:

| Setting | Default | Initial choices |
| --- | --- | --- |
| Galaxy size | 100 systems | 100 only for the current milestone |
| Galaxy shape | Barred spiral | Barred spiral initially; spiral, elliptical, ring and irregular later |
| Seed | Random | Randomize, enter, and copy |
| Stellar variety | Balanced | Realistic, Balanced, Exotic |
| Planet-bearing systems | Common | Sparse, Common, Crowded |
| Habitable worlds | Uncommon | Rare, Uncommon, Common |
| Guaranteed nearby habitable worlds | 2 per major civilization | 0, 1, 2 |
| Other civilizations | 5 | 3, 5, 8 |
| Ancient civilizations | Rare | None, Rare, Standard |
| Space hazards | Standard | Low, Standard, High |
| Starting development | Early Space Age | Fixed initially |
| Difficulty | Standard | Explorer, Standard, Strategist |

Seed and generation rules:

- Treat the seed and selected generation options as separate inputs. Reproducing a
  galaxy requires the same seed, options and generator version.
- Accept both whole numbers and normalized text such as `MY-FIRST-GALAXY` or
  `SOL-ASCENDANT-42`; convert text deterministically to the internal numeric seed.
- Generate a fresh seed by default and provide Randomize, Copy Setup and Restore Defaults.
- Store the entered seed, internal seed, complete option snapshot, generator version,
  game version, creation time, player civilization/species and starting home location in
  campaign metadata.
- Preserve legacy numeric seeds and existing saves.
- Show a spoiler-free summary, for example: `100 systems · balanced distribution ·
  uncommon habitable worlds · 5 civilizations · rare ancient powers`.
- Difficulty must preserve common simulation rules. It may adjust AI planning quality,
  aggression, coordination and tolerance for mistakes, but must not grant hidden free
  resources or exempt a participant from normal costs.

Stellar and planetary composition:

- Keep physical stellar classification separate from gameplay content. Star class, number
  of stars, planetary architecture, resources, ruins, anomalies and hazards are separate
  seeded layers that may overlap. A resource-rich system is not a star type.
- For the 100-system Balanced profile, use an exact quota deck as the initial tuning target:

  | Primary object or life stage | Systems | Percent |
  | --- | ---: | ---: |
  | M-type red dwarf | 48 | 48% |
  | K-type orange dwarf | 20 | 20% |
  | G-type yellow dwarf | 11 | 11% |
  | F-type yellow-white dwarf | 6 | 6% |
  | A-type white star | 3 | 3% |
  | Hot blue B/O star | 1 | 1% |
  | Red/orange giant | 4 | 4% |
  | White dwarf | 3 | 3% |
  | Neutron star or pulsar | 2 | 2% |
  | Black hole | 1 | 1% |
  | Young star or protostar | 1 | 1% |

- This Balanced profile deliberately enriches rare landmarks for a fun 100-system strategy
  map. Realistic shifts more of the quota to long-lived dwarf stars and may generate zero
  black holes or pulsars; Exotic permits up to 2 black holes, 3 pulsars and more young-star,
  giant and nebular systems. The setup page shows resulting counts before play.
- Treat multiple-star structure separately from primary type. The initial Balanced target is
  76 single-star, 20 binary and 4 triple-star systems. Companion stars do not increase the
  selected system count.
- The initial Balanced planetary-architecture target is 18 systems with no major planets,
  22 with 1–2, 42 with 3–6, 14 with 7–10 and 4 with 11–14. A system without major planets
  may still contain asteroid belts, debris, accretion material, stations or discoveries.
- Weight planetary architecture by stellar age and type. Compact objects and very young or
  short-lived stars should usually have no conventional planets; stable dwarf stars should
  supply most ordinary planetary systems. Do not guarantee every star a planet.
- Use seeded weighted variation inside those galaxy-wide targets. The profile controls the
  overall shape of a 100-system campaign, while the seed decides which particular stars are
  planetless and how many bodies each remaining system forms. Avoid repeating a fixed number
  or orbit template from one system to the next.
- Generate major moons according to planet type, mass, formation history, orbital stability
  and distance from the star. Most rocky planets should have no moon, some should have one,
  and a few may have multiple captured or impact-formed moons. Giant planets can have several
  modeled major moons plus an aggregate minor-moon population; do not create hundreds of
  individually simulated rocks merely to inflate a count.
- Give every planet and major moon a deterministic axial tilt and rotation state. Use mostly
  modest tilts with less common severe seasons, sideways rotation and retrograde rotation.
  Allow tidal locking where orbital and stellar conditions support it. Tilt and rotation must
  drive lighting, seasons, climate pressure, surface presentation and habitability where those
  systems are modeled, rather than existing only as decorative numbers.
- Generate orbital eccentricity and inclination within stable bounds. Most mature systems use
  relatively orderly orbits, while a minority contain visibly eccentric, inclined, resonant,
  captured or disturbed bodies. Multi-star systems must use stable circumstellar or
  circumbinary configurations instead of placing planets through companion-star paths.
- Generate visible rings independently from moons. Most planets have no visible ring system;
  gas and ice giants are much more likely to have faint or substantial rings, while rocky
  worlds and moons receive them only rarely after plausible impact or capture histories.
  Ring width, density, color, gaps and tilt vary by seed, with spectacular Saturn-like rings
  remaining uncommon landmarks.
- Model ring and moon consequences where useful: eclipses, tides, seasonal lighting, orbital
  resources, navigation hazards, observation opportunities and potential construction sites.
  These effects must remain bounded and should not turn every body into a separate per-frame
  simulation burden.
- Keep authored systems authoritative. Sol retains its real planets, major moons, axial tilts,
  rotations and Saturn's rings as maintained catalog data rather than being re-randomized.
- Place rare objects with seeded spacing rules and outside every new major civilization's
  protected opening area. A black hole or pulsar should be a strategic landmark rather than
  an accidental immediate-start hazard.

Starting-world fairness and habitability:

- Every major civilization starts on one species-compatible homeworld. Humans always start
  on Earth in the authored Sol catalog; every other playable race receives its own persistent
  named homeworld and starting position.
- Standard setup guarantees 2 additional colonizable worlds compatible with each major
  civilization within practical early exploration range. These are candidates to discover
  and colonize, not free starting colonies. The player may select 0, 1 or 2 in Advanced
  Settings, and the same selected rule applies to player and AI major civilizations.
- Guarantee suitability for the civilization's species rather than using a universal
  `habitable` flag. A world suitable for one biology may be marginal or hostile to another.
  One physical world may satisfy more than one civilization's guarantee when appropriate,
  but generation must prevent overlapping starts or contested guaranteed opening space.
- Apply the galaxy-wide Habitable Worlds setting only after homeworld and nearby guarantees.
  It controls additional naturally compatible discoveries, while terraforming, habitats and
  life support can make otherwise unsuitable worlds useful later.
- Minor pre-space societies and ancient powers use their own scenario rules and do not
  consume a major civilization's nearby-world guarantee unless they are configured as a
  normal competing start.
- Show only aggregate setup counts. Exact locations, planetary environments and which race
  can thrive on each undiscovered world remain hidden behind exploration.

Generation order and safeguards:

1. Reserve Sol and all other home systems with minimum separation and fair access.
2. Allocate physical stellar quotas, then place rare objects using safety and spacing rules.
3. Assign single, binary and triple structures without changing the system total.
4. Generate planetary architecture from star type, age and seeded variation, including
   legitimately planetless systems, varied moon families, axial tilts, rotation states,
   orbital geometry and uncommon ring systems.
5. Satisfy each major civilization's species-relative nearby-world guarantee.
6. Add remaining environments, resources, hazards, ruins and anomalies as independent layers.
7. Validate exact totals, homeworld viability, reachable expansion choices, start separation
   and deterministic reproduction before accepting the generated galaxy.

Advanced controls should use presets and bounded counts rather than a page of independent raw
percentages. Changing one exact star count must rebalance the remaining 100-system allocation,
show a live count preview and prevent impossible totals. The seed, preset, resolved counts and
generator version all belong in the saved/shareable setup data.

Galaxy form, artwork and star placement:

- Replace the single fixed galaxy picture with a shape-driven galaxy presentation. The same
  seeded shape field must control both the luminous galaxy artwork and playable system
  coordinates, so stars appear embedded in the arms, central bar, core, ring or irregular
  concentrations instead of being scattered over an unrelated image.
- Use a barred spiral matching the Milky Way as the default 100-system Sandbox. Its systems
  must cover the full visible galactic form: central bar and bulge, multiple spiral arms and
  a sparse outer edge. Leave only a small presentation margin around the occupied galaxy.
- Treat the 100 systems as the campaign's strategically significant navigable systems across
  the galaxy, rather than implying that they are every physical star in the Milky Way. Dense
  unresolved star fields in the artwork communicate the much larger background population.
- Give each supported shape its own art construction and placement rules:
  barred spiral has a bright elongated core and curved arms; spiral uses a rounder nucleus;
  elliptical uses a smooth concentrated distribution; ring places most systems around a
  luminous annulus; irregular uses asymmetric clouds and knots. Shape changes must alter both
  appearance and travel topology.
- Galaxy size must change composition and density rather than stretching one image. The
  current 100-system map receives purpose-built framing and system-marker scale. Future larger
  maps receive more detailed arms, a larger navigable canvas and additional system density,
  with their own tuned art profile.
- Build the galaxy from high-resolution layered assets and procedural fields: distant stars,
  dust lanes, emission regions, nebula color, central glow and foreground system markers.
  Render those layers at suitable detail levels so zooming inward reveals detail without
  enlarging a low-resolution bitmap or making the image blurry.
- At the full-galaxy zoom level, surround the playable galaxy with a seeded deep-space field
  containing many distant galaxies. Vary their apparent size, distance, brightness, color,
  rotation and morphology, including spirals, barred spirals, ellipticals, lenticular forms,
  irregulars, edge-on discs, small companions and faint galaxy clusters.
- Distribute background galaxies at convincing depths rather than as evenly spaced icons.
  Use scale, haze, red-shifted color, reduced contrast and subtle parallax to communicate
  distance. A few nearby companions may show structure, while the most distant objects appear
  as small diffuse lights and clustered smudges.
- Keep the playable galaxy visually dominant and unmistakable. Background galaxies must not
  resemble selectable star-system markers or imply that they can be entered during the
  initial single-galaxy campaign. Hover and selection behavior applies only when later
  intergalactic gameplay makes a galaxy a real destination.
- Generate the deep-space composition from the campaign seed while preserving important
  authored identity, such as the Milky Way's recognizable companion galaxies. Save its art
  profile version so the same campaign restores the same surrounding universe.
- Render distant galaxies in performance-bounded layers with reusable high-resolution source
  art, seeded variations and aggressive detail scaling. They should remain clean at supported
  resolutions without drawing hundreds of full-detail objects every frame.
- Keep system markers, routes, fleets, selection effects and labels in a separate sharp map
  layer above the galaxy art. Marker brightness and size remain readable at every zoom level
  without changing the underlying coordinates.
- Preserve continuous left-drag panning and wheel zoom. Zooming toward a selected system must
  keep it under the cursor/focus point and transition into its solar-system view without
  requiring a double-click. Zooming back out restores the same galactic position and scale.
- Fog of war may hide system identity, routes, hazards and ownership, but it should not replace
  the galaxy with a blank field. Unsurveyed regions retain atmospheric galaxy art and only the
  information the civilization could legitimately know.
- Derive all shape variation from the campaign seed and store the shape/art profile version in
  campaign metadata so the visual layout reproduces exactly after save/load and setup sharing.

Implementation order:

1. **Implemented:** add the Sandbox setup page with random/custom seed entry and a live seed-driven
   barred-spiral vector preview built from the shared shape profile.
2. **Implemented foundation:** persist the generation configuration, galaxy shape and generator
   version. The dedicated art-profile version follows with shape-driven rendering.
3. **Implemented for current inputs:** deterministic same-seed/same-options tests cover system
   names, star types and coordinates plus metadata save/load. Extend them to art parameters.
4. **Coordinate and scale pass implemented:** replace circular random scatter with a seeded
   barred-spiral field and size its markers for the 100-system overview. Replace the remaining
   fixed backing image with shared procedural shape-driven light and dust rendering. The first
   vector light pass and varied distant-galaxy field are implemented; deeper dust lanes remain.
5. **Balanced physical foundation implemented:** physical star types are separate from content
   tags, exact stellar and planet-count decks are active, and surveyed stars render by physical
   class. Add optional variety/density, species-relative nearby-world, civilization, ancient-power
   and hazard controls after the fixed profile is validated.
6. **Implemented foundation:** add the spoiler-free summary and clipboard-shareable setup. A
   machine-importable advanced setup code follows when optional controls become authoritative.
7. Add additional galaxy shapes and sizes only after the 100-system barred spiral is readable,
   attractive and fully navigable.
8. Add difficulty profiles after AI behavior can express meaningful differences.

Acceptance criteria:

- The default path starts a recommended 100-system Sandbox without requiring Advanced
  Settings.
- Randomize visibly changes the seed; copying and re-entering a setup reproduces the same
  starting galaxy on the same generator version.
- Every selected option is visible before confirmation and survives save/load.
- Invalid seeds fail cleanly in the setup page with a useful message.
- Generation options never reveal undiscovered systems, species, hazards or outcomes.
- The selected stellar and planetary quotas total exactly 100 systems, including legitimately
  planetless systems, and every major civilization receives the selected number of viable
  nearby expansion candidates without overlapping protected starts.
- Repeated generation produces planetless stars, moonless planets, single- and multi-moon
  worlds, varied axial tilts and a minority of visibly ringed bodies. The same seed reproduces
  every result, and no generated orbit is physically invalid for its stellar configuration.
- The default map clearly reads as a complete barred-spiral Milky Way; playable systems occupy
  its bar, arms and edge around the separate star-free galactic core landmark.
- Galaxy art remains crisp through its supported zoom range, and continuous zoom can enter a
  focused solar system and return to the same galaxy position without a double-click.
- The whole-galaxy view includes a varied, convincing deep-space population of distant
  galaxies at multiple apparent depths; none can be mistaken for a selectable system or an
  immediately playable destination.
- Player and AI remain subject to the same authoritative economy, research, construction,
  movement and combat rules.

## Immediate roadmap — Interstellar travel lanes, distance and range

Status: in progress. The first topology layer builds a deterministic connected minimum-distance
backbone plus three-nearest local alternatives, exposes shortest-route queries, and draws only
lanes whose endpoints the player knows. System intelligence now displays coordinate separation
from home in light years and parsecs using the maintained conversion constants; AU-to-kilometre
conversion is also tested. Fleet orders still use the provisional reach adapter, so lane-by-lane
movement, drive range, route knowledge state, fuel and persistence remain required before lanes
become authoritative movement constraints.

Distance and unit rules:

- Store galactic system coordinates and interstellar distances in light years at double precision.
  Derive every displayed separation from those coordinates so the artwork, route graph, movement,
  sensors and arrival times do not use contradictory distances.
- Display **light years (ly)** as the default interstellar unit and allow **parsecs (pc)** in the
  measurement settings. Use the exact conversion `1 pc = 3.26156 ly`; detailed tooltips may show
  both values together without crowding every map label.
- Display **astronomical units (AU)** for travel and orbit scales inside a star system. Very close
  orbital and surface distances may use kilometres, but their detail view also retains the AU value
  where it helps comparison. Use `1 AU = 149,597,870.7 km` as the maintained conversion.
- Treat the galaxy rendering as a readable projection of the physical coordinates. Visual zoom and
  artistic compression cannot change the authoritative distance between two systems.
- Show unit preferences consistently in the map ruler, selected-system panel, route preview, fleet
  status, sensor range and relevant setup summaries.

Lane generation and discovery:

- Generate a deterministic connected lane graph from the same seeded coordinates and galaxy shape.
  Build a sparse connectivity backbone first, then add bounded local alternate links so the map has
  meaningful routes and chokepoints without becoming a web between every nearby star.
- Constrain candidate lanes by real separation and configurable lane-density rules. Long links are
  rare and visually distinct; crossing two drawn lines does not create an intersection in empty
  space.
- Make lanes follow the barred spiral's system distribution, linking nearby arm regions and using a
  limited number of bridges across arm gaps and the galactic core. Do not create links merely to fill
  decorative background space.
- Validate that every ordinary starting region has multiple early expansion choices, access to the
  wider graph and at least one alternate route before a single chokepoint can isolate it. Preserve
  strategic geography without allowing an unwinnable start caused by generation.
- Separate existence from knowledge. A civilization sees only lanes it has detected or inferred
  legitimately through astronomy, probes, surveys, foreign charts or transit. Unconfirmed candidate
  routes show uncertainty rather than an exact hidden connection.
- Let advanced sensors, surveys and navigation research reveal difficult, unstable or previously
  unusable connections. Permanent seeded lanes remain reproducible through save/load.

Ship range and route feasibility:

- Give every ship design a base maximum direct interstellar leg range. A fleet can use a lane only
  when the next connected system is within every participating ship's safe leg range.
- Derive effective range from installed drive and engine capability, available fuel or stored energy,
  ship mass, payload, drive condition, navigation quality, crew endurance and required safety reserve.
  The fleet uses its most restrictive participating vessel unless ships are detached.
- Improve range through researched drives, engines, energy storage, fuel, lower-mass construction,
  navigation, life support, maintenance reliability and purpose-built support vessels. Each upgrade
  changes an understandable physical limit rather than adding an unexplained empire percentage.
- Keep structural drive range distinct from current operational range. A damaged or poorly fueled
  ship may be unable to make a leg its design normally supports; adding fuel cannot exceed the drive's
  structural maximum.
- Permit refueling, charging, maintenance and crew support at compatible colonies, stations, depots,
  tenders or other developed nodes. These extend a route through multiple valid legs but do not
  increase the maximum unsupported jump.
- Require the player to carry a configurable arrival reserve. Emergency or high-risk routing may use
  part of that reserve only through an explicit order showing failure, delay and rescue consequences.
- Fleets cannot silently cross an unavailable leg. If technology, fuel, endurance, access or route
  knowledge changes, pause or recalculate the order at the last safe location and notify the player.

Speed, time and in-system movement:

- Calculate each interstellar leg's travel time from its authoritative length, drive performance,
  preparation and arrival requirements. Show departure time, estimated arrival, uncertainty and
  total multi-leg duration before confirmation.
- Keep range and speed independent: a drive can be long-range but slow, or fast with demanding fuel,
  heat, maintenance or support requirements.
- After arrival, use AU-scale travel between the system boundary, stars, planets, moons, stations and
  fleets. Entering a system does not place a ship instantly beside every body in it.
- Connect detection, interception, pursuit, blockade and rescue opportunities to actual position and
  time in transit without requiring detailed orbital-mechanics piloting from the player.

Map interaction and route planning:

- Draw known lanes sharply above the galaxy art with separate states for usable, out of range,
  unconfirmed, hostile or access-restricted, congested and currently disrupted connections.
- Selecting a fleet displays its safe direct-range envelope and highlights reachable adjacent
  systems. Hovering a lane shows length in the selected unit, required range, estimated leg time,
  fuel or energy, reserve on arrival, access and known hazards.
- Clicking a destination previews the recommended multi-leg route plus alternatives optimized for
  fastest arrival, lowest fuel, safest travel, friendly support or avoidance of restricted space.
- Make invalid routes explain the first blocking leg and offer direct remedies such as refuel,
  repair, detach the limiting ship, add a tender, survey a route or choose a nearer destination.
- Allow waypoints and route policies through mouse-driven controls. Preserve the camera position and
  selection while zooming continuously between the galaxy, route, system and destination planet.

AI, persistence and generation validation:

- Player and AI fleets use the same graph, known routes, access rights, range calculations, fuel,
  reserves, travel time and interdiction rules. AI planning cannot route through undiscovered or
  unreachable lanes.
- Persist physical coordinates, lane identifiers and state, route knowledge, fleet path, current
  leg, progress, fuel, reserve policy and generator version. Loading cannot duplicate travel or move
  a fleet to a route endpoint prematurely.
- Validate graph connectivity, start fairness, lane-length distribution, alternate-path availability,
  unit conversion and route determinism across representative seeds before accepting a generator
  version.
- Bound route searches and map drawing for 100-system play, while keeping the design scalable to
  later galaxy sizes without changing saved distances.

Implementation order:

1. Establish authoritative light-year coordinates, conversion utilities and consistent ly/pc/AU
   presentation throughout galaxy, system and fleet views.
2. Generate and persist the deterministic connected lane graph with fair-start and connectivity
   validation.
3. Add ship design range, operational fuel or energy, endurance, condition and safe-reserve rules.
4. Replace unrestricted destination movement with adjacent-leg validation and multi-leg routes.
5. Add route previews, range overlays, lane states, waypoints, refueling nodes and direct remedies.
6. Connect in-system AU travel, detection, access, hazards, interception and AI route planning.
7. Retune the starting drive and nearby lane lengths so early exploration offers several choices
   while later technologies open genuinely new regions.

Acceptance criteria:

- Every system pair, lane and route displays a distance derived from the same coordinates with exact
  ly/pc/AU conversion and the selected unit preference survives restart.
- The default 100-system galaxy is connected but strategically sparse, has bounded alternate routes
  around starts and reproduces the identical lane graph from the same seed and generator version.
- A fleet cannot begin a leg beyond the least-capable participating ship's current operational range,
  and the interface identifies the limiting ship and requirement before confirmation.
- Fuel, damage, payload, drive technology, support vessels and refueling nodes change reachable
  routes consistently; no upgrade or depot grants unexplained unlimited range.
- Multi-leg travel consumes time and support per leg, survives save/load at intermediate progress and
  never teleports a fleet between the galaxy and a planet.
- Unknown routes and foreign access remain observer-safe, and player and AI pathfinding produce only
  routes they can legitimately know and traverse.

## Immediate roadmap — Planetary-orbit shipyards

Status: planned replacement for civilization-wide shipyard projects. Every ordinary shipyard is a
physical station constructed in the selected planet's orbital space. It belongs to that location,
draws from that world's economy and logistics, and launches completed ships from that orbit.

Location and construction rules:

- Start shipyard construction by selecting an owned or legally accessible planet and entering its
  orbital layer. The order must identify the host planet and orbital region before authorization.
- Do not permit ordinary shipyards on a planetary surface, in an abstract empire inventory or at an
  unspecified system location. Specialized deep-space yards may become a later researched class
  with their own support requirements; they cannot silently use planetary-shipyard rules.
- Require sufficient survey knowledge, orbital access, launch or freight infrastructure,
  communications, construction vessels or platforms, power, materials, Industry, funding and
  qualified orbital workers.
- Build the yard through orbital works, structural frame, power and thermal systems, fabrication
  equipment, docks, habitation and commissioning. Inputs travel to the construction site and are
  consumed through milestones.
- Validate legal orbital altitude, congestion, debris and known environmental hazards. Orbital
  placement should remain strategic and readable without requiring manual orbital-mechanics
  piloting.

Local support and capacity:

- Connect each yard to its host planet's surface and orbital logistics. Materials, components,
  workers, crews, supplies and fuel must reach the yard through launch complexes, orbital transports,
  mass drivers, elevators or later equivalent infrastructure.
- Charge the actual energy and transport burden of lifting goods and people from the surface. Local
  moons, asteroids and orbital industry can provide lower-cost inputs when connected by freight.
- Give each yard local construction bays, maximum supported hull size, fabrication throughput,
  storage, workforce, power, maintenance and docking limits. Multiple yards do not merge into one
  invisible civilization-wide queue.
- Let technology and yard upgrades improve bay count, hull scale, automation, fabrication accuracy,
  construction rate, repair capability and logistics efficiency. Upgrades require downtime and
  physical work where appropriate.
- A yard without sufficient workers, power, components, freight or maintenance slows or stops and
  displays the exact constraint. Its host planet's population cannot operate it without available
  qualified orbital personnel and transport.

Shipbuilding, launch and servicing:

- Assign every ship order to one specific yard and bay. Reserve its design, funding and initial
  inputs there, then consume materials and labor through visible keel, structure, systems,
  integration, testing and commissioning stages.
- Spawn a completed ship at the constructing yard's planetary orbit with its real fuel, stores,
  crew and commissioning state. It must travel in AU from that orbit to another body or the system's
  interstellar departure route.
- Require delivery and training of the intended crew before full readiness. An empty completed hull
  may remain docked rather than receiving personnel from elsewhere instantly.
- Use compatible yards for repair, overhaul, refit, refueling, resupply, mothballing and salvage.
  Damage, design size and installed technology determine which facilities can perform the work.
- Allow construction queues and priorities per yard, with clear estimates based on current inputs
  and capacity. Reassigning an incomplete hull to another yard requires a plausible tow or transport
  operation and compatible destination capacity.

Strategic consequences and presentation:

- Render shipyards as selectable structures around their actual planet at appropriate system and
  planet zoom levels. Selection opens construction, docking, storage, workforce, power, maintenance,
  defense and logistics information.
- Show the host planet and launch orbit on every ship order card. From the Ships page, `View Yard`
  moves directly to that planet's orbital view without losing the selected order.
- Make the yard's position matter during blockade, interception, bombardment, sabotage, debris events
  and evacuation. Local defenses and patrols protect the installation; damage affects real bays and
  projects.
- Prevent a blockaded or disconnected yard from drawing materials from a global stockpile. Existing
  on-site inventory may sustain work until depleted.
- Support orbital traffic whose destinations come from actual deliveries, crew transfers, docked
  ships, launches and repair work rather than decorative circling.

Migration and implementation order:

1. Add a stable host-body identifier and orbital-region state to shipyards, queues and saves.
2. Migrate each legacy civilization-wide yard to a suitable owned homeworld orbit exactly once,
   preserving completed status, current queues, progress and paid costs.
3. Move shipyard authorization from the global construction list to the selected planet's orbital
   construction interface and require local prerequisites.
4. Connect local inventory, freight, power, workforce, maintenance and construction bays to ship
   production; remove use of unrestricted empire-wide Industry at the yard.
5. Spawn completed ships at the yard's orbit and connect AU-scale departure, docking, repair, refit,
   resupply, blockade and combat consequences.
6. Add graphical yard tiers, construction activity, docking and authoritative support traffic.

Acceptance criteria:

- Every shipyard in a new or migrated campaign has one valid host planet and visible orbital
  position; no functional yard exists only as a civilization-wide completion flag.
- A ship can be built only in a compatible local bay supplied with the required funding, materials,
  power, workforce, logistics and technology, and shortages produce a useful explanation.
- Completed ships appear at the correct planetary orbit with no teleportation of hull, crew, fuel or
  cargo and retain that state exactly through save/load.
- Disconnecting, blockading, damaging or under-staffing a yard affects its actual queues and service
  capacity without changing unrelated yards elsewhere.
- Player and AI civilizations select, supply, protect and use planetary-orbit yards through the same
  rules, and a representative 100-system campaign keeps yard routing and presentation responsive.

## Immediate roadmap — Fully surveyed world inspection

Status: partially implemented. Clicking a world currently shows a compact portrait plus radius,
mass, gravity, temperature, pressure, atmosphere and natural-satellite count when known. Replace
that caption-sized presentation with a complete graphical world profile driven only by the
observing civilization's legitimate survey knowledge.

Interaction and information levels:

- Clicking any visible planet or moon selects it and opens a persistent world-information panel
  beside the solar-system view. Selection must work at every supported system zoom level and
  remain focused while the player pans or zooms toward the body.
- A detection reveals only the star system. Reconnaissance may reveal the orbital catalog,
  approximate size, body category and positive signatures with clear uncertainty. A completed
  science survey unlocks the full currently knowable profile. Owned colonies may add live local
  information beyond remote survey data.
- Never fill an unknown field with the authoritative hidden value, zero or `false`. Display
  `Unknown`, an uncertainty range or omit the row, according to the observation. Confirmed
  absence is distinct from no evidence.

The fully surveyed profile should contain:

- **Identity:** world name, planet/moon classification, parent body, orbital position, natural
  moons and known artificial satellites, stations, fleets and settlements.
- **Physical properties:** radius in Earth radii and kilometres, mass in Earth masses, surface
  gravity, density when derivable, solid/gaseous classification and immersed/oceanic state.
- **Environment:** average temperature in kelvin and the player's selected everyday unit,
  surface pressure in kPa and atmospheres, atmosphere regime, available natural solvent,
  radiation hazard and other environmental facts that the generator authoritatively models.
- **Survey findings:** confirmed resources, confirmed absence of resources, anomalies, native
  activity and pre-space civilization evidence. An unresolved anomaly remains described as an
  unresolved finding and does not reveal its hidden outcome.
- **Species suitability:** natural habitability and unprotected operating capacity for every
  player-owned population that can legitimately be evaluated, plus the limiting factor and
  required gravity, thermal, pressure, atmospheric, biosphere, immersion and radiation support.
- **Settlement outlook:** whether colonization is presently possible, which population or
  lineage would settle, expected support burden, known hazards, required technology and
  infrastructure, estimated authorization cost and the reason an unavailable action is locked.
- **Existing use:** owner and population when legitimately known, current construction,
  districts, power balance, support condition and direct View Surface/Land/Manage actions for
  an owned colony.

Presentation requirements:

- Use a large planet portrait or live globe, graphical environment icons, readable stat rows,
  suitability bars and colored warning badges. Keep explanatory tooltips available without
  turning the main view into a wall of text.
- Show the active species or lineage beside its suitability result and allow switching among
  player-owned populations directly in the panel. Never present one universal habitability
  score as a property of the planet.
- Clearly label measured, estimated and unknown information. Include survey level, progress,
  observing civilization and last-confirmed date where the knowledge model supports them.
- Use consistent units and conversions throughout the game. Player mode shows readable names;
  Developer mode may additionally expose stable body/system IDs and raw normalized values.
- World actions must operate on the selected body rather than silently choosing another planet
  in the system. Colonization, fleet destination, orbital construction and surface entry must
  preserve the exact body ID through execution and save/load.

Acceptance criteria:

- Clicking every fully surveyed planet and moon opens its complete known profile without
  leaving the solar-system map.
- All authoritative physical fields currently generated for the body are represented, including
  solvent, radiation, solid surface and immersion facts that the existing compact caption omits.
- Species suitability and settlement costs update when the selected population or adapted
  lineage changes.
- Reconnaissance and detection views cannot reveal full-survey values through text, artwork,
  icons, sorting, disabled-action reasons or species-habitability calculations.
- Selection, panel state and exact-body actions survive normal zoom transitions and save/load.

## Immediate roadmap — Procedural star-system and celestial naming

Status: in progress. New balanced Sandbox campaigns now generate 100 unique proper system/star
names from a dedicated seed stream, retain authored Sol, and remove the `SYS-060` pattern from
Player map labels. Generated planets receive unique proper names within their system and moons
receive a proper epithet tied to their named parent. Legacy numeric campaigns keep their existing
reconstructible names. Scientific designations, cultural naming profiles, aliases, search and
player renaming remain planned.

Naming model:

- Generate a unique primary-star name for every system before planets and civilizations are
  placed. The system takes the primary star's name, so map labels, search, breadcrumbs, fleet
  destinations and notifications all refer to the same recognizable location.
- Preserve authored real-world catalogs where applicable. Sol, the Sun, Mercury, Venus, Earth,
  Luna/the Moon, Mars, Jupiter, Saturn, Uranus and Neptune retain their established names, and
  future real systems use maintained astronomical naming data rather than procedural aliases.
- Give every generated major planet and moon its own deterministic proper name. Also retain a
  secondary scientific designation based on its star and orbit for sorting and technical
  display, but do not use a bare letter or number as the ordinary player-facing name.
- Name companion stars consistently in binary and triple systems, retaining the shared system
  name plus component designation where useful. Planets orbiting a component or common
  barycentre must identify that relationship without ambiguous duplicated names.
- Expand the physical catalog beyond only planets and moons. Major dwarf planets, asteroid
  belts, notable asteroids, comet reservoirs, named comets and other strategically relevant
  natural bodies receive stable IDs, generated names and appropriate body classifications.
  Represent an asteroid belt as one aggregate celestial region plus individually modeled
  notable objects when gameplay requires them; never create one simulation object per rock.
- Artificial satellites, stations, habitats, shipyards and megastructures use their owner's
  naming policy and remain visibly distinct from natural bodies. Ownership changes may add an
  alias without destroying historical identity.

Name sources and identity:

- Use seeded, curated phoneme and word-part libraries with multiple naming styles instead of
  unrestricted random characters. Home systems and inhabited bodies draw from their founding
  civilization's language/culture profile; uninhabited systems draw from a neutral astronomical
  catalog style until a discoverer or owner assigns another name.
- Maintain canonical name, discoverer/observer catalog designation, native name and player
  rename as separate aliases when relevant. A civilization cannot know a native name before
  legitimate contact or translation, and another empire's private rename does not silently
  rewrite every observer's records.
- Allow the player to rename owned systems, stars, planets, moons, settlements and installations.
  Renaming changes presentation and recorded history, never stable identity, coordinates,
  orders, save references or another civilization's knowledge.
- Prevent duplicates within a campaign, confusing near-duplicates in neighboring systems,
  reserved real-world-name collisions, control characters and unsuitable generated words.
  Names must fit map labels and remain searchable with case- and punctuation-tolerant matching.
- Derive procedural names from dedicated seed streams so adding a moon, changing a resource or
  rebalancing habitability does not rename unrelated systems. Persist accepted names and alias
  history in the save rather than regenerating presentation names every load.
- Support localization by keeping grammatical templates and translated classifications outside
  the stable proper name. Do not translate a proper name differently between screens.

Interface requirements:

- Galaxy markers show the star/system name. Solar-system labels show each star, planet, moon,
  belt and other known body by name; selecting one opens its exact profile.
- Search accepts proper names, known aliases and scientific designations, then centers the map
  and zooms smoothly to the selected system or body.
- Survey notifications use names as they become legitimately known. Before sufficient survey,
  a temporary observer-local designation may be used and should update cleanly when the final
  name is learned or assigned.
- Developer mode may show internal system/body IDs beside names. Player mode never substitutes
  `SYS-060` or another implementation identifier for a missing display name.

Acceptance criteria:

- A generated 100-system galaxy contains 100 unique readable star/system names and no visible
  `SYS-###` fallback labels in Player mode.
- Every generated planet, moon, belt and other modeled celestial body has a stable individual
  name, classification and exact parent/orbital relationship.
- The same seed, generation options and naming-profile version reproduce the same initial names;
  saves preserve later aliases and player renames exactly.
- Adding or removing an unrelated generated body does not rename previously stable systems or
  bodies elsewhere in the galaxy.
- Names remain consistent across galaxy, system, planet, colony, fleet, exploration, diplomacy,
  notification, search and save/load interfaces without leaking undiscovered native identity.

## Immediate roadmap — Mouse-first interface and Escape menu

Status: planned consolidation of the current prototype controls. Normal Player mode must be
fully playable with the mouse; keyboard and controller inputs may provide equivalent shortcuts
but cannot be the only way to reach an action or understand the current state.

Mouse-first interaction:

- Use the persistent graphical navigation bar to open the main Economy, Research, Industry,
  Ships, Exploration, Colonies, Logistics and Relations pages. Remove redundant prototype menus,
  cycling controls and instruction boxes once their actions have direct buttons and visual pages.
- Make map interaction consistent at every scale: left-click selects, left-click-and-drag pans,
  wheel zooms toward the cursor, clicking a selected object opens its contextual profile and
  visible graphical buttons issue available actions.
- Planets, moons, systems, fleets, colonies, buildings, projects and contacts must expose their
  normal commands from their visual object, profile or owning page. The player should never need
  to remember an undocumented key to survey, move, build, research, colonize, inspect or manage.
- Use right-click only for an optional concise contextual command surface after the same actions
  are available through visible controls. Do not hide essential actions exclusively in a context
  menu or hover state.
- Every icon needs a readable label or tooltip, clear hover/pressed/disabled states, a useful
  locked reason and a sufficiently large click target. Selected objects remain visibly selected
  after the pointer moves away.
- Preserve optional keyboard shortcuts for experienced players and accessibility, but list them
  in Settings/Controls rather than filling ordinary game pages with keyboard instructions.

Escape pause/save menu:

- Pressing Escape during a campaign pauses simulation time and opens one centered game menu over
  the current view. Pressing Escape again or selecting Continue closes it and returns to the same
  page, map position, selection and previous simulation speed.
- The initial menu contains four large mouse-operated choices in this order:
  **Continue**, **Save Game**, **Load Game** and **Main Menu**.
- Save Game opens named manual save slots with timestamp, campaign date, civilization, galaxy
  seed/setup summary, version and optional overwrite. Saving returns a clear success or failure
  result without silently resuming simulation.
- Load Game opens compatible saves with the same summary, identifies incompatible or damaged
  files before selection and uses the existing recovery/backup path when appropriate.
- Main Menu protects progress. If the current state differs from the latest successful save,
  present Save and Return, Return Without Saving and Cancel. A completed save returns to the
  main menu; Cancel returns to the paused game.
- No campaign simulation advances while the Escape menu, save browser, load browser or unsaved
  progress decision is open. Developer mode uses the same pause/save behavior and keeps its save
  namespace visibly separate from Player mode.
- Avoid a chain of overlapping pause, options and confirmation windows. Each choice replaces the
  menu body or opens one clear modal state with a visible Back button and mouse focus retained.

Acceptance criteria:

- A player can complete the opening campaign loop using only visible mouse controls from New
  Game through research, construction, shipbuilding, exploration, world inspection, settlement,
  diplomacy, combat and saving/loading.
- Removing the prototype keyboard instruction menus does not remove any player action.
- Escape always opens the same four-choice paused game menu during ordinary campaign play and
  restores the exact prior view when continued.
- Save, Load and Main Menu paths handle success, cancellation, invalid saves and unsaved progress
  without losing a campaign or mixing Player and Developer saves.
- Automated navigation coverage and real Godot input checks exercise the primary mouse path,
  Escape pause/resume, manual save, reload and main-menu return.

## Immediate roadmap — Unified graphical polish and production artwork

Status: planned full-game presentation pass. Continue the selected Cinematic Strategy direction:
deep black space, rich blue/violet nebula color, warm stellar light, sharp silhouettes and
readable graphical controls. Existing visual assets are production candidates until reviewed
inside the final integrated screens at their actual display sizes.

Visual-system requirements:

- Establish one maintained art bible covering palette, typography, spacing, panel depth,
  borders, icon weight, button states, lighting, animation, effects, species identity, ship
  language and map rendering. Every screen should look like part of one game rather than a
  collection of prototype panels.
- Replace plain rectangular controls with a consistent family of polished graphical buttons:
  sculpted frames, restrained gradients and highlights, clear icon-plus-label hierarchy,
  responsive hover/press/focus states, disabled-state reasons and subtle motion. Decoration
  must preserve legibility and large click targets instead of covering information.
- Give major actions stronger visual weight than navigation and secondary actions. Research,
  construction, colonization, fleet orders and dangerous confirmations need distinct, reusable
  visual treatments without assigning a different style to every page.
- Build panels from scalable theme elements, vectors, nine-patch frames and rendered effects so
  they remain sharp at different resolutions and interface scales. Do not bake labels, numbers
  or controls into raster artwork.
- Add restrained transition motion, selection pulses, progress animation, engine trails,
  construction activity, survey sweeps and event effects so accepted actions visibly affect the
  world. Motion should be interruptible, performance bounded and reducible in accessibility
  settings.

Map and celestial polish:

- Replace the flat-picture feeling with layered depth: high-resolution/procedural galaxy light,
  dust lanes, nebulae, dense unresolved stars, distant galaxies, subtle parallax and a sharp
  interactive map layer tied to the generated galaxy shape.
- Render each physical star class distinctly through color, size, corona, flares, illumination
  and special effects. Pulsars need focused beams and rotation; black holes need an accretion
  treatment and lensing; giants, white dwarfs, young stars and binaries need immediately
  recognizable silhouettes without relying only on text labels.
- Improve generated planets and moons with high-detail materials, clouds, atmosphere rims,
  night lights where civilization is known, oceans, ice, storms, terrain, rings, shadows and
  eclipses appropriate to their authoritative properties. Visuals must not imply water, life,
  resources, structures or ownership that the observer has not discovered.
- Maintain detail through continuous galaxy-to-system-to-planet zoom using layered detail levels,
  procedural materials and sufficiently large source assets. Never enlarge a small bitmap until
  it becomes visibly soft or pixelated.
- Keep routes, markers, labels, selections, fleets and warnings crisp above cinematic artwork.
  Test readability in dense cores, bright nebulae, dark outskirts and color-vision accessibility
  modes.

Production-art requirements:

- Create original photorealistic artwork where authored imagery provides more value than live
  rendering: splash/loading scenes, campaign and event illustrations, species and leader
  portraits, ship presentation art, major discoveries and selected environmental backdrops.
- Generate or commission each image from an exact gameplay brief. Species portraits must match
  canonical morphology and habitat; ships must match their role and civilization; planet art
  must match known physical data. Attractive but contradictory art is rejected.
- Keep generated artwork free of embedded text, logos, interface frames and watermarks. Compose
  all labels and controls in the live interface so they remain editable, localizable and sharp.
- Retain high-resolution source masters outside the runtime package, create optimized runtime
  derivatives and record prompt/creator, date, dimensions, crop, compression, intended screen,
  license/provenance and approval state in `ASSET_MANIFEST.md`.
- Use shared visual families rather than one unrelated image per item. Each civilization needs
  recognizable materials, shapes, lighting and motifs across portraits, ships, stations,
  buildings and interface accents while preserving common control readability.

Implementation order:

1. Produce representative final-quality mockups for the main menu, galaxy, solar-system,
   surveyed-world, planet-surface and one management page; approve the common visual language.
2. Build the scalable theme, typography, panel and button kit, then apply it to the persistent
   HUD, navigation and Escape menu.
3. Implement the shape-driven galaxy, star-class visuals, celestial materials and zoom detail
   levels before producing large quantities of supporting art.
4. Recompose each management page around graphics, cards, diagrams and direct actions, removing
   leftover prototype text boxes and redundant controls.
5. Create the remaining photorealistic portraits, ships, loading art, events and backdrops from
   canonical briefs, registering every accepted asset in the manifest.
6. Perform a complete interaction and screenshot review at supported resolutions and interface
   scales, then fix visual inconsistency, clipping, blur, weak hierarchy and unreadable contrast.

Acceptance criteria:

- The main menu, galaxy, system, planet, surface, management pages and Escape menu share one
  recognizable polished visual language and no longer resemble debug or form-based interfaces.
- Buttons are visually attractive, responsive and understandable while remaining readable and
  fully mouse operable at supported interface scales.
- Stars, planets, moons, rings, ships and active effects visually reflect authoritative game
  state and remain sharp through their supported zoom ranges.
- All production artwork has recorded provenance, loads in the packaged Windows build and has
  been reviewed in a real Godot capture rather than only as a source image.
- A full screenshot journey contains no placeholder boxes, temporary names, missing textures,
  stretched artwork, raw implementation identifiers or hidden-information leaks.

## Immediate roadmap — Planetary settlements, modules and organized growth

Status: partially implemented redesign. The current free-placement surface supports power,
research, industry, trade and habitat buildings with upgrades, local power and a fixed
64-building prototype ceiling. Replace the generic ceiling and identical hub with planet-scale
development potential, research-gated modules and an upgradable administrative core.

Capacity model:

- Keep free placement, but limit development through understandable physical systems rather
  than one universal building count. A colony's usable module capacity is the lowest relevant
  constraint among planetary development potential, administrative-core capacity, serviced
  area, available workforce, power, logistics and environmental support.
- Derive the planet's long-term development potential primarily from usable solid surface area,
  using radius squared, then adjust it for oceans/immersion, terrain, climate, radiation and
  other genuinely unusable regions. Planet size sets the eventual ceiling; a larger planet does
  not make an undeveloped outpost immediately capable of supporting a metropolis.
- Divide very large worlds into a bounded number of regional settlement areas as development
  expands. The existing 1,024-metre scene becomes one local region rather than pretending to be
  the entire planet. Only active/visible regions render detailed buildings and traffic, keeping
  simulation and save size bounded.
- Replace `Buildings 12/64` with a clear capacity breakdown such as `Modules 18/24`, `Serviced
  area 72%`, `Power +6`, `Housing +1.2M` and the current limiting factor. Different modules
  consume different capacity according to footprint, staffing, utilities and administrative
  complexity; one power station is not equivalent to one planetary spaceport.
- Research unlocks new module families, higher-efficiency variants, core upgrades, broader
  service networks, difficult-environment construction and additional regions. Research never
  makes planet size irrelevant and does not directly place free buildings.

Administrative cores:

- A civilization's starting homeworld has one unique **Capital Hub** representing its existing
  government, infrastructure, archives and metropolitan service core. It begins visibly built
  up and can be upgraded in place; the homeworld must not look like an empty colony around one
  isolated structure.
- A newly founded world begins with a **Landing Command Center** created by the settlement
  mission. It supplies minimal power, communications, storage, construction coordination and a
  small serviced radius. Its initial capacity supports survival and a few modules, not immediate
  mass industry.
- Upgrade the colonial core through clear stages such as Landing Command Center, Colony Command,
  Planetary Administration and World Coordination. Upgrade the capital through Capital Hub,
  Metropolitan Capital, Planetary Capital and System Capital. Names may vary by civilization,
  while capabilities use shared simulation contracts.
- Each core tier increases administrative module capacity, service radius, construction
  coordination, logistics throughput and the number of regions it can manage. Upgrades require
  population, research, power, industry, time and appropriate infrastructure; a larger planet
  provides room but cannot skip these requirements.
- The Capital Hub remains unique to the current capital. Relocating a capital is a major project
  that promotes a suitable command center and changes the old hub's role without deleting its
  buildings or history.
- Damage or power loss at the core reduces coordination, construction and service coverage but
  does not instantly erase a colony. Recovery, emergency power and redundant later-game
  administration create meaningful resilience.

Initial module families:

- **Power:** solar, fission/fusion, geothermal, wind/tidal where suitable, storage and grid
  control. Output depends on environment and research.
- **Housing and life support:** residences, sealed habitats, arcologies, food/water processing,
  medical care and species-specific environment systems. Housing determines supported
  population rather than acting only as a percentage discount.
- **Research:** field laboratories, specialized institutes, observatories and campuses that
  create real Effective Research Lab capacity and require suitable power, staff and facilities.
- **Industry:** fabrication, refining, extraction processing, heavy manufacturing, automation
  and storage. Inputs, outputs, pollution/heat and logistics determine useful operation.
- **Logistics and commerce:** depots, transit hubs, ports, markets and communications that move
  resources and connect regions rather than creating money in isolation.
- **Administration and services:** security, emergency response, education, governance and
  maintenance capacity required by larger populations and more complex settlements.
- **Defense and orbital support:** sensors, shelters, surface defenses, launch facilities and
  orbital interfaces after the appropriate military and aerospace capabilities exist.

Keeping free-placement cities organized and attractive:

- Begin every core with a small generated road and utility spine. New buildings remain freely
  positioned but use optional magnetic alignment to nearby roads, plazas, building edges and
  compatible district clusters. The preview shows the final entrance, foundation and utility
  connection before placement.
- Require a believable service connection for operation. Players may place beyond the current
  road/utility network as a planned site, but it remains unpowered or under construction until
  connected. This naturally produces coherent settlements without rigid square tiles.
- Generate roads, footpaths, pipes, power links and landscaping between connected structures,
  following terrain with smooth curves. Buildings automatically face their access route and use
  foundations, retaining walls or limited grading instead of floating or cutting randomly into
  slopes.
- Use soft districts rather than fixed slots. Nearby related modules share service buildings,
  visual language and bounded adjacency benefits; incompatible heavy industry, housing or
  hazardous facilities create visible reasons for separation. Existing three-building district
  bonuses become consequences of a connected district rather than simple global type counts.
- Offer optional road-first drawing, rotation snapping, alignment guides and reusable district
  blueprints. Every aid can be overridden within valid terrain and safety rules, preserving the
  requested ability to place buildings anywhere practical.
- Fill connected districts with bounded decorative detail derived from real population and
  modules: high rises, smaller residences, parks or sealed commons, freight yards, service
  vehicles, pedestrians where appropriate, transit, utility equipment and occasional shuttles.
  Decorative objects never block valid placement or claim production they do not provide.
- Give each civilization a coherent architectural kit across its core, modules, roads, vehicles
  and lighting. Adapt structures to pressure, gravity, atmosphere, temperature and species body
  plan so an alien colony is more than a recolored Human city.
- Use distance-based detail and pooled traffic. At orbital scale show settlement glow and major
  districts; at surface scale reveal buildings and activity; only nearby objects receive full
  geometry, animation and shadows.

Surface camera and continuous planetary descent:

- Replace the current surface camera binding with a mouse-first scale camera. Hold and drag the
  middle mouse button to look horizontally and vertically around the current focus. At high
  altitude this orbits and tilts around the selected settlement or surface point; near street
  level it behaves as free look while keeping the camera above valid terrain.
- Preserve left-button navigation as well. Holding and dragging the left mouse button pans the
  surface beneath the camera at bird's-eye and settlement scales and moves laterally at close
  range. A short click still selects terrain or a structure. Use a clear movement threshold and
  pointer capture so a drag never accidentally selects, places or activates a module when the
  button is released.
- Use the mouse wheel for continuous altitude and distance control. Scrolling inward moves from
  orbital context through atmosphere, regional bird's-eye, settlement overview and close street
  view. Scrolling outward reverses the same path and can return through atmosphere to the focused
  planet and solar-system view without a loading-screen-like jump.
- Scale camera movement, near/far clipping, selection tolerance, label density and rendered detail
  with altitude. Street view needs precise slow movement and readable buildings; bird's-eye view
  needs rapid traversal, district silhouettes and uncluttered labels.
- Preserve short left-click for selecting terrain, modules, buildings and placement confirmation.
  While a placement preview is active, a left-drag pans without building and a short valid click
  confirms placement. Provide visible mouse help and configurable alternate bindings for
  trackpads, accessibility and players whose middle button is unavailable, while keeping
  left-drag pan, middle-drag look and wheel zoom as defaults.
- Maintain focus through every transition. Zooming toward a selected planet, colony, district or
  building keeps that target beneath the cursor or screen focus; zooming back out restores the
  previous surface, planet and system camera positions.
- Use a visual scale transition backed by explicit detail levels and scene streaming. Planetary
  orbit renders the full globe, clouds and night side; descent reveals terrain regions and the
  selected settlement; street level loads detailed buildings, roads, traffic and local effects.
  Blend these stages so the player experiences one continuous descent without rendering an
  entire detailed planet simultaneously.
- Allow approach and atmospheric viewing of legitimately surveyed worlds. Surface construction
  and local management remain available only where ownership, landing capability, access and
  environment permit them. Camera access must not reveal hidden settlements, resources or life.

Atmosphere, sky and terrain rendering:

- Derive sky color and atmospheric scattering from the star's spectrum, atmospheric composition,
  pressure, density, aerosols/clouds, sun angle and viewing altitude. Earth uses a convincing blue
  daylight sky, warm horizon scattering, clouds and a darkening upper atmosphere because of its
  known oxygen-nitrogen atmosphere rather than a generic blue preset applied to habitable worlds.
- Give other worlds physically coherent skies: airless bodies retain a black sky and hard light;
  thin dusty atmospheres show weak colored haze; dense carbon-dioxide atmospheres produce heavy
  scattering and cloud cover; reducing methane-rich environments shift color appropriately;
  high-pressure or immersed environments use depth, absorption and particulate effects suited to
  their actual composition.
- Use ground and ocean reflectance to affect ambient light, horizon color and the underside of
  clouds without allowing surface color alone to determine the sky. Ice, bright sand, dark basalt,
  vegetation, hydrocarbons and water should produce different reflected-light character.
- Generate terrain materials and forms from authoritative surface properties: temperature,
  gravity, solvent, solid-surface state, atmosphere, radiation, geology and legitimately known
  biology. Do not add forests, oceans, cities or breathable-looking skies when survey data does
  not support them.
- Show altitude-dependent atmosphere during descent: space black, a thin limb, upper-atmosphere
  haze, cloud layers, horizon curvature and finally local sky. Weather, clouds, storms and surface
  lights use bounded simulation/presentation layers and fade to aggregate patterns at distance.
- Match lighting and shadows across the globe, atmosphere and local surface so the star direction,
  time of day, ring shadows, eclipses and nearby moons do not visibly change during a scale handoff.
- Use high-resolution/procedural terrain and atmospheric detail with distance-based texture and
  mesh streaming. Zooming closer must reveal finer detail rather than enlarging the orbital image
  until it blurs.

Purposeful surface, orbital and interplanetary traffic:

- Replace endlessly circling decorative shuttles with visible journeys that have an origin,
  destination, vehicle role and direction. Craft depart from actual pads, ports, industrial
  districts or settlements, follow local traffic corridors, climb through the atmosphere and
  continue toward a legitimate orbital or interplanetary destination.
- Generate routes from authoritative activity: passenger movement, freight demand, construction,
  colony supply, trade, orbital stations, moons, other planets, fleets and settlements. A quiet
  outpost shows occasional service flights; a developed capital shows multiple organized lanes.
  Power loss, blockade, damaged ports or absent orbital infrastructure visibly reduce traffic.
- Support several readable journey classes: local surface transports between districts,
  surface-to-orbit shuttles, cargo lifters, passenger craft, orbital transfer vehicles and
  interplanetary ships. Vehicle appearance, acceleration and flight path must match its role and
  the civilization's technology rather than using one shuttle for every purpose.
- Assign every visible departure a named known destination where player knowledge permits, such
  as `Luna Transfer Station`, `Mars`, an orbital shipyard or another surface region. Selecting or
  hovering a nearby craft may show role, origin, destination and broad status without exposing
  hidden cargo, orders or locations.
- Use believable flight phases: pad departure, safe low-altitude corridor, ascent, atmospheric
  transition and orbital departure vector. Arrivals enter from the direction of their actual
  route, descend through assigned corridors and land at a compatible destination instead of
  vanishing into or spawning from arbitrary circles over the city.
- Preserve visual continuity across camera scales when practical. A selected ascending shuttle
  can remain visible through upper atmosphere before becoming an orbital traffic marker; distant
  journeys continue as bounded aggregate traffic rather than requiring one continuously simulated
  physics object across interplanetary space.
- Derive cosmetic traffic instances deterministically from real aggregate flows and cap their
  number per visible region. Reuse pooled vehicles, simplified distant paths and altitude-based
  detail so a busy homeworld feels alive without creating save bloat or per-citizen simulation.
- Keep routes clear of buildings, terrain, launch exclusion zones and one another through
  generated corridors and altitude layers. Weather, atmosphere, gravity and available propulsion
  may alter departure style and frequency while never turning ambient traffic into uncommanded
  military or economic outcomes.
- Do not fabricate destinations merely for visual motion. If a colony has no other settlement,
  orbital installation, moon operation or reachable planet, traffic remains local until real
  activity creates a route.

Surface build interface:

- Open a graphical build tray directly on the surface with large category icons for Power,
  Housing, Research, Industry, Logistics, Services and Defense. Each card shows appearance,
  purpose, capacity use, power, workforce, inputs, outputs, construction cost, upkeep and
  research requirement before placement.
- Selecting a module creates a clear 3D ghost with green/amber/red validity, projected roads and
  utilities, terrain work, district effects and the exact reason an invalid location fails.
- Clicking a core or module opens its graphical status card with Upgrade, Repair, Prioritize,
  Disable, Relocate where allowed and Demolish actions. Upgrades visibly transform the existing
  structure instead of replacing it with an unrelated model.
- Show current and post-placement capacity, power, housing, workforce, logistics and support
  effects before confirmation. Player mode uses the civilization's active currency and never
  exposes premature Credits.

Implementation order:

1. Add persisted core type/tier and authoritative planet/regional development capacity with
   migration from existing generic hubs and the fixed 64-building limit.
2. Add module capacity, housing, workforce and explicit research prerequisites to the catalog;
   keep power, cost, upkeep, construction and save validation authoritative.
3. Add service radius, road/utility connectivity and smart placement/alignment without removing
   free placement.
4. Build the graphical module tray, capacity forecast and core/module management cards.
5. Replace generic hub and building geometry with tiered Capital Hub, Command Center and
   civilization/environment-specific modular art, then add roads and lived-in activity.
6. Teach AI to upgrade cores, expand service networks and place coherent functional districts
   under the same capacity and resource rules.

Acceptance criteria:

- Earth begins with a recognizable developed Capital Hub and existing urban context; a new
  colony begins with a small Landing Command Center and visibly grows through upgrades.
- Planet size and environment set a persistent long-term ceiling, while core tier and research
  control current usable capacity. The interface identifies the active constraint.
- Power, research, housing and industry modules can be freely placed, connected, constructed,
  upgraded, disabled and removed through mouse controls with honest costs and outputs.
- Settlements form readable road-connected districts with coherent foundations and activity,
  while the player retains manual positioning and optional blueprints.
- Left-drag pan, middle-drag look and wheel zoom move smoothly between street, settlement,
  bird's-eye, atmosphere and orbital views while preserving selection and camera focus.
- Earth presents a convincing blue atmosphere and terrain lighting; every other surveyed world
  derives its sky, haze, clouds, illumination and terrain appearance from its own authoritative
  star, atmosphere, pressure, surface and solvent data.
- Visible shuttles and interplanetary vehicles travel between legitimate origins and destinations,
  enter or leave the atmosphere along believable routes and respond to actual colony/logistics
  activity instead of circling endlessly as disconnected decoration.
- Player and AI use identical capacity, research, construction, power, workforce, logistics and
  environmental-support rules, and every state survives save/load.
- Large developed worlds remain responsive and visually legible at surface and orbital scales
  without simulating or rendering unbounded individual buildings, citizens or vehicles.

### Harsh-world resource outposts and later colonization

Status: the staffed settlement kind, dedicated construction vessel, exact-body player
command, lane/fuel reach, founding population transfer, baseline upkeep and limited refueling
are implemented. Powered processing and bounded persistent local storage are also implemented;
stored material does not become empire Industry until a represented bulk freighter collects it
and returns to a developed colony. Player freight dispatch, bounded cargo, lane/fuel routing and
mid-run persistence are implemented. The initial sealed hub limits the surface to eight
appropriate power, processing, science or habitat modules and disallows civilian trade hubs;
later outpost tiers will raise this limit. Repeating schedules, multiple cargo types, supply
delivery, failure consequences, terraforming and colony conversion remain planned.

- Allow a fully surveyed resource-rich world to qualify for a staffed extraction outpost even
  when no available population can presently colonize its natural environment. Qualification
  requires a known valuable deposit, a solid or otherwise technologically supportable operating
  environment, reachable logistics, suitable transport and the research needed to survive the
  identified hazards.
- Establishing an outpost consumes a real construction mission, equipment, industry, active
  currency and personnel transferred or assigned from an existing population. It never creates
  free inhabitants, instantly claims every resource on the planet or bypasses survey knowledge.
- Begin with a **Sealed Outpost Hub** containing a pressure dome or equivalent species-specific
  controlled habitat, emergency shelter, power, life support, communications, landing access and
  minimal storage. Its small crew can operate nearby extraction, science and logistics modules
  within a tightly limited serviced area.
- Derive dome requirements and upkeep from the exact mismatch between crew biology and local
  gravity, pressure, temperature, atmosphere, solvent/immersion and radiation. A harsher world
  requires stronger containment, more power, more replacement supplies and more expensive crew
  rotation; an interrupted life-support chain can cause evacuation, injury or loss.
- Treat outpost personnel as a maintained crew rather than an automatically growing civilian
  population. Crew have origin, species/lineage, rotation needs, transport capacity and living
  limits. Long residence may contribute legitimate acclimatization or adaptation only when the
  supported environment and population conditions satisfy the normal cohort rules.
- Give an outpost a narrow upgrade path: Survey Camp, Sealed Outpost, Industrial Outpost and
  Regional Extraction Base. Upgrades expand crew, storage, power, safety and extraction capacity
  but do not silently turn the installation into a normal colony.
- Connect extracted materials to real freight capacity and destination demand. Production stops
  or stockpiles when power, crew, equipment, storage or transport is unavailable, and the Economy
  and Logistics pages show revenue, upkeep, delivered output and shortfall separately.
- Outposts create a limited presence and may support a political claim, but sovereignty,
  diplomacy, blockade, seizure and attack remain governed by the normal territorial and military
  systems. An isolated dome is strategically vulnerable and cannot defend a planet by label.

Terraforming and conversion:

- After the required Adaptive Research capabilities mature, allow a civilization to begin a
  long, staged terraforming program from a supported outpost. Candidate stages include orbital
  and surface assessment, atmosphere/pressure intervention, temperature and radiation control,
  solvent/ecological preparation, stabilization and final biological certification.
- Terraform the physical environment itself over time. Each completed stage changes authoritative
  pressure, temperature, atmosphere, solvent, radiation or surface conditions in bounded steps,
  visibly alters the globe and surface, and recalculates suitability for every species/lineage.
  Terraforming for one species may make conditions worse for another.
- Require continuous industry, energy, specialist facilities, logistics, research competence and
  funding. Pauses, shortages, sabotage, war, equipment failure and unexpected planetary feedback
  can delay or partially reverse progress; the program is never a single instant purchase.
- Preserve scientific and ecological consequences. Existing native life, pre-space societies,
  protected environments, foreign claims and inhabited domes create diplomatic, ethical and
  safety decisions before irreversible stages proceed.
- Permit formal colony conversion only when a chosen population's resulting environment meets
  the maintained colonization threshold and the site has the required housing, services,
  administration and permanent population. Conversion upgrades the outpost core into a Landing
  Command Center or higher valid tier, retaining compatible extraction modules and recorded
  history rather than deleting and rebuilding the site.
- Keep sealed outposts useful when terraforming is impossible or unwanted. They remain limited,
  expensive industrial/scientific installations and never gain ordinary colony population growth,
  broad construction or political status without satisfying conversion requirements.

Outpost acceptance criteria:

- A surveyed valuable harsh world can expose an Outpost action with exact construction,
  transport, crew, life-support and logistics requirements plus a clear locked reason when any
  requirement is missing.
- The outpost displays its dome integrity, environment controls, crew, power, supplies, storage,
  extraction and freight state on both the surface and management pages.
- Loss of support produces visible operational consequences and cannot continue full extraction
  or preserve personnel through an unexplained hidden exemption.
- Terraforming changes authoritative planet data gradually, survives save/load and updates art,
  habitability, adaptation pressure, outpost cost and colonization eligibility consistently.
- Conversion to a colony occurs only after environmental and settlement requirements pass, keeps
  existing compatible infrastructure and uses the same rules for player and AI civilizations.

## Immediate roadmap — Realistic costs and lifecycle economics

Status: planned replacement for individually hand-tuned prototype prices. The existing
`1 prototype Credit = $10 million` mapping is explicitly rejected as a future currency design.
It remains only as a legacy scale factor for migrating already-created saves; future
player-facing prices use the civilization's active full-scale currency and a maintained
physical/economic cost model.

Cost foundation:

- Use constant 2050 purchasing power as the Human baseline so inflation does not make design data
  meaningless. Record the reference year, source, assumed technology maturity, project scale and
  uncertainty range for every real-world calibration anchor. Other civilizations price the same
  underlying requirements in their own currencies and economic conditions.
- Display Human costs directly in full Dollars with readable suffixes and precise detail on
  demand: `$850K`, `$42.6M`, `$3.2B` or `$1.1T`. Do not divide every strategic price into an
  invented small-number token merely to keep the interface compact.
- Derive a project's total requirement from explicit components:
  **materials + components + energy + labor + design/tooling + transport + site/environment work
  + administration + risk/contingency**. Apply local prices and exchange rates afterward; never
  assign a race-wide arbitrary cheap/expensive multiplier.
- Keep money, materials, Industry, workforce and time distinct. Currency authorizes purchases,
  labor and contracts; materials are physical inputs; Industry is available productive capacity;
  workforce operates facilities; elapsed construction performs the work. Paying one requirement
  cannot silently satisfy all the others.
- Give every asset a complete lifecycle: design/development, site preparation, construction,
  commissioning, staffing, power/fuel, routine operation, maintenance, consumables, repair,
  refit/upgrade, decommissioning and salvage. A low purchase price cannot hide enormous operation
  or support expense.
- Calculate scale from authoritative physical specifications where available: mass, volume,
  capacity, power, crew, habitat volume, radiation/pressure protection, propulsion, cargo,
  construction environment and distance. Catalog multipliers express a real cause such as scarce
  materials or hostile-world containment and must identify that cause in the UI.
- Use bounded regional markets and strategic aggregation. The simulation does not need to price
  every bolt or citizen purchase; it needs consistent bulk material, energy, labor, transport,
  equipment and service categories that reconcile with the civilization economy.

Category requirements:

- **Surface modules and cities:** include structure, foundations/terrain work, roads, utilities,
  grid connection, environmental sealing, equipment, staffing and maintenance. Planet gravity,
  pressure, atmosphere, temperature, radiation, remoteness and local materials alter real inputs.
- **Power:** show construction, rated output, capacity factor/fuel, grid/storage requirements,
  maintenance, waste heat and decommissioning. A cheap generator with unreliable output is not
  equivalent to dependable baseload power.
- **Research:** price laboratory construction, instruments, specialists, samples, facilities,
  energy and operations. Research progress consumes finite lab time and applicable competence;
  money cannot directly purchase completed knowledge.
- **Industry and extraction:** include mines or collection systems, processing, machinery,
  replacement parts, energy, labor, storage, pollution/heat handling and freight. Revenue exists
  only when useful output reaches a buyer or consuming project.
- **Ships and stations:** include hull mass, drive, reactor, radiators, life support, sensors,
  payload, weapons where applicable, shipyard work, crew training and commissioning. Operation
  includes crew, fuel/reaction mass, maintenance, spares, port services, ammunition, repairs and
  readiness; damage and distance change actual cost.
- **Colonization and outposts:** include transport craft, colonists or rotating crew, equipment,
  initial shelter, power, life support, supplies, landing infrastructure, reserves and return or
  evacuation capacity. Hostile destinations must cost more for visible physical reasons.
- **Terraforming and megaprojects:** calculate staged equipment, energy, transported/local mass,
  industrial throughput, specialists, maintenance and decades of operation. Present phase and
  remaining lifecycle costs rather than one implausible purchase price.
- **Population and government:** represent housing, healthcare, education, public services,
  administration, environmental support, security and infrastructure maintenance at population
  scale. Revenue comes from actual productive activity and policy, not population multiplied by
  a universal money constant.
- **Military operations:** include recruitment, training, equipment, deployment, supply,
  readiness, munitions, casualties, replacement, repair, occupation and demobilization. Winning a
  battle does not erase its economic cost.
- **Trade, finance and diplomacy:** include freight, insurance/risk, tariffs, exchange spread,
  contract terms, sanctions and payment availability. Changing currency denomination cannot
  create purchasing power or make a physical project cheaper by itself.

Pricing behavior:

- Begin with maintained regional reference prices for bulk categories, then modify them from
  local supply, demand, reserves, productive capacity, transport distance, hazard, blockade and
  technology. Smooth ordinary changes over suitable intervals so prices do not flicker every
  simulation frame.
- Separate an engineering estimate from a final committed authorization. Early survey or design
  produces a range; improved knowledge narrows it. Before confirmation, show expected cost,
  uncertainty, construction time, recurring operation and the largest cost drivers.
- Reserve funding and physical inputs when an order begins, then spend them through construction
  milestones. Cancellation recovers only unspent funding and reusable materials; completed work,
  consumed energy, labor and damaged/specialized components are not magically refunded.
- Re-estimate genuinely variable future stages while protecting already signed contracts and
  acquired materials according to their terms. Make overruns, shortages and delays explainable
  events rather than silent number changes.
- Use the civilization's issuing currency for domestic projects. Foreign purchases settle through
  an available exchange pair or Credits after those systems become usable, with the rate, spread
  and settlement currency visible before commitment.
- Maintain sensible price compression for play by choosing project scale and government budget
  scope, not by breaking relative costs. A surface `Power Complex` may represent an entire program
  of facilities; its description and output must match that scale consistently.

Player presentation:

- Every build, research, ship, outpost, colony, upgrade, repair and policy card shows its active
  currency cost, required Industry/materials, workforce where relevant, estimated completion time,
  recurring upkeep and expected output or capacity before authorization.
- Provide a concise total first and an expandable cost breakdown. Highlight the limiting input and
  explain why the cost differs between locations, species, currencies or technologies.
- The Economy page reconciles opening balance, income, operating expenses, reserved commitments,
  construction spending, exchange, debt where supported and closing balance for the selected
  period. No resource or currency should continuously increase without a visible source and cap,
  market, storage or demand consequence.
- Developer mode can expose formulas, reference-price IDs and raw quantities. Player mode uses
  clear units, tooltips and comparisons without requiring the player to audit implementation data.

Calibration and validation:

1. Define versioned physical specifications and cost-component schemas before retuning prices.
2. Build a documented Human 2050 reference catalog for energy, labor, launch/transport, major
   materials, industrial facilities, scientific facilities, spacecraft and infrastructure.
3. Reprice the complete early-game construction, surface, ship, colony and operating catalog from
   those components, then convert existing saves without deleting paid assets or duplicating funds.
4. Add civilization-local price baskets and currency conversion only after the underlying physical
   costs reconcile in the Human baseline.
5. Run campaign affordability tests across multiple seeds and species, adjusting starting economy,
   project scale or physical assumptions rather than applying unexplained discounts.

Acceptance criteria:

- Every player-authorized asset and activity has documented construction and recurring-cost
  components with consistent units, reference year and scale.
- Two equivalent projects in equivalent conditions consume equivalent real resources regardless
  of player/AI ownership or currency label; legitimate local differences produce an explainable
  cost breakdown.
- Total displayed costs reconcile exactly with authoritative balances, inventories, Industry,
  construction progress and recurring cash flow through save/load and cancellation.
- No project can operate indefinitely without its required staffing, power, maintenance,
  consumables and logistics, and no income appears without a traceable productive source.
- Early play provides meaningful choices and financial recovery paths without trivializing ships,
  colonies, hostile-world outposts, advanced research or terraforming.

## Immediate roadmap — Labor-backed production and public finance

Status: planned core-economy correction. The current prototype derives recurring colony revenue
from population and lets powered surface buildings produce without workers or material inputs.
Remove those placeholders before broader economic expansion. Population can generate household,
income, payroll, sales and property tax revenue when people are employed, paid and participating
in a functioning economy. Headcount by itself is not a guaranteed treasury payment.

Closed production rules:

- A facility operates only when it is complete, connected to sufficient power, staffed by the
  required workforce, supplied with its material and service inputs, maintained, connected to
  adequate transport and able to store or deliver its output. Show each constraint separately.
- Set its operating fraction from the tightest required constraint: power, suitable labor,
  materials, logistics, maintenance or output capacity. A half-staffed plant can produce at most
  half output; an unpowered, unstaffed or unsupplied facility produces nothing.
- Track broad job groups rather than individual citizens: operators and trades, scientists and
  technicians, logistics and services, administration and construction. Each worker can fill only
  one job. Skills, health, gravity and pressure adaptation, commuting access and policy affect the
  effective labor pool.
- Let the player prioritize essential services and strategic facilities when workers or power are
  scarce. Automation reduces labor requirements only through researched equipment and adds its
  own power, specialist, component and maintenance needs.
- Power plants need operators, maintenance and their appropriate fuel or environmental source.
  Industry consumes materials, energy and labor to create finite physical output. Laboratories
  consume specialist labor, power, equipment and supplies to provide research capacity.
- Housing increases supported population but needs utilities and services; it does not create
  jobs or revenue by itself. Population consumes housing, food, potable water, life support where
  required, healthcare and ordinary goods. Shortfalls reduce health, stability, labor availability
  and growth through visible consequences.
- Cap physical inventories by available storage. When storage, freight capacity or demand is
  exhausted, curtail production instead of accumulating Industry, goods or research forever.

Population support and growth:

- Calculate a colony's sustainable population from the dependable food and potable-water supply
  that actually reaches it, then constrain it further by housing, atmosphere or life support,
  healthcare, sanitation, energy and local environmental safety. Show both the present population
  and supported capacity with the limiting need.
- Treat the support limit as a carrying capacity rather than an instant hard clamp. Births and
  immigration slow as the colony approaches capacity. Persistent shortages cause rationing,
  emigration, illness and eventually deaths at explicit rates; population can never continue
  exponential growth while food or water is missing.
- Reserve emergency food and water stockpiles and consume them during disrupted production or
  freight. The displayed support horizon states how long reserves last at current consumption.
- Calculate food and water per species and environment. Recycling lowers gross demand but needs
  power, equipment and maintenance and can never provide unexplained perfect recovery. Harsh
  outposts include sealed-life-support capacity in their personnel limit.
- Population assigned to agriculture, water extraction, treatment, logistics and maintenance is
  part of the same finite labor pool as every other job. Expanding support capacity therefore has
  a visible construction, operating and workforce cost.

Money and revenue rules:

- Treasury balances rise only through named transactions: taxes and fees on actual household or
  business activity, dividends from public enterprises, completed domestic or export sales,
  contracts and transfers, or explicit finance such as borrowing and monetary issuance when those
  systems exist. Every increase appears in the ledger with its source.
- A trade hub provides market access, warehousing and freight capacity. It earns revenue only from
  delivered trade and applicable fees; it never prints a fixed amount of money merely because it
  is powered.
- Distinguish gross economic output from government revenue. The treasury receives the selected
  tax, fee or ownership share rather than the whole value produced by the civilian economy. Tax
  changes affect compliance, household surplus, demand, investment, stability and growth over
  time. Employed population can provide recurring tax revenue, but unemployed or unsupported
  population cannot pay a fictional fixed tax merely to keep the treasury increasing.
- Charge construction spending when work is authorized and completed through milestones, and
  charge wages, supplies, utilities and maintenance while facilities operate. These expenses reduce
  the treasury surplus; expanding faster than the tax base and reserves support creates a visible
  deficit and can pause projects or operations.
- Operating spending represents wages, suppliers and services within the wider economy. The first
  implementation may aggregate the private sector, but opening balances, payments, tax receipts,
  trade settlement and closing balances must reconcile. Money supply changes only through an
  explicit issuance, retirement, credit or debt mechanism with later inflation consequences.
- Established homeworlds such as Earth begin with represented civilian economic sectors,
  infrastructure and employed workers, so they can earn real starting revenue. A new colony begins
  only with the activity its command center, settlers and constructed facilities can support.

Player presentation:

- Give every building an operating card showing required and assigned workers, power, inputs,
  maintenance, logistics, storage, output, operating percentage and the current limiting reason.
- Add Economy-page views for employment, unemployment, vacancies by job group, wages, productive
  output, taxable activity, tax receipts, public-enterprise revenue, imports, exports, operating
  spending, commitments and treasury reserves.
- Explain stalled output directly on the map and surface: `Needs 320 operators`, `Fuel supply 62%`
  or `Output storage full`. Let mouse-first controls assign priority or open the relevant remedy.
- Use the same accounting and operating rules for player and AI civilizations. Developer mode may
  expose equations and raw sector values; player mode receives the same results in readable form.

Implementation order:

1. Add workforce supply, job demand, assignments and facility operating fractions to colony state,
   including save migration and deterministic simulation tests.
2. Add food, potable water, species consumption, supported population capacity, reserves and
   shortage-driven demographic effects before allowing continuing population growth.
3. Add material inputs, storage, logistics and curtailment to power, research, industry and trade
   facilities; remove unconditional building output.
4. Replace the fixed population-revenue multiplier with employed household and business activity,
   taxable transactions and explicit tax policy. Seed homeworld employment and sectors explicitly.
5. Add reconciled treasury and economy ledgers, then connect construction and operating spending,
   maintenance, trade, currencies, exchange and AI decisions to them.
6. Replace the current HUD counters with operating, population-support, employment and finance views
   and retune the starting economy across species and representative 100-system Sandbox seeds.

Acceptance criteria:

- Population produces tax receipts only from traceable taxable income, property or consumption;
  unemployed headcount alone cannot increase the treasury.
- An unpowered, unstaffed or input-starved facility produces zero; partial staffing and supply scale
  output deterministically, and the same worker pool cannot staff two facilities.
- Population growth approaches zero at the supported capacity; removing food or water production
  consumes reserves and then causes explicit demographic harm instead of continued growth.
- Construction and operation reduce the treasury by their recorded milestone and recurring costs,
  and the displayed surplus or deficit reconciles with the closing balance.
- Disconnecting or demolishing a facility immediately removes its jobs, costs and output without
  leaving a hidden income source.
- Credits, local currency, Industry, goods and research never increase without a traceable producer,
  transaction or authorized monetary event, and physical output respects storage and demand.
- A valid starting Earth economy remains playable because its initial facilities, civilian sectors,
  workers, consumption and taxes balance visibly rather than through passive population income.
- Save/load preserves assignments, shortages, inventories, ledgers and operating fractions exactly;
  repeated simulation from the same seed produces the same result.

## Immediate roadmap — Operational realism and sustainable expansion

Status: planned in staged delivery. These systems form one causal economic simulation rather than
independent penalties. The first six complete the initial playable production loop; the remaining
six deepen it after that loop is understandable, stable and fun at 100-system scale. Model people,
firms and commodities in strategic aggregates rather than simulating every citizen or item.

### 1. Finite resource deposits

**Implemented foundation:** staffed rare-resource outposts now receive a deterministic,
body-scaled finite reserve. Extraction reduces that reserve and is bounded by remaining material,
processor output and local storage. Depletion, persistence and player-facing reserve reporting are
covered. Fully surveyed sites also expose a deterministic material family, bounded grade and
environmental accessibility that change processor yield. Survey uncertainty, depth, multi-deposit
sites, equipment specialization and declining marginal yield remain.

- Give extractable sites a resource type, estimated quantity, grade, accessibility, hazard and
  confidence based on survey quality. Better surveys narrow estimates instead of revealing false
  precision immediately.
- Make extraction cost and output depend on grade, depth, gravity, environment, equipment, energy,
  labor and transport access. Rich accessible deposits are meaningfully cheaper to exploit.
- Reduce remaining reserves as material is extracted. Declining grade or access raises marginal
  cost and can make an old site uneconomic before every unit is removed.
- Let processing, improved extraction and recycling recover more useful material without creating
  matter. Closing a site requires safe shutdown, reclamation where applicable and salvage.

### 2. Physical freight and travel

**Implemented foundation:** outpost material already travels aboard a finite-capacity bulk freighter
over the real lane route. Loading and unloading now consume time at the vessel's maintained 20
material-unit/day transfer rate. A basic settlement hub handles 4 units/day, while a buildable, staffed
and powered Cargo Terminal adds 20 units/day up to the vessel limit. Partial cargo remains aboard across
ticks and saves, unfunded freight handling stops, and a full destination warehouse blocks unloading
without deleting material. Congestion, multiple commodity manifests, fuel loading and route risk remain.

- Move food, water, fuel, raw materials, components and finished goods through routes with finite
  vehicle capacity, loading capacity, travel time, range, fuel, crew, maintenance and risk.
- Require a valid origin inventory and destination demand. Reserve cargo at loading, keep it in
  transit, then deliver it on arrival; never teleport shared empire stockpiles between colonies.
- Let distance, congestion, damaged ports, missing vehicles, hazards, piracy, war and blockade
  reduce throughput or interrupt delivery. Route views identify the exact bottleneck and loss.
- Permit local reserves and alternate routes so players can build resilience. A colony can suffer a
  shortage even when sufficient goods exist elsewhere but cannot reach it in time.

### 3. Skilled workforce and training

- Divide available labor into broad groups: general workers, construction trades, operators,
  logistics and services, scientists and technicians, medical workers, administrators and trained
  military personnel. Species and cultures may organize roles differently but obey equal rules.
- Require matching qualifications for advanced facilities. Having enough total population cannot
  substitute instantly for missing reactor operators, physicians or researchers.
- Use schools, universities, academies, apprenticeships and workplace training to change skill
  supply over time. Training consumes teachers, facilities, funding and student time.
- Support wages, working conditions, safety, migration policy and quality of life as reasons workers
  move between colonies. Show vacancies, underemployment and the time needed to fill shortages.

### 4. Maintenance, wear and reliability

**Implemented foundation:** completed surface complexes now retain persistent physical condition.
When base operating upkeep is underfunded, enabled complexes lose condition at a deterministic rate;
shutting a complex down prevents that operating wear. Reduced condition lowers effective generation,
production and support, and a complex at or below the failure threshold releases its workers and power
allocation until repaired. The landed surface shows condition, efficiency and an exact repair quote,
and a player-authorized repair consumes stored Materials. Preventive-maintenance labor, spare-part
categories, downtime, age and probabilistic failures remain.

**Implemented environmental exposure:** gravity departure, vacuum or extreme pressure, severe heat
or cold, and elevated radiation now produce a bounded world-specific maintenance multiplier. Fully
funded preventive work offsets this exposure; when maintenance is deferred, hostile-world structures
lose condition faster by the exact multiplier shown on Surface and Colonies views. Shutdown still
prevents operating wear.

- Give buildings, infrastructure, ships and equipment condition, maintenance demand, expected
  reliability and age. Use class-level cohorts where individual asset tracking adds no decision.
- Consume technicians, spare parts, money and downtime for preventive maintenance. Harsh pressure,
  temperature, radiation, corrosion, dust, gravity and heavy utilization increase wear visibly.
- Reduce efficiency and safety when maintenance is deferred, then allow bounded failures whose
  probability and cause are shown. Preventive work should usually cost less than breakdown repair.
- Support inspection, repair, overhaul, upgrade, mothballing and decommissioning. Nothing remains
  fully productive forever solely because its initial construction completed.

### 5. Staged construction and temporary labor

**Implemented visibility foundation:** every surface site now derives a deterministic physical phase
from authoritative material progress: site preparation, foundations and utilities, primary structure,
equipment installation, then testing and commissioning. The selected site and its in-world label show
phase progress, overall completion and remaining Materials. Existing allocation, pause and save/resume
rules remain authoritative. Phase-specific labor, power, freight and milestone consumption remain.

- Build through site survey and preparation, foundations and utilities, structure, equipment
  installation, testing and commissioning. Each stage has explicit material, labor, power,
  transport, funding and environmental requirements.
- Assign finite construction workers and machinery across projects. Starting too many projects
  divides capacity and slows them rather than creating free parallel work.
- Charge and consume inputs at milestones, show committed and remaining requirements, and preserve
  completed physical work through pauses and save/load.
- Require protection and limited upkeep for unfinished sites. Cancellation returns only reusable,
  unconsumed material and cannot refund performed labor or damaged specialized equipment.

### 6. Power grids, storage and priority

**Implemented priority and storage foundation:** instantaneous local supply and demand determine which
staffed complexes operate, and manual Priority remains the highest player override. When no override
exists, the grid now protects generators, potable water, controlled food and habitat support in that
order before discretionary research, industry and trade. Selection labels essential services and
distinguishes automatic protection from player priority. A buildable Grid Battery Complex stores a
finite 12 grid-power-days (288 GWh at the displayed scale), charges or discharges at no more than
4 GW, loses 10% on charge and discharge, requires workers and upkeep, persists exact local energy,
and limits support to what can last through the actual simulation interval. Fuel, generator startup,
renewable variability and player reserve policies remain.

- Model power as instantaneous generation and demand plus bounded stored energy, not as an
  endlessly accumulating resource. Show rated output, available output and actual load separately.
- Give generators fuel or environmental inputs, capacity factor, ramp or startup limits where they
  matter, operators, maintenance and grid connection. Weather and local conditions affect relevant
  renewable sources without excessive frame-to-frame noise.
- Give batteries and other storage capacity, charge/discharge limits, efficiency loss, condition
  and reserve policy. Stored energy bridges disruptions but cannot replace sustained generation.
- During shortages, follow player-set priorities with safe defaults: life support, potable water,
  emergency care and command before discretionary industry. Show curtailed facilities on the map.

The first playable economic loop is complete when the player can **extract resources, transport
them, construct facilities, staff and power them, maintain output, collect taxes and fund the next
expansion**, with every failure traceable to one of those links.

### 7. Food, water and supply quality

- Track food energy and potable water as strategic bulk supplies with production, treatment,
  storage, spoilage or contamination risk and per-species consumption.
- Make agriculture depend on suitable land or controlled habitat, light, water, nutrients,
  equipment, power and labor. Model wells, surface water, ice extraction, desalination, treatment
  and recycling as location-appropriate water sources.
- Let contamination, crop failure and infrastructure damage reduce usable supply rather than only
  changing a cosmetic status. Inspection and monitoring reduce risk and improve warning time.
- Prevent perfect recycling. Higher recovery requires researched equipment, power, maintenance and
  replacement inputs and still leaves losses that must be replenished.

### 8. Population structure and migration

- Track strategic age and participation cohorts so children, students, workers, caregivers,
  patients and retirees produce a visible dependency ratio and labor supply.
- Derive births and deaths from species life history, health, safety, housing, nutrition, policy and
  confidence in the future. Avoid a universal exponential growth percentage.
- Make migration respond to available housing, jobs, wages, environment, rights, safety, services,
  family or cultural ties, distance and transport capacity. Moving people takes time and vehicles.
- Preserve lasting consequences from casualties, emigration and aging: a colony can lose scarce
  skills or face years of dependency even after its total headcount recovers.

### 9. Waste, pollution and heat

- Give settlements, reactors, extraction and industry solid, liquid, atmospheric and thermal waste
  outputs appropriate to their process and environment.
- Require collection, treatment, recycling, containment or safe disposal capacity. Sealed habitats
  and spacecraft cannot hide waste or reject heat without functional systems.
- Let accumulated pollution affect health, agriculture, ecosystems, habitability, maintenance and
  stability. Display sources, affected regions and recovery time before the player commits a site.
- Support cleaner processes, remediation and waste reuse with real costs and limits. Moving waste
  elsewhere transfers the burden and needs transport rather than deleting it.

### 10. Regional markets and prices

- Use a small readable set of strategic goods and services. Calculate regional prices from supply,
  demand, inventory, productive capacity, freight cost, risk, scarcity and policy at stable update
  intervals.
- Separate physical output, sale quantity, sale price, business income and government tax receipts.
  Unsold output enters available storage or causes curtailment instead of automatic revenue.
- Let shortages raise prices and suppress dependent production; new capacity can lower prices and
  margins. Government procurement competes with civilian demand and has a visible budget cost.
- Show current price, recent trend, available volume, major suppliers and the causes of unusual
  changes without requiring the player to manage thousands of individual contracts.

### 11. Public budgets and finance

- Divide the treasury into an opening balance, tax and fee receipts, public-enterprise dividends,
  trade and transfers, construction spending, operations, public services, defense, debt service,
  reserves and closing balance.
- Support income or payroll, consumption, property, business-profit, extraction and trade taxes only
  where the civilization's institutions use them. Each tax has a defined base and collection rate.
- Make tax policy affect household surplus, demand, investment, migration, compliance and stability
  over time. Do not use one universal tax penalty or instantly collect nominal rates in full.
- Add borrowing, interest, default risk and monetary issuance only after the core ledger reconciles.
  These can bridge deficits but create future costs and inflationary pressure rather than free money.

### 12. Emergencies and resilience

- Let colonies prepare food, water, medicine, fuel and spare-part reserves; backup generation;
  redundant utilities; fire, medical and rescue services; shelters; and evacuation capacity.
- Generate accidents and disasters from authoritative exposure such as weather, geology, radiation,
  warfare, unsafe utilization, poor maintenance or inadequate staffing. Avoid arbitrary punishment.
- Provide forecasts, inspections, alarms and response time whenever knowledge and sensors allow it.
  Player preparation changes severity, recovery time and casualties.
- During an emergency, expose direct choices for rationing, shutdown priorities, reserve release,
  repair allocation, outside aid and evacuation, each with forecast costs and consequences.

Delivery sequence:

1. Implement deposits, freight, skilled labor, maintenance, staged construction and power behavior
   together with the labor-backed economy; these are required for the first sustainable colony loop.
2. Seed Earth and other homeworlds with explicit working infrastructure, inventories, routes,
   workforce and maintenance obligations that reconcile with their opening budgets.
3. Add food and water quality plus population cohorts and migration, then balance colony carrying
   capacity and recovery across playable species.
4. Add waste and heat consequences, regional markets and detailed public budgets after physical
   production and consumption are stable.
5. Add emergency generation and resilience tools last, using the same facilities, inventories,
   routes and hazards rather than a disconnected random-event system.

Acceptance criteria:

- No resource, cargo, worker, energy unit, completed construction stage or payment appears at a
  destination without a source, capacity and elapsed process that can be inspected.
- Resource deposits deplete, freight takes time, qualifications constrain advanced operations,
  assets wear, construction consumes staged inputs and power failures follow priority rules.
- Food and water shortages respect inventories and freight, constrain population, and produce
  predictable recovery or harm rather than silent growth.
- Goods create revenue only when sold or transferred under a defined transaction; prices, taxes,
  spending and treasury balances reconcile for player and AI civilizations through save/load.
- Pollution, failures and disasters derive from visible conditions, and preparation materially
  improves outcomes without guaranteeing perfect safety.
- A representative 100-system Sandbox remains responsive and understandable without citizen-level
  simulation, while developer mode can explain every aggregate calculation.

## Immediate roadmap — Funded research and realistic technology costs

Status: recurring operations, one-time program authorization and reserved maturity-milestone funding
are implemented on Core; requirement-specific physical-test costs remain planned. Every directed
technology program requires both Research Points and money.
RP represents accumulated scientific and
engineering work; funding pays the people, institutions, equipment, materials, prototypes, test
operations and support that make that work possible. Neither requirement can replace the other.

Research cost model:

- Give every research possibility a required RP total plus a versioned financial-cost profile in
  the civilization's active currency. Avoid a single one-time `buy technology` price.
- Divide financial cost into **program authorization**, **recurring research operations** and
  **milestone costs**. Authorization establishes the team and facilities; recurring spending pays
  wages, utilities, instruments, computing, samples and administration; milestones fund prototypes,
  field trials, test articles, specialized facilities, certification or deployment validation.
- The current campaign command charges 0.5/2/7.5/25 Credits to authorize Foundation/Developing/
  Advanced/Frontier work, then applies the lab-scaled daily operating cost. It requires enough cash
  for authorization, milestone reserve and the first operating day, charges only after the research authority accepts
  the order, and is shared by Player and AI starts. These are initial playable values to calibrate
  against the later requirement-derived financial profiles.
- The same command reserves 0.3/1/3/8 Credits for the three prototype and validation boundaries.
  One-third is consumed when the program reaches Demonstrated, one-third at Engineering and the
  remainder at Mature. The unspent balance is persisted per active project and malformed saves that
  overspend or attach a reserve to an inactive project fail closed. Older campaigns migrate without
  a retroactive treasury charge.
- Research cards expose each visible program's catalog-backed physical facility capability for its
  starting or current stage. Existing eligibility gates remain authoritative: reserving money does
  not satisfy a missing containment lab, precision instrument, prototype center or test facility.
- Active research cards provide direct Pause and Resume actions. Pausing immediately removes the
  program from recurring research burn; resuming requires current scientific/facility eligibility
  and enough treasury for the first operating day. Hypothesis-resolution pauses remain distinct and
  cannot be bypassed by the ordinary resume command.
- The Economy Research line reports active authorization already paid, unspent prototype/validation
  reserves and current operations per day. These capital values persist with project funding and
  are validated on load; completed-program history remains future ledger work.
- Derive costs from explicit requirements: project duration, assigned Effective Research Labs,
  specialist workforce, facility class, equipment, computing or energy demand, rare materials,
  experimental articles, test environment, safety and containment, data collection, logistics and
  uncertainty reserve.
- Deduct actual spending from the same treasury and ledger used by construction and operations.
  Research is part of the civilization budget and can reduce or eliminate the current surplus.
- Keep required physical facilities and materials separate from money. Funding authorization cannot
  create a missing particle accelerator, biological containment complex, test spacecraft, sample or
  qualified research workforce.
- Use stable real-price assumptions and then express the result in the civilization's current
  currency. Inflation and exchange rates alter the nominal amount without changing the underlying
  laboratories, labor, materials or test work.

Cost growth with technological sophistication:

- Make Foundation research comparatively affordable and broadly supportable, Developing research
  more specialized, Advanced research institution- and prototype-intensive, and Frontier research
  a major sustained program. Maintain rising median RP, minimum-lab, facility and financial costs
  across those complexity bands.
- Do not apply an unexplained price multiplier solely because a node appears later in the tree. A
  better technology costs more when it requires greater precision, energy, scale, rarity, safety,
  specialist knowledge, integration, experimentation or validation.
- Allow legitimate exceptions. A powerful mathematical insight can be financially modest but RP
  intensive; a conceptually simple civilization-scale reactor or starship test can be financially
  enormous. The UI explains the dominant cost drivers.
- Scale applied engineering and deployment research with the physical system being proven. A new
  laboratory method, surface reactor, capital ship drive and planetary terraforming process require
  very different prototypes even at similar knowledge complexity.
- Increase uncertainty at the frontier. Initial estimates use a range; evidence, surveys, preliminary
  studies and demonstrated components narrow it. Cost growth must come from visible discoveries or
  failures rather than arbitrary hidden overruns.

Funding, labs and progress:

- Assign labs and an authorized budget to each active directed program. The project operating
  fraction is limited by available labs, suitable researchers, facilities, power, inputs, logistics
  and released funding, using the same constraint model as productive buildings.
- More assigned labs increase RP generation under the existing diminishing-return rules and also
  increase wages, utilities, equipment use and consumable spending. Extra money cannot bypass those
  diminishing returns.
- Offer bounded funding postures such as Conservative, Standard and Accelerated. They change staffing,
  equipment access, redundancy and test tempo within real limits; they never multiply RP from the
  same unchanged resources.
- Pause RP progress when required operating funding or physical inputs are unavailable. Preserve
  completed knowledge, but prolonged pauses can disperse assigned specialists, lose reserved test
  windows or require recommissioning before work resumes.
- Reserve known milestone funding when the player approves a program where policy requires it, while
  showing recurring burn, committed amount, remaining estimate and treasury runway. Let the player
  pause or reduce funding before insolvency with explicit time and continuity consequences.
- Unassigned laboratories still perform background science under their institutional operating
  budgets. They cannot generate a free bank of generic RP while receiving no workforce, power,
  maintenance or funding.

Research outcomes, foreign knowledge and failure:

- Completion of the RP requirement establishes the appropriate knowledge outcome only after required
  demonstrations and milestone validation finish. Physical deployment still needs compatible
  manufacturing, trained operators, infrastructure and construction.
- Foreign records, licenses, artifacts and observation may reduce uncertainty or native RP work, but
  acquisition, translation, verification, adaptation, reproduction and training retain their real
  financial and facility costs.
- Failed experiments consume the resources actually used and create evidence, revised estimates or
  safety consequences. Do not erase all RP progress or charge a random cash penalty with no event.
- Basic-science dead ends may still improve field competence or expose another possibility. Applied
  prototype failures can damage test assets, delay milestones or require redesign according to the
  tested system and preparation.
- Research collaboration divides real work and costs according to treaty commitments, contributed
  labs, facilities, experts, data and funding. A passive funder does not receive mature operational
  capability automatically.

Player presentation:

- Every visible research card shows RP required and completed, assigned and required labs, estimated
  completion, authorization cost, recurring cost per period, milestone commitments, required physical
  inputs and facilities, active currency and the current limiting factor.
- Before starting a project, show an expected cost range, current affordable runway and the effects of
  each funding posture. Identify whether the project is RP-limited, funding-limited, facility-limited,
  workforce-limited or waiting on a physical experiment.
- Add a Research line to the Economy page that reconciles authorization, recurring operations,
  milestone spending and collaboration payments by project. Selecting a line opens that program.
- Notify the player before funding exhaustion, a major test commitment or a material estimate change.
  Pausing or cancelling uses a direct decision card showing retained work, lost reservations and
  recoverable funds or materials.

Implementation order:

1. Add versioned research financial requirements and cost-component definitions alongside existing
   RP, lab, evidence, facility and applicability data without exposing hidden nodes.
2. Add per-project authorization, recurring burn, milestone commitments, funding posture and ledger
   state with save migration.
3. Bind progress to available funding, staff, facilities, power and physical inputs; remove any path
   that advances a directed project from RP alone when its required program cannot operate.
4. Connect prototypes and field trials to construction, shipbuilding, planetary sites, logistics and
   actual test assets where required.
5. Update Research and Economy pages, then calibrate Foundation through Frontier project portfolios
   against the realistic Human cost catalog and each civilization's local economy.
6. Teach fair-information AI to compare scientific value, urgency, total expected cost, budget runway
   and opportunity cost using the same project data and constraints.

Acceptance criteria:

- No directed research project completes unless both its RP work and every required funded milestone
  are satisfied; paying money alone never grants knowledge and RP alone never pays program expenses.
- Advanced and Frontier portfolios have higher median total cost than Foundation and Developing work,
  while every individual exception identifies the physical or intellectual reason for its scale.
- Starting, accelerating, pausing, resuming and cancelling research reconcile exactly with treasury,
  commitments, labs, workforce, materials, progress and save/load state.
- Adding labs raises both useful RP rate and operating cost under the documented diminishing-return
  curve and cannot create free research by repeatedly reallocating capacity.
- Foreign knowledge and collaboration reduce only the work they genuinely provide and never bypass
  adaptation, reproduction, facility, manufacturing or financial requirements.
- Player and AI civilizations pay the same research requirements in their own active currencies,
  with no hidden difficulty funding or free high-tier technology.

## Immediate roadmap — Civilization currencies and Credits

Status: phase 1 implemented; phases 2–5 remain planned. The simulation retains normalized
treasury values for balance and save compatibility, while Player-facing domestic screens and
command results format those values as the civilization's sovereign currency. The rejected
prototype Credit label and `$10M per Credit` bridge are no longer presented to players.

Currency progression:

1. **Sovereign currency:** Each civilization begins with its own named currency, symbol and
   denomination. Humans begin with their contemporary national or chosen starting currency;
   nonhuman civilizations use culturally appropriate currencies. Economy, construction,
   upkeep, wages and domestic trade show only that civilization's currency.
2. **Interstellar exchange:** Contact and trade expose foreign currencies and exchange rates.
   Foreign denominations appear only where they are relevant, such as trade agreements,
   market conversion, diplomacy and financial intelligence. Ordinary domestic screens still
   show the local currency.
3. **Credit development:** Credits become visible only after the civilization can actually
   hold and use them through the required research, institutions, trade network or diplomatic
   agreement. Unlocking the concept without access to a Credit market is not sufficient.
4. **Transition:** Local currency and Credits coexist only while both are genuinely usable.
   The interface shows both balances on exchange and transition screens, provides the current
   conversion rate and clearly identifies which currency will pay a quoted cost.
5. **Credit adoption:** Once a civilization fully replaces its sovereign currency, convert
   balances, contracts, prices, upkeep, debts and queued costs using a recorded transition
   rate. Remove the obsolete currency from ordinary play screens while retaining it in
   historical records and old transaction details.

Credit definition and legacy migration:

- Define the future **Interstellar Credit** as a settlement and clearing currency issued or
  governed by a real interstellar financial institution, treaty network or sufficiently trusted
  market. It is a normal divisible unit of account, not a bundle representing millions of
  Dollars and not a score for government spending power.
- Give the Credit a published interstellar trade basket containing standardized delivered energy,
  refined materials, habitat consumables, transport service and other widely exchanged inputs.
  The basket provides a stability reference; actual exchange rates still respond to access,
  confidence, liquidity, reserves and economic conditions.
- Do not hard-code an intrinsic Dollar value for one Credit. When Humans first gain access, the
  game calculates and records a market quote such as `1 CR = $x.xx` from the Credit basket and
  both economies. That quote can change over time and differs for every issuing civilization's
  local currency.
- Keep the Credit denomination at an ordinary transactional scale and use K/M/B/T formatting for
  large projects. Interstellar ships and planetary programs should naturally cost millions or
  billions of Credits when their real inputs justify that scale.
- Migrate legacy prototype saves by converting each old Credit-denominated balance, price,
  commitment and cash flow to its recorded 2050 purchasing-power equivalent exactly once. For
  example, a legacy balance of 500 prototype units becomes a $5 billion Human-era balance for
  continuity; this does not establish any exchange rate for the future Interstellar Credit.
- Tag migrated money with denomination and migration version so it can never be multiplied again
  on a later load. Existing buildings, ships, queues and contracts retain equivalent purchasing
  power without preserving the rejected unit in Player mode.

Rules and safeguards:

- Currency is owned by a civilization or issuing institution; its name, symbol and formatting
  are presentation data separate from the underlying economic quantity.
- Never show local currency and Credits together merely because the simulation stores a
  compatibility Credit value. The player interface derives the visible denomination from the
  civilization's current monetary stage.
- Prohibit a fixed universal Dollar-to-Credit conversion. Exchange rates should reflect the
  issuing economy, monetary policy, trade access, stability and market conditions. Provide a
  trade-basket reference and readable historical chart, then allow bounded movement.
- Maintain a separate Credit conversion for every actively issued civilization currency.
  For example, the Terran Dollar, a foreign Union Mark and an alien Exchange Unit can each
  buy a different fraction of one Credit at the same moment. Civilizations of the same species
  still receive separate rates when they operate separate economies or issue separate money.
- Establish a currency's first rate from a comparable basket of real output and costs, such as
  energy, food or life support, industrial production, labor and transport. Species needs can
  change the basket, but a racial label alone must never arbitrarily make a currency stronger
  or weaker.
- Store authoritative rates against Credits as the common settlement reference and derive
  foreign-to-foreign quotes from those rates. This avoids maintaining contradictory conversion
  tables for every possible pair while still presenting direct local quotes to the player.
- Rates may move within readable bounds from production, reserves, trade balance, debt,
  stability, shortages, war, sanctions and market access. Use smoothing and update intervals
  so normal play cannot create meaningless second-to-second price flicker.
- Model exchange availability and liquidity as well as the numerical rate. Unknown, isolated,
  embargoed or collapsed currencies may have no trustworthy conversion; thin markets may add
  a larger fee or spread. The interface must say `no available exchange` rather than inventing
  a rate.
- A displayed conversion must include its direction and unit, for example `1 Credit = 4.20
  Terran Dollars`, plus any fee or spread before the player confirms a transaction.
- Player and AI civilizations follow the same adoption, exchange and settlement rules. A race
  may retain its own currency indefinitely if it can support foreign settlement or refuses
  Credit adoption, though this creates real trade friction rather than a hidden penalty.
- Currency replacement is a deliberate institutional transition, not a UI rename. Existing
  saves and active contracts must migrate without creating or destroying purchasing power.
- Developer mode may inspect canonical internal values and conversion calculations, while
  Player mode shows only currencies legitimately known and usable by that civilization.

Initial acceptance criteria:

- A new early Human campaign displays the selected Human currency and no Credit balance.
- No Player-mode screen describes one Credit as $10 million or treats Credits as a renamed
  strategic budget point. Human projects show full Dollar costs until real Credit access exists.
- Every nonhuman civilization can display its own currency without changing shared economy
  rules or duplicating the entire economy implementation.
- Credits do not appear anywhere in Player mode before they are usable.
- During transition, every price identifies its payment currency and conversions reconcile to
  the authoritative balance.
- Two civilizations with different issued currencies can hold different Credit rates, and
  converting through Credits produces a consistent direct quote after declared fees.
- Saving and reloading preserves every known rate, its last update time, availability and
  market spread without revealing rates the player has not legitimately discovered.
- A migrated legacy save preserves equivalent purchasing power exactly once, then follows the
  same local-currency and later Credit rules as a new campaign.
- After full adoption, the retired currency disappears from current economy panels without
  corrupting saves, contracts, queues or historical records.

## Immediate roadmap — Playable species identities and balance

Status: planned balance layer over the four implemented biological profiles. The physical
facts already in `SpeciesCatalog` remain the source of effects; the values below are initial
tuning targets and must be proven in full campaign simulation before being treated as final.

Design rules:

- Build advantages and disadvantages from biology, environment, infrastructure and history.
  Do not attach arbitrary universal research, industry, combat or income percentages to a
  species name.
- Give every playable species a distinct strategic opportunity, a meaningful operating cost
  and at least one environment where it excels. No species should be best across population,
  colonization, logistics, research and warfare at once.
- Keep biology separate from culture and government. Two civilizations of the same species
  can develop different economies, doctrines, institutions, currencies and research paths.
- Balance comparable outcomes over varied maps rather than making every starting number
  identical. Founding population, adapted infrastructure, reserves and ships should provide
  comparable early productive capacity and survival runway while preserving different needs.
- Show causes to the player. A tooltip should say that immersion infrastructure or thermal
  control creates a cost, rather than presenting an unexplained `racial penalty`.

Pressure tolerance, settlement cost and adapted lineages:

- Every species has an atmospheric-pressure profile measured in kPa with four readable bands:
  preferred, comfortable, marginally survivable and naturally lethal. High pressure reduces
  suitability progressively before the lethal threshold; it must not behave as a single
  habitable/uninhabitable switch.
- Use these implemented unadapted pressure ranges as the initial physical baseline:

  | Species | Preferred | Comfortable range | Natural survival range |
  | --- | ---: | ---: | ---: |
  | Terran Baseline | 101.3 kPa | 66.3–136.3 kPa | 16.3–186.3 kPa |
  | Pelagic High-Pressure | 350 kPa | 225–475 kPa | 50–650 kPa |
  | Compact High-Gravity | 160 kPa | 100–220 kPa | 20–300 kPa |
  | Cryogenic Hydrocarbon | 150 kPa | 85–215 kPa | 15–285 kPa |

- A population at its locally adapted preferred pressure receives the full baseline. Any
  meaningful departure from that preference creates a negative cost: small and manageable
  within the comfortable range, then increasingly severe across the wider natural survival
  range. Pressure mismatch creates explicit health, productivity,
  reproduction, mortality, medical and life-support costs. Beyond that range, unprotected
  settlement is lethal and requires a sealed pressure-controlled habitat; technology keeps
  the population alive but does not pretend its biology is naturally comfortable.
- Settlement viability considers pressure together with gravity, temperature, radiation,
  atmosphere, solvent and immersion. A planet is not simply `habitable`: the colony panel
  shows the limiting factor, expected support demand and projected demographic effect before
  settlement is confirmed.
- Short residence produces reversible acclimatization over years. Heritable changes require
  locally born populations and many generations. Default tuning begins preference movement
  around 12 generations and broader tolerance around 18 generations, modified by each
  species' developmental plasticity and multigenerational adaptability.
- Adaptation belongs to the local population cohort, not instantly to the entire species.
  Migrants retain their inherited range; locally born descendants gradually form a distinct
  adapted lineage. Movement and intermarriage between colonies blend lineages gradually using
  bounded cohorts rather than creating one record per individual.
- A mature high-pressure lineage can become naturally comfortable above its ancestor's range,
  receive lower support costs and improved health, reproduction and local operations there,
  and eventually open still higher-pressure settlement candidates within its biological
  ceiling. It may become less comfortable near the ancestral pressure, preventing adaptation
  from becoming a permanent universal bonus.
- Natural adaptation cannot cross a lethal gap, change biological solvent, create a breathable
  atmosphere or remove an immersion requirement. Pressure-controlled habitats, medicine and
  deliberate biological engineering can establish a survivable bridge, but engineered change
  has its own research, time, risk and infrastructure requirements.
- When inherited divergence crosses maintained thresholds, present the population as a named
  derived lineage or subspecies of its parent species. It retains ancestry and shared identity
  while gaining its own pressure range, appearance variations, home-environment advantages and
  compatibility data. Reserve `hybrid species` for actual mixed ancestry where reproduction
  and xenobiology permit it; environmental descendants are adapted lineages.
- Keep natural expansion bounded. Using the current adaptation ceilings, initial fully matured
  high-pressure targets are approximately 233 kPa for Terrans, 779 kPa for Pelagics, 349 kPa
  for Compact High-Gravity populations and 326 kPa for Cryogenic populations. These are tuning
  ceilings, not immediate settlement limits, and require sustained viable residence across the
  lineage's full generational timescale.
- Persist lineage origin, founding date, residence duration, generations, local-born fraction,
  pressure preference shift and pressure-tolerance expansion. The UI should show progress as
  biological history and forecast ranges, never as a rapidly filling generic experience bar.
- Apply the same cohort adaptation rules to player and AI populations. AI settlement planning
  must price present support costs and future adaptation potential without knowing hidden
  planet data.

Low-gravity deconditioning and combat consequences:

- Track short-term physical conditioning separately from inherited gravity adaptation.
  Adults living below their adapted gravity gradually lose bone strength, muscle capacity,
  cardiovascular tolerance and ability to carry heavy equipment unless the colony pays for
  exercise, medicine or artificial-gravity facilities. Much of this adult deconditioning can
  recover after return and rehabilitation.
- Locally born populations developing across many low-gravity generations gradually shift
  toward lighter frames and lower musculoskeletal robustness. This inherited change belongs
  to the local cohort and can eventually form a named low-gravity lineage; it must not weaken
  every remote population of the parent species.
- Feed actual gravity suitability, current conditioning, body mass and musculoskeletal
  robustness into personal-combat and local-operations calculations. Low-gravity populations
  should have reduced load carrying, recoil control, close-combat force, injury resistance and
  endurance when fighting in stronger gravity, making unassisted infantry and boarding duty a
  poor fit over time.
- Keep the result environment-relative. A low-gravity lineage understands movement in its own
  habitat and may maneuver effectively there, while a heavy-world opponent also faces altered
  traction and movement. Do not turn either outcome into a universal racial combat percentage.
- Show the distinction between civilian habitability and military suitability. A world can
  support a healthy low-gravity-adapted population while that population remains poorly suited
  to high-load personal combat, planetary assault or work on a stronger-gravity world.
- Provide costly countermeasures rather than immunity: resistance training, centrifuge
  habitats, pharmaceuticals, developmental gravity programs, powered armor and exoskeletons.
  These consume space, energy, maintenance, medical capacity and equipment, and can reduce or
  compensate for weakness only while supplied and operational.
- Allow player and AI forces to recruit from suitable populations, use mixed-species units or
  assign low-gravity personnel to roles that depend less on raw physical force. Crew quality,
  doctrine, morale, weapons and tactics remain separate from biological capacity.
- Migration back toward stronger gravity creates a visible rehabilitation period, elevated
  injury/health risk and, for strongly inherited lineages, a long-term environmental support
  burden. Natural readaptation again requires viable residence and generations.
- Persist cohort conditioning and inherited gravity range through save/load. Keep the model
  bounded by aggregating nearby adaptation states and never tracking individual citizens.

Initial biological stat cards:

| Species | Metabolic demand | Lifespan | Relative demographic pace | Radiation tolerance | Adaptation responsiveness |
| --- | ---: | ---: | ---: | ---: | ---: |
| Terran Baseline | 1.00 | 82 years | 1.00 | 0.10 | 1.00 |
| Pelagic High-Pressure | 0.85 | 140 years | about 0.79 | 0.18 | 0.85 |
| Compact High-Gravity | 1.20 | 96 years | about 0.79 | 0.26 | 0.90 |
| Cryogenic Hydrocarbon | 0.22 | 360 years | about 0.31 | 0.38 | 0.35 |

The demographic pace is the current bounded result derived from generation length, maturity
age and reproductive-event throughput; it is not a free population-growth modifier. Final
growth still depends on health, capacity, living conditions, policy and resources.

### Terran Baseline

Advantages:

- Fastest acclimatization and multigenerational environmental adaptation of the initial four.
- Baseline demographic replacement and familiar carbon-water biosphere compatibility.
- Flexible upright workspaces and mature starting infrastructure in the authored Sol system.

Disadvantages:

- Lowest natural radiation tolerance.
- No natural dormancy and full baseline metabolic support demand.
- Poor unprotected performance in high gravity, extreme pressure, severe heat/cold and
  non-water biospheres.

Strategic identity: adaptable generalists with the easiest initial Human learning curve, but
no inherent immunity to hostile space environments.

### Pelagic High-Pressure

Advantages:

- Lower routine metabolic demand, long lifespan and four capable manipulators.
- Excellent natural operation in immersed, high-pressure water environments that are costly
  or inaccessible to terrestrial populations.
- Torpor can reduce demand during limited emergencies and long low-activity operations.

Disadvantages:

- Requires buoyant immersed workspaces; dry ships, stations and colonies need specialized
  life support and construction.
- Slower demographic replacement than Terrans.
- Low-pressure terrestrial environments and incompatible biospheres impose severe support
  burdens despite broadly carbon-water chemistry.

Strategic identity: efficient aquatic infrastructure and access to oceanic niches in exchange
for expensive operation outside them.

### Compact High-Gravity

Advantages:

- Highest musculoskeletal robustness of the initial four and strong performance on heavy
  worlds where other species need gravity mitigation.
- Better natural radiation resilience than Terrans or Pelagics.
- Dense horizontal body plan suits compact, high-load environments and physically demanding
  local operations when conditions match its biology.

Disadvantages:

- Highest routine metabolic demand of the initial four.
- Slower demographic replacement than Terrans.
- Low-gravity habitats and ordinary Terran-pressure environments require adapted workspace,
  health support or gravity systems; robust biology does not grant a universal combat bonus.

Strategic identity: capable heavy-world operators whose population and fleets are expensive
to sustain away from appropriately engineered environments.

### Cryogenic Hydrocarbon

Advantages:

- Extremely low routine metabolic demand, longest lifespan and highest natural radiation
  tolerance of the initial four.
- Natural deep dormancy can reduce biological demand to 8% for up to roughly 180 days, making
  carefully planned long-duration missions unusually efficient.
- Can exploit cryogenic hydrocarbon environments that are extremely hostile to water-based
  species.

Disadvantages:

- By far the slowest demographic replacement: approximately 31% of the Terran baseline pace,
  with maturity around age 55 and very long recovery from population loss.
- Lowest adaptation responsiveness and narrow compatibility with cold reducing-atmosphere,
  hydrocarbon-solvent environments.
- Warm carbon-water worlds, shared habitats and conventional allied infrastructure require
  extensive thermal isolation, sealed biospheres and specialized industry.

Strategic identity: patient, resilient and logistically efficient in its native conditions,
but exceptionally vulnerable to demographic losses and costly environmental incompatibility.

Starting-equivalence rules:

- Every species starts with one viable homeworld, two viable nearby expansion candidates under
  the Standard setup, adapted home infrastructure and ships that can support its own biology.
- Compare useful output and reserve duration rather than raw population counts. A species with
  heavier biological demand may begin with more support capacity; a slow-growing species may
  begin with a stable mature population, but must still bear the long-term cost of casualties.
- Starting differences may change building types, habitat volume, workforce organization and
  resource mix. They must not secretly grant free upkeep, impossible technology or recurring
  resources after play begins.
- Species selection presents clear strengths, constraints, preferred environments and an
  estimated complexity level without a misleading single overall power score.

Balance validation before enabling all four player starts:

- Run deterministic campaign batches across representative barred-spiral seeds, homeworld
  environments and neighboring-system layouts using the same AI planning quality.
- Measure 5-, 10-, 20- and 50-year survival, economic output, support burden, population,
  research capacity, exploration reach, colonization opportunities, fleet readiness and
  recovery from equivalent disasters.
- Run mirrored one-on-one and four-way AI campaigns. On neutral mirrored starts, no species
  should sustain a win rate outside 45–55% without an explainable map interaction; across the
  full varied-map suite, investigate any result outside 40–60%.
- Test each species in favorable, average and hostile regions. Its favorable environment
  should feel valuable, while hostile starts remain playable through visible engineering and
  strategy rather than hidden compensation.
- Validate every pressure boundary just below, at and just above comfortable, survivable and
  fully adapted limits. Confirm that marginal colonies pay real costs, lethal exposure cannot
  pass as natural settlement, and adaptation cannot advance without viable sustained
  population residence and new generations.
- Run long migrations in both directions to confirm a high-pressure lineage gains a local
  advantage, retains a meaningful ancestral-pressure tradeoff and never rewrites the immutable
  base species or every remote population.
- Run low-gravity residence, return-migration and combat-readiness benchmarks over months,
  years and generations. Verify reversible deconditioning, inherited lineage divergence,
  countermeasure operating costs and severe high-gravity infantry limitations without making
  a peaceful low-gravity colony nonviable.
- Stress the Cryogenic profile specifically for runaway low-upkeep expansion and stress the
  Compact profile for excessive support costs. Tune causal inputs, infrastructure and starting
  capacity before considering any narrow explicit modifier.
- Re-run the full balance suite whenever physiology, population, logistics, habitability,
  surface construction, ship support or starting-generation rules change.

## Immediate roadmap — Playable-game completion gaps

Status: required integration program for the first coherent 100-system Sandbox release. The
project has enough feature concepts; the priority is now to connect them into an understandable
opening, middle and first interstellar arc. New content does not take priority over completing and
validating these player loops.

### 1. Complete opening progression

- Begin on the selected species' developed homeworld with a stable but constrained economy,
  understandable shortages or opportunities and direct map actions.
- **Implemented foundation:** Sandbox setup selects any maintained species with its portrait;
  Humans always occupy Earth/Sol, while a selected nonhuman Player begins on its own naturally
  viable generated homeworld. The selection persists in generation metadata.
- Guide the player through stabilizing food, water, power, employment and public finances;
  developing the home system; discovering practical FTL; building and supplying an expedition;
  surveying nearby space; first contact; and founding an extrasolar colony.
- Present a small current objective, its reason and its direct action without forcing the player
  through instruction menus. Preserve free Sandbox play and allow guidance to be dismissed.
- Tune pacing so meaningful choices begin immediately while travel, research and construction retain
  believable time and cost.

### 2. Replace the placeholder economy

- Implement the labor-backed production, sustainable population, operational realism and currency
  roadmaps above as the authoritative economy rather than leaving passive prototype counters active.
- Seed every start with explicit facilities, workers, supplies, routes, tax bases and obligations.
  Reconcile all balances and remove invisible income or output sources.
- Make bankruptcy, shortage and stalled production recoverable through clear priorities, shutdowns,
  trade, taxation, financing or scaled-back expansion.
- **Implemented foundation:** base operating costs that cannot be paid now accrue as persistent
  arrears, future income repays them before rebuilding reserves, and the Economy page exposes both
  arrears and current payment coverage. Prolonged-arrears degradation and explicit priority controls
  remain required.
- **Implemented recovery control:** completed surface complexes can be shut down and restarted
  without demolition. Shutdown suspends their labor, power, output, district contribution and
  upkeep, with operating state preserved in saves.

### 3. Strategic AI under shared rules

- Make AI civilizations budget, build, staff, power, maintain, research, explore, trade, colonize,
  defend and recover using the same commands, information and resources available to players.
- Let difficulty change planning horizon, forecasting quality, coordination, risk tolerance and
  mistake rate. Never grant hidden production, knowledge, range, money or survival exemptions.
- Teach AI to retain reserves, respect workforce and logistics limits, abandon uneconomic projects
  and avoid expansion that predictably collapses its economy.
- Record concise developer-mode reasons for major AI decisions and validate deterministic planning.

### 4. Fleet operations and logistics

- Give routes and orders visible destination, departure requirements, travel time, operational
  range, cargo, passengers, fuel or reaction mass, crew, condition, risk and resupply plan.
- Support survey, transport, construction support, colonization, patrol, escort, interception,
  retreat and repair missions through direct map selection and previews.
- Add scheduled transport routes and sensible automation for repeated cargo or passenger movement;
  notify the player when a route cannot meet demand rather than requiring constant manual orders.
- Keep fleet state, reservations, cargo and mission progress authoritative through save/load.

### 5. Adaptive Research gameplay cutover

- Remove remaining fixed prototype research progression once the maintained Adaptive Research
  runtime, effects, facilities and persistence are connected end to end.
- Make laboratories, qualified staff, equipment, evidence, Research Pressure, applicability and
  visible possibilities determine the legitimate research horizon.
- Ensure completed work unlocks concrete designs, construction methods, operations or capabilities;
  research must not end as an isolated progress notification.
- Give the Research page direct lab allocation, competing-project consequences, readiness and
  missing-evidence explanations while preserving hidden possibilities and fair-information AI.

### 6. Meaningful colonization

- Require a suitable destination, surveys, transport, founding population, supplies, technology,
  access rights and a viable landing or orbital-support plan.
- Begin new colonies as dependent settlements with limited reserves, labor and services. Expose
  their path through command-center upgrades, local support, self-sufficiency, specialization and
  mature settlement without guaranteeing that every site succeeds.
- Let colonization fail or be evacuated through traceable shortages, environmental mismatch,
  conflict or poor planning. Preserve surviving infrastructure and population outcomes.
- Apply the same founding, support and growth rules to AI colonies and harsh-world outposts.

### 7. Consequential diplomacy

- Connect first contact, communication quality, borders, claims, transit, trade, research exchange,
  treaties, trust, threats and war to actual map, economic and military permissions.
- Let biology, culture, government, history and current interests shape diplomacy without making
  any species automatically friendly or permanently hostile.
- Give proposals explicit terms, duration, obligations and known consequences. Breaches change
  trust, access and third-party reactions rather than only adding flavor text.
- Keep unknown identity, capability, relationships and intentions protected by observer knowledge.

### 8. Complete combat loop

- Connect detection, pursuit or interception, engagement rules, tactical resolution, retreat,
  damage, casualties, capture where supported, repair, resupply and political aftermath.
- Make ship design, sensors, range, weapons, protection, readiness, crew and logistics affect results
  for explainable physical reasons.
- Present readable battle motion, warnings, selectable orders and an outcome report that identifies
  losses, damage, ammunition or supply use, experience and strategic consequences.
- Ensure warfare damages budgets, trade, infrastructure, populations and diplomacy and cannot exist
  as an isolated battle screen.

### 9. Objectives, success and failure

- Keep Sandbox open-ended while offering optional milestone tracks such as stable off-world
  self-sufficiency, practical FTL, first contact, an extrasolar colony, crisis survival, scientific
  discovery, economic influence or major-power status.
- Record progress and campaign history without forcing one play style. Allow players to continue
  after achieving a milestone.
- Define recoverable local failures: abandoned colonies, lost fleets, defaults, unrest, fragmented
  states and political defeat. Reserve total campaign loss for the actual end of the playable
  civilization or a user-selected stricter rule.

### 10. Information management and automation

- Add fast search and filters for systems, celestial bodies, fleets, colonies, routes, projects,
  contacts and known resources.
- Provide overlays for survey knowledge, habitability by selected population, resources, supply,
  ownership and claims, trade, hazards and military threat while protecting fog of war.
- Classify notifications by urgency, group repeated events, retain history and offer direct `View`
  and `Resolve` actions. Pause only for player-selected critical categories.
- Automate mature transport, maintenance, routine construction and colony priorities within limits
  chosen by the player. Surface exceptions and shortages instead of demanding repetitive clicks.

### 11. Contextual onboarding

- Teach the game through the real Player-mode interface and ordinary simulation rules. Use optional
  contextual guidance, highlighted objects and short explanations tied to the current objective.
- Every unavailable action names the blocking requirement and links to its location or remedy.
- Provide a guided Human/Sol opening, concise concept reference and searchable help. Never require
  the tutorial to understand a hidden control or undocumented rule.
- Validate onboarding with new-player observation before shortening or expanding it.

### 12. Sound and responsive game feel

- Add a coherent score, ambient layers and restrained interface, engine, construction, survey,
  launch, arrival, colony and combat sounds with independent volume controls.
- Give every accepted order immediate visual and audible acknowledgement, persistent status and a
  satisfying completion event. Reject invalid actions with a precise reason.
- Use purposeful movement, lighting, particles, traffic and camera transitions tied to authoritative
  activity. Keep effects interruptible, performance bounded and compatible with reduced motion.

### 13. Settings and accessibility

- Support remappable controls, UI scaling, readable type sizes, color-vision-safe status cues,
  reduced motion and flashes, subtitles or text equivalents, graphics quality, audio mixing,
  autosave frequency and notification controls.
- Provide mouse-accessible alternatives for middle-button gestures and optional keyboard shortcuts
  for experienced players. Core actions must remain available without precision dragging.
- Preserve settings separately from campaign saves and validate common resolutions, window modes,
  input devices and high-DPI displays.

### 14. Performance, saves and release stability

- Schedule distant simulation work at appropriate intervals and use bounded aggregates so a
  100-system campaign remains responsive through long play without changing authoritative results.
- Establish frame-time and simulation-step budgets for maps, surfaces, AI, economy, research,
  logistics and effects. Developer mode identifies the subsystem causing overruns.
- Maintain versioned manual saves, autosaves, atomic writes, backup recovery and one-time migrations.
  Failed loads explain the problem without corrupting the original file.
- Require clean Windows packaging, startup, new-game, save/reload, extended simulation, input,
  graphics-tier and crash-diagnostic gates before release.

### 15. Content depth after systems work

- Expand buildings, ships, technologies, events, leaders, species, anomalies, environments and art
  only through working systems that give each item a distinct use, cost and consequence.
- Define content in validated data where possible, with stable identifiers and save compatibility.
  Avoid near-duplicate entries whose only distinction is a small percentage bonus.
- Finish the first representative set for each core loop before multiplying variants. Use playtest
  evidence to identify where more options improve decisions or pacing.

Required implementation sequence:

1. Complete labor, food, water, power, taxation and operating expenses. Food/water carrying
   capacity, shortage decline, visible support limits and opening-fleet affordability are implemented;
   a first local surface-workforce pool, deterministic staffing and housing capacity are implemented;
   local food/water reserves are implemented; occupational allocation, inter-colony supply and
   broader physical material reserves remain next.
2. Complete physical production, staged construction, maintenance and freight.
3. Cut Adaptive Research over to real facilities, evidence, capacity and gameplay unlocks.
4. Complete fleet movement, supply, exploration and colonization.
5. Make strategic AI operate and recover through all four systems under shared rules.
6. Complete first contact, consequential diplomacy and the initial combat loop.
7. Add optional objectives, contextual onboarding, information tools and bounded automation.
8. Finish sound, visual response, accessibility, performance and packaged-release validation.
9. Expand content only after the representative end-to-end loop passes sustained playtesting.

Playable-release acceptance criteria:

- A new player can start a 100-system Sandbox and reach the first extrasolar colony through visible
  map and page actions without developer controls or undocumented commands.
- Economy, research, fleets, colonization, diplomacy and combat exchange authoritative state and do
  not behave as disconnected prototypes.
- At least one normal AI civilization can pursue the same arc, respond to setbacks and interact with
  the player without hidden resources or knowledge.
- The campaign provides clear goals, warnings, recovery options and history while remaining
  open-ended after milestones are achieved.
- A representative long campaign remains responsive, saves and reloads exactly, survives expected
  interruptions and produces a supportable packaged Windows build.
- Additional content is blocked from the release branch when its required gameplay system lacks an
  end-to-end player path, AI behavior, persistence or validation.

## 0.0.x — Foundation / playable simulation prototype

Completed/ongoing foundations include:

- Godot 4 + C# project foundation.
- Simulation isolated from the Godot scene tree.
- Deterministic seeded procedural galaxy generation with quota-controlled archetypes.
- Continuous real-time simulation with pause and adjustable speeds.
- Sustainable-speed/backlog protection for late-game performance.
- Fair-information AI contract and civilization trait model.
- Authoritative per-civilization fog of war.
- Real-time fleet exploration and first contact.
- Colonies, population, basic economy, research, and construction.
- A focused 100-system playable campaign profile with Earth/Sol as the Human origin.
- A 2050 human multi-world opening with Earth, a 100,000-person Luna settlement and a
  250,000-person young Mars settlement under ordinary colony, logistics and surface rules.
- Credit-funded infrastructure, ships, settlement and surface construction plus visible
  colony/fleet operating costs and powered trade revenue.
- Direct Economy and department pages, owned-fleet location controls, and owned-colony
  orbital/surface access.
- A graphical observer-safe Research horizon showing completed, active and currently
  investigable nodes without rendering unknown possibilities.
- Construction, shipbuilding, strategic AI and campaign guidance consume Adaptive Research
  capabilities directly, so retired prototype flags cannot unlock player operations early.
- Adaptive campaigns preserve legacy save data without accumulating the retired Science
  currency; research growth comes from powered, finite Effective Research Labs.
- Industry uses visible physical reserve capacity derived from colony infrastructure and
  completed industrial/orbital projects; idle production is curtailed at the cap.
- Direct 3D surface-building selection, construction cancellation and demolition with
  authoritative ownership, production and partial-refund rules.
- In-place surface upgrades with visible credit/industry requirements, stronger output,
  higher upkeep, distinct 3D presentation and save/load continuity.
- Surface districts derived from matching completed complexes, with visible specialization
  progress and bounded science, industry, trade or energy bonuses.
- Environment-driven 3D colony palettes for temperate, frozen, hot, airless, oceanic,
  reducing-atmosphere and rocky worlds without changing placement physics.
- Player-built powered habitat complexes and closed-loop upgrades that reduce exact-world
  life-support costs, with a bounded 75% maximum reduction.
- Colony overview rows reconcile gross and post-infrastructure life-support costs and show
  local surface power, turning the Colonies page into a direct landing/building decision view.
- The home-system orbital map visually represents launch-complex and shipyard plans, status,
  and active progress using the authoritative construction state; each marker directly opens
  Industry operations.
- Optional asteroid extraction now requires Orbital Industry plus a completed Launch Complex,
  adds bounded industrial output, recurring orbital upkeep, and a connected logistics node.
- Bounded diagnostics, system-spec logging, performance logging, support-bundle export.
- Save format/versioning and migration foundation.
- Automated .NET + pinned-Godot headless validation.
- Machine-validated public Adaptive Research possibility data and RP/Pressure/Lab research-economy design foundation.

## 0.0.5 — Pre-Warp Dawn / 2050 opening

Implemented prototype foundation:

- New campaigns begin on January 1, 2050.
- Player and normal major AI civilizations begin pre-warp.
- Research progression into prototype faster-than-light capability.
- A small number of remote seeded old powers begin already spacefaring.
- Seeded old powers are initially non-expansionist and neutral unless provoked.
- Old powers remain hidden until legitimately detected.

Current design direction substantially deepens this phase beyond the first prototype:

- A human-like 2050 civilization begins with a permanent lunar presence and a young Mars
  colony. Substantial orbital infrastructure and its deeper construction choices remain next.
- Pre-warp gameplay grows through home-system settlement, outposts, orbital construction, resource extraction, logistics, life support, long-duration habitation, supply, and automation.
- Other species begin at a comparable broad era but can have radically different home-system infrastructure and technological history.
- The player-facing operational scale expands from homeworld/local space into the solar system and eventually nearby stars as reach increases.

## 0.0.6 — Construction-driven development

Current validated gameplay baseline on `main` as of 2026-09-07.

- Industry-funded construction projects.
- Planetary Research Network.
- Industrial Automation Program.
- Orbital Launch Complex.
- Orbital Shipyard.
- Warp Test Facility.
- Research can require completed infrastructure.
- Normal AI uses the same prerequisite framework.
- Save format v6 persists construction progress/completion.

The current construction/research list is a prototype and will evolve as the richer solar-system phase and Adaptive Research runtime are implemented.

## 0.0.7 — Physical shipbuilding

Status: paused/incomplete/unvalidated; see `PROJECT_STATE.md` before resuming.

Intended milestone:

- Prototype FTL unlocks ship designs rather than gifting ships.
- Orbital shipyards physically construct spacecraft.
- Initial interstellar roles: scout, science, colony.
- Ship production consumes real industry.
- Colony ships consume/reserve real population.
- Science ships gain a meaningful survey/anomaly role.
- AI and player follow the same core production/prerequisite rules.
- Shipyard queues survive save/load.

When implementation resumes, future ship/research prerequisites must be reconciled with the Adaptive Research capability/possibility model rather than hard-wiring the old prototype tech chain as the final architecture.

## Pre-demo solar-system expansion work

Before the first public demo is considered complete, the pre-warp/early-space phase should become a real game rather than a short technology timer.

Planned direction includes a manageable subset of:

- homeworld and orbital development
- lunar/moon settlements appropriate to the species
- planetary colonies such as a young Mars settlement for a human-like start
- asteroid/resource extraction
- outpost ships and supply nodes
- orbital yards and transport infrastructure
- realistic-ish travel times and transfer constraints without turning the game into orbital-mechanics software
- long-duration life support
- radiation protection
- gravity management
- species-relative planet gravity/habitability effects and long-term population adaptation foundations
- food independence / advanced fabrication / replication progression
- fleet operational endurance and resupply
- prototype FTL with short practical reach
- automation of mature home-system tasks as the player becomes interstellar

## Pre-demo Adaptive Research foundation

The first public demo does not need all public possibility nodes implemented, but it should demonstrate the real research architecture rather than the temporary fixed prototype chain.

Required direction:

- hidden universe-scale Technology Possibility Graph
- player sees only the civilization's currently known/plausible research tree
- branches can appear from need, basic science, observations, discoveries, warfare, environmental conditions, and foreign evidence
- Research Labs generate Research Points
- technologies have minimum lab requirements
- certain technologies require relevant Research Pressure thresholds before becoming available
- multiple projects can run simultaneously when enough unreserved lab capacity exists; no arbitrary fixed research-slot count
- player and AI use the same core research-capacity/availability rules
- only the active civilization-specific research horizon is materialized in runtime/save state

Public seed design data lives under `data/research/v1/`; canonical rules live in `ADAPTIVE_RESEARCH_SYSTEM.md` and `RESEARCH_ECONOMY.md`.

## 0.1.0 — First public playable-demo target

The first public demo should present a coherent civilization arc rather than a technology showcase.

Target experience:

- begin in 2050 as an early multi-world/pre-FTL civilization
- develop the home system
- make strategic construction and adaptive-research choices
- watch the visible research tree change as conditions/discoveries change
- achieve practical FTL
- build the first interstellar spacecraft
- explore legitimately through fog of war
- establish first contact
- colonize at least one extrasolar destination
- encounter meaningful diplomacy and sovereignty/border decisions
- construct basic military forces
- experience an initial combat/conflict loop
- save/load/recover a campaign

Public-demo polish should include:

- main menu and New Game flow
- proper player-facing panels replacing most keyboard-only prototype controls
- usable evolving research-tree / research-lab allocation UI
- clear tooltips/event notifications
- basic sound/visual polish
- tutorial/help sufficient for a new tester
- Windows packaged test build
- visible build/version information
- support-bundle export and diagnostics

Working playable-species scope for the first demo: roughly 3–4 deeply differentiated starts can be sufficient. Quality/depth matters more than species count.

## 0.1+ — Adaptive research / technology-divergence foundation

### Galactic-core expedition (future)

New barred-spiral Sandbox campaigns reserve the galaxy centre for a persisted supermassive-black-hole landmark outside the 100 ordinary systems. It is visible on the strategic map but has no system ID, lane, route, or current access action. A future late-game expedition must introduce its destination, hazards, research, travel and return rules together; it must not bypass ordinary navigation or survey requirements.

- Do not use one fully visible universal tree or one giant separately authored fixed tree per species.
- Similar strategic capabilities can come from different technological implementations.
- Each civilization materializes a changing visible tree from a broader hidden possibility graph.
- Biology, environment, resources, culture, history, need, warfare, observations, and discoveries influence which branches emerge.
- Basic science can expose possibilities without immediate practical pressure where appropriate.
- Research Pressure provides contextual availability/urgency without hidden underdog rubber-banding.
- Research Labs provide physical research capacity and determine natural simultaneous-project concurrency.
- Some civilizations may never independently discover FTL.
- Foreign technology can require evidence, analysis, adaptation, reverse engineering, and compatible manufacturing rather than instant unlocking.
- Some foreign technologies may be incompatible, dangerous, incomprehensible, or valuable mainly to third parties.
- Technology can become a diplomatic/economic commodity.
- Research state must remain bounded: static possibility data is shared; campaign saves keep only civilization-specific state.

A much deeper technology-market/licensing/brokerage/hybrid-research system is a strong candidate for a later expansion, but the base architecture must support divergence from the beginning.

## 0.2 — Living civilizations

- Government and leadership change over time.
- Cultural/political evolution.
- Civilizations can fracture, reform, merge, collapse, and create successor states.
- Historical memory influences diplomacy without forcing permanent hostility.
- Relationships and intelligence can fade when contact ends.
- Old relationships may decay from active diplomacy to historical record, cultural memory, and eventually rumor/legend.
- Species lifespan, archives, cultural tradition, government continuity, censorship, and historical significance influence what is remembered.
- Species/culture-specific attitudes toward borders, trade, expansion, surrender, and war.
- Survival-first strategic behavior by default, with explicit cultural exceptions.

## 0.3 — Emerging powers

- Major and minor pre-warp societies progress through technological stages at different rates.
- Adaptive Research allows their development paths to diverge from the player's rather than following a synchronized fixed ladder.
- Some may plateau without native FTL.
- Protection, exploitation, trade, technology assistance, and non-interference create persistent consequences.
- Former pre-warp civilizations can become allies, rivals, major powers, or emergent threats.
- Information quality and sensor sophistication determine whether civilizations can verify threats, bluffs, fleet estimates, and unusual technology.
- Already-spacefaring seeded old powers do not receive automatic expansion behavior simply because they are technologically advanced.

## 0.4 — Subjects, coercion, sovereignty, and asymmetric power

- Vassals, protectorates, tributaries, client states, and culturally distinct subject relationships.
- Political defeat does not automatically end the campaign.
- Subject civilizations can rebuild, negotiate autonomy, cooperate with other subjects, rebel, or break free.
- Coercive diplomacy depends on credibility, intelligence, culture, risk tolerance, and actual strategic position.
- Borders are political warnings/claims rather than physical force fields.
- Civilizations can violate access restrictions and accept resulting diplomatic/military consequences.
- Historical/legal claims influence legitimacy and diplomacy but are not mandatory permission tokens for conquest.
- Occupation, formal ownership, recognition, resistance, logistics, sanctions, and coalition reactions create the real cost of expansion.

## 0.5 — Emergent crises and great-power consequences

- Crises arise from simulation history rather than only scripted timers.
- Expansion, technological imbalance, economic concentration, ideology, civilizational collapse, and political domination can create galaxy-scale threats.
- Powerful empires may deliberately accept huge diplomatic/occupation/logistical consequences because they believe they can survive them.
- Rival civilizations may cooperate against a hegemon/common threat based on legitimate information and their own interests.
- Technological leaders can become complacent naturally, but are not forced to fall behind; observed rival progress can restart urgent research/arms races.
- Crisis resolution can include war, containment, diplomacy, regime change, fragmentation, accommodation, subject relationships, or internal collapse.

## 0.6 — Civilization ark megaproject

- Colossal generation ark requiring a civilization-scale industrial commitment and decades of construction.
- Intergalactic propulsion hardware is megastructure-scale and not a normal ship module.
- Ark is extraordinarily durable but extremely slow, poorly maneuverable, and not designed for conventional combat.
- Limited industrial/defensive lasers and electronic-warfare support; no capital-ship offensive loadout.
- Limited onboard construction for scout, science, and colony craft while anchored.
- Ark construction/operation consumes population, resources, industry, and strategic opportunity.
- Severe drive/core damage can create catastrophic system-scale consequences.
- Deliberate scuttling/core-breach capability becomes a high-stakes strategic/diplomatic tool with enormous cost and consequences.

## 0.7 — Intergalactic exodus

- First-generation intergalactic transit is effectively one-use: transit stresses destroy the specialized drive.
- The ark carries scientific knowledge but not a fully rebuilt industrial civilization.
- Arrival requires settlement and reconstruction before advanced technology can be manufactured at previous scale.
- Intergalactic voyages remain long enough for generational events aboard the ark.
- Returning to the original galaxy requires rebuilding intergalactic capability as another civilization-scale project.

## 0.8 — Persistent multi-galaxy history

- Departed galaxies autosave at departure.
- While the player is away, they advance through compressed strategic historical simulation.
- Outcomes are causally derived from economy, population, technology, logistics, alliances, wars, stability, leadership, expansion, and overextension.
- A former dominant power may conquer most of a galaxy, collapse, fracture into successors, or be replaced by a rising civilization.
- Returning players receive a historical summary and reconstructed current galaxy state.
- Mature lower-level administration can become increasingly automated/delegated so multi-galaxy scale does not become unmanageable micromanagement.

## 0.9 — Deep discovery framework

- Rare undocumented discoveries, artifacts, research chains, unusual technologies, lore, and emergent strategic consequences.
- Discovery chains interact with exploration, Adaptive Research, diplomacy, intelligence, trade, theft, and war rather than behaving as simple collectible checklists.
- Civilizations value unknown artifacts only according to what they legitimately know about them.
- Secret discoveries plug into the same research architecture without being enumerated in the public research catalog.
- Exact chains, triggers, probabilities, and rare AI outcomes are intentionally excluded from this public roadmap.

## 1.0 — Full release target

- Stable long-campaign simulation.
- Mature fair-information AI.
- Rich civilization evolution and diplomacy.
- Mature Adaptive Research with strongly divergent civilization-specific technological histories.
- Deeply differentiated playable species rather than shallow bonus variants.
- Working planning target around 12 major playable species if quality/depth can be maintained.
- Strong late-game performance on target hardware.
- Intergalactic progression and persistent historical continuity.
- Extensive procedural and handcrafted content.
- Mod-friendly data boundaries where secrecy/security constraints permit.
- Production support, crash recovery, diagnostics, accessibility, localization, Steam integration, achievements, and workshop/community planning.

## Development philosophy

- Realism-driven causes/consequences before arbitrary restrictions.
- Playable builds before feature sprawl.
- Fewer deep systems/species rather than many shallow ones.
- The late game should change the player's problems rather than simply inflate numbers.
- Older routine tasks become automatable as civilization scale grows.
- Fix severe player-reported bugs quickly and communicate clearly.
- Stable and experimental branches once Early Access begins.
- Diagnostics and player-provided saves/logs are first-class development inputs.
- Optimize from real measurements, especially long-running campaigns.
- Every major system needs a bounded-memory, cleanup, save-size, and late-game CPU strategy before it is considered architecturally mature.
- Research-catalog changes must pass machine validation for stable IDs/prerequisites/pressure references/cycles before merge.
