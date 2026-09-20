# Canonical Project State

## Current candidate — 0.1.6 Alpha galaxy presentation repair

The galaxy repair restores the fitted spiral overview frame for the 500-system
profile and preserves the catalogue astronomy contract: no measured coordinates or
distances change, discovered systems retain catalogue identifiers, unexplored systems
display `Unknown`, and existing saves do not restart. Regional zoom and unknown-system
privacy are retained. The build is clean; Quality passed 20/20, Godot smoke 39/39,
and Windows package checks 11/11. Native receipt `work/galaxy-repair-regional-final`
at `4483e4e4` exited 0 with empty stderr at 720p and 1080p, covering free zoom,
regional zoom ceiling 192, known-system entry threshold 18, and unknown privacy.
Nearby receipt at `36232526`
passed 500-system fit, pan, and picking. Final performance and hosted package
provenance remain separate release gates. Native performance receipt
`work/galaxy-repair-performance/performance.json` at source `4483e4e4` exited 0
with empty stderr. RTX 3080 Ti, 2560x1440, uncapped five-second samples across
12 views measured 83.22–645.19 FPS; galaxy measured 84.08 FPS, p95 12.80 ms,
maximum 15.66 ms, and surface measured 303.49 FPS, p95 4.06 ms, maximum 22.12 ms.
No universal 60 FPS claim is made.


## Released integration baseline — 0.1.5 Alpha, 500 nearby systems and species selection

The accepted base is integration `35e03cd5` (0.1.4, PR #317). The candidate branch
`work/milky-way-500` makes new Player campaigns use 500 real HYG catalogue systems
around Sol. It preserves three-dimensional catalogue distances for travel, fuel and
sensors, while fitting the projected map to the viewport. Existing 100-system saves
retain their positions and rules. Regional point stars and solar-system sun art remain.

This is a local Solar-neighborhood sample, ending about 40.72 light-years from Sol,
not an entire Milky Way reconstruction. It has 50 proper names and 450 catalogue
designations; the seed changes fictional planets and civilizations, not star positions.
The distant galactic centre does not appear as a local landmark.

The new-game setup includes a left portrait selector and a right-hand
species biography and authoritative statistics, before generation/loading begins.
See [the 0.1.5 handoff](handoffs/nearby-catalog-0.1.5.md) for scope, validation and
remaining release gates. Main remains reserved for separate approval.

The candidate selects the highest progressive Windows refresh rate at the current desktop
resolution when automatic mode is selected; manual caps and focus restoration remain.
At 2560x1440 on an RTX 3080 Ti, `work/performance-fresh-final` passed all 12 views at
83.5–605.4 FPS with p95 frame times of 1.91–13.12 ms. `work/performance-aged-final`
also passed all 12 at 114.0–322.8 FPS with p95 no higher than 11.69 ms. Native autosave
capture measured 15.24–17.18 ms for the cold first save and 0.57–1.39 ms for warm
main-thread snapshots; JSON and atomic worker writes stay detached from that capture.
Core 87, Simulation 72, Quality 20, Godot smoke 39 and Windows packaging 11 validations
passed at `6538660c`. The full native bug-hunt also completed with exit 0 and empty stderr
(35 captures, 160 checks and 467 pointer actions through 4K). Final refresh preview,
rollback, monitor restoration, and native minimize/restore checks also pass. Core 87,
MassiveCombat 17 and persistence 7 were rerun at `2c77a476`. The final 100,000-vessel
diagnostic reduced evidence allocations from 733.4 MB to 203.9 MB and its frame outlier
to 79.97 ms (p95 18.00 ms); extreme combat still falls short of a 60 FPS frame budget.
Fresh CI and package provenance remain pending. These receipts do not establish
universal 60 FPS or a final release.

## Previous regional map checkpoint — 0.1.4 Alpha

The accepted base is 0.1.2 Alpha, integration `65ede790` (PR #316). It includes free
2D system zoom, orbital sizing/clearance, Pluto, British scientist voice routing, and
single silent startup loading followed by main-menu music.

The 0.1.4 candidate uses luminous point stars with colored halos and fine flare rays
in the main regional map, keeping detailed solar-system suns unchanged. It extends free
cursor-anchored zoom to 48, and carries a wheel approach into a known star's detailed
photosphere without requiring prior selection. Distant galaxies now appear only at
galaxy overview; local stars, clusters and nebula replace them regionally. Rendering
resources are cached and bounded. Explicit system entry still opens the 2D orbital map.
The 0.1.3 preview's resolved regional discs were superseded before integration.
See [the 0.1.4 handoff](handoffs/regional-map-0.1.4.md) and its PR for final validation
and package provenance. Main remains reserved for separate release approval.

## Historical 0.1.1 combat visuals checkpoint

The released 0.1.0 Alpha is accepted at `f9c57dbf8126b224124b2fa2d6329b908d005981`.
This stream carries observer-safe system-scene placement, procedural close-vessel geometry,
bounded cohort representatives, live-fire observation, and maintained 100,000-fleet
reconciliation. Release build and MassiveCombat 17/17 plus persistence 6/6 validations pass
locally. Local native acceptance has passed; hosted receipts remain pending for this branch.
The known visual limit is procedural Alpha-quality ship art and a measured 201.72 ms maximum
frame outlier.

## Historical alpha integration checkpoint — PR #314

The active alpha integration combines the visual expedition stream (`dd18ea96`), diplomacy
workspace (`4f368634`) and combat stream (`086c5c5f`) on foundation `7e84f29d`. Follow-up
repairs include exact research search, canonical lane-only arrows, cached territory triangle
meshes after a real Player-save renderer failure, and the requested dedicated loading art.
The current candidate is `work/alpha-playthrough-integration`; PR #314 holds the exact head.
The requested three loading artworks, live work progress and random tips are integrated;
Play/Pause and speed use separate controls. Native checks passed 35 rendered captures and
141 input checks at 720p through 4K, ordinary Player settlement/save/reload continuation,
and tactical menu/recovery. The dedicated 100k combat fixture passed 20 checks and six
images. Player validation uses an attributed opening plus a hash-verified saved continuation,
not an unbroken fresh-source run. Source-specific receipts are in the handoff.

Release CPU validation passed Simulation 71/71, CoreRuntime 84/84, Quality,
DiplomacyWorkspace, MassiveCombat and MassiveCombat.Persistence; Release builds have
0 warnings/errors. Final hosted gates/package provenance are recorded in PR #314 once
complete. Alpha review still includes pacing, presentation, dense tactical-label readability,
schematic close-up ships and broader diplomacy/combat outcomes. Main remains untouched.

See [`docs/handoffs/ALPHA_PLAYTHROUGH.md`](handoffs/ALPHA_PLAYTHROUGH.md) for the bounded
stream receipts and current handoff.

This is the authoritative continuity record for Stellar Continuum. `WORKSTREAMS.md` defines
branch ownership; Adaptive Research design/data merges do not promote gameplay VERSION.

## Historical integration checkpoint — published a19e5f6

Published PR #312 remains a draft at `a19e5f62944cc8bf1395771a2bb3052d95fbc45a`. Its four
hosted gates for build (`34585287234`), research (`34585287215`), voice (`34585287230`) and
Windows (`34585287238`) passed; the screenshot gate (`34585287157`) was still pending at the
last check. The full native run at `a19e5f6` stopped in `ScreenshotCapture.FleetOrders.cs:47`
after 24 screenshots because travel did not satisfy the unfinished-route assertion after its
20-frame wait. Do not claim that timing behavior is fixed until a later source change proves it.

Pure validation for source `d7460f9` is clean: CoreRuntime 79/79, Simulation 70/70, Quality
19/19, Logistics 4/4, Species checks passed and Python evidence checks 32/32, all exit 0. At
final source `a19e5f6`, the Debug build has 0 warnings/errors and Python checks are 36/36. The
fresh headless import and focused checkpoint both exited 0; the checkpoint reached 20 checks,
3 images, route-recovery completion and empty stderr. The focused startup UI proof from
`122f268` integrated by `d5dcc53` exits 1 as intended, and the late failure proof from
`69805fa` also exits 1 with the source save unchanged. These are source-specific receipts,
not a combined full-game acceptance.

## Current world-generation and startup investigations

Generation placement failures were reproduced with seeds `1789000000017` and `1789000000154`.
Separately, a save/load regression for seed `SOL-ASCENDANT-42` lost the physical conditions of
20 guarantee-altered bodies. The
complete catalog implementations for galaxy v16 and campaign v17, plus fresh-home fallback v2,
are implemented and reviewed; legacy worlds and old-save reconstruction remain preserved.

The focused startup and late-failure proofs are complete as described above. The combined
fresh Player journey, package verification, Hold/Return 383 native proof and human pacing/fun
review remain open.

## Current ordinary Player expedition milestone

The maintained pure case uses `CampaignSessionService.CreateNew("20260908")`, the canonical
100-system Barred Spiral bootstrap, Adaptive Research and diplomacy. It reaches a colony by
paid research, construction, physical ships, surveys and authoritative settlement. This is
simulation evidence only. The fresh native path still must prove the same progression through
visible Player controls, including save/reload with preserved people, ship identities,
simulation day and application revision. The next bounded step is to diagnose the FleetOrders
timing assertion, then rerun the affected native evidence at the resulting source tip.

## Remaining full-game acceptance work

The stages 1–6 objective remains active: 100-star playable foundation, realistic early economy,
core player loop, map UI, colony gameplay and controlled expansion. Implemented foundations
include paid research and production, labor and local life support, spatial mouse navigation,
surface construction, fleet travel and settlement. Remaining acceptance work includes:

- one continuous ordinary Player UI journey from opening through first extrasolar colony and save/recovery;
- clear civilian destination previews and recovery from mistaken orders;
- strategic AI recovery, physical support of dependent colonies and connected diplomacy/combat outcomes;
- sustained playtesting, accessibility, human pacing/fun review and production-quality visual/content review.

Use current code and published evidence when older handoffs conflict. Historical sections below
are retained as historical records and do not mark the full stages complete.

## Earlier full-game milestone — 2026-09-08/09

The user has replaced the demo-only goal with one full game and explicit Player /
Developer modes. Core PR #239 contains cinematic direction B, continuous map scale
navigation, 3D free-placement colonies, separate mode saves and a marked Developer
toolbox. See [GAME_MODES.md](GAME_MODES.md) and the Core handoff for current validation.
The starting baseline for this milestone is PR #233 / integration `334004d15c1f0cff7ee6dc345c8de225a2603de4`.
Acceptance, exact source revision and native validation results are recorded on PR #239; merging requires passing combined CI and native input validation.

The live economy now shows only the player's sovereign currency. Humans use the United
Earth Dollar and the three current nonhuman species use distinct named currencies and
denomination scales. The normalized treasury fields remain save-compatible implementation
values; no Player screen calls them Credits or shows the rejected `$10M per Credit` bridge.
The Interstellar Credit remains unavailable until its future clearing and adoption systems exist.
Civilian revenue is now labor-backed: the economy derives a working-age population, job
capacity and employed population for each colony, taxes employed people rather than every
resident, and exposes the employment rate on the Colonies page. Infrastructure and Industrial
Automation increase job capacity while food, water and housing continue to cap total population.
Staffed surface complexes add represented jobs to that civilian base without allowing total
employment to exceed the working-age population.
The Economy page now turns a negative cash-flow number into an actionable treasury state. It
shows exact reserve runway for a funded deficit and a depleted warning with available recovery
paths, while a sustainable economy is labeled as a surplus.
Unpaid base operating expenses now persist as authoritative arrears. New income pays arrears before
reserves grow, current operating payment coverage is visible, and save/load rejects negative or
nonfinite arrears and invalid funding fractions. This removes the previous free-operation hole at
an empty treasury. Fresh industrial and legacy science output now scales with the actually funded
share of current base operations, so insolvency cannot preserve full production. The interface labels
the finite stored Industry pool as Materials and explains its mine/fabricator/freight-to-construction
flow; internal names remain stable for saves and simulation APIs. The Economy page shows both
material storage/capacity and the current funded daily production rate beside the cash ledger.
Players can now shut down or restart completed surface buildings directly. Shutdown removes the
building's staffing, power, production, district bonus and upkeep, persists through save/load and
provides the first reversible austerity control for recovering from a deficit.
Surface complexes now carry persistent physical condition rather than operating forever after their
construction payment. Underfunded active facilities wear deterministically, reduced condition lowers
effective output, and a failed complex releases workers and power until repaired. Shutdown prevents
operating wear. The selected-building panel shows condition and efficiency and offers an exact
stored-Materials repair action; old saves initialize missing condition at 100%.
Deferred maintenance now reflects the exact occupied environment. Gravity departure, vacuum or
extreme pressure, severe temperature and radiation raise a bounded maintenance-exposure multiplier;
hostile-world buildings lose condition faster when operations are underfunded. Surface and Colonies
show the same authoritative multiplier, while full funding and shutdown retain their existing behavior.
The Colonies page also reports average condition, damaged-complex count and failed-complex count,
so the player can find a maintenance problem and land to repair it without already being on that surface.
Incomplete surface sites now identify their current physical phase in the 3D scene and selection bar:
preparation, foundations and utilities, primary structure, equipment installation, or commissioning.
Each view reports phase progress, overall progress and exact remaining Materials from the same
authoritative construction state; no additional mutable stage field is needed in saves.
Surface power allocation now has a safe automatic order. After explicit player Priority, the grid
protects generators, potable-water systems, controlled agriculture and habitat support before
research, fabrication and trade. Essential-service selection labels explain that protection, while
the existing player control can still deliberately override it for an emergency strategy.
Colonies can now build local Grid Battery Complexes. Each provides finite, persistent storage with
bounded charge/discharge power and explicit conversion loss; staffing, shutdown, physical condition
and upkeep use the same surface rules as other complexes. Economy and sustenance calculations use
the authoritative simulation interval, so stored energy cannot overstate trade, food, water or
housing through a large time step. Surface and Colonies views show GW flow and GWh state directly.
Bulk freighter handling is now physical and time-bounded. The maintained vessel transfers at most
20 material units per day while loading or unloading. A basic settlement hub provides 4 units/day;
a buildable powered Cargo Terminal adds 20 units/day up to the vessel limit. Partial cargo persists,
unfunded operations halt handling, and full civilization Industry storage leaves cargo aboard. Surface,
Colony and Fleet views expose port capacity and transfer state.
Surface building capacity now comes from a persistent administration center rather than a universal
limit. New colonies begin with a 16-module level-1 Command Center, ordinary homeworlds begin with a
32-module level-2 Planetary Hub, and the player can pay currency plus stored Materials to expand to
32 and then 64 modules. The landed view shows the level, exact capacity and upgrade cost, and the
central 3D structure gains additional towers, lighting and communications hardware at each level.
Older surface saves retain their former 64-module capacity through a backward-compatible default.
Level-2 expansion requires the completed Industrial Automation Program; level 3 requires the
authoritative Orbital Manufacturing research capability. Locked upgrades stay visible with their
exact blocker, so currency alone cannot bypass technological progression.
Planetary radius also constrains the final surface tier: bodies below 0.35 Earth radii stop at
32 modules and direct later development toward orbital infrastructure.
Surface authorization prices now vary by the exact occupied environment. Gravity departure,
vacuum, extreme pressure, temperature and radiation produce a bounded local construction factor;
Earth remains the 1.00× baseline while harsher worlds cost more. The build palette, upgrade action,
failure message and cancellation refund all use the same authoritative quote.
Hub expansions now apply that factor to both funding and Materials instead of retaining an
Earth-flat administrative price on hostile worlds.
Advanced surface power, fabrication, trade and habitat upgrades now consume live Adaptive Research
knowledge. Their commands revalidate the specific prerequisite and the selected-building control
shows the missing research instead of allowing an early treasury to buy late equipment. The advanced
science campus remains available in the opening research-capacity loop.
Surface shortages are now player-directed. Any completed building can be marked Priority from its
3D selection controls; prioritized buildings receive local workers and power before normal ones.
The setting is authoritative, visible in building status, persists through saves and rejects invalid
priority values, while Shut down remains the separate upkeep-saving control.
Completed-building labels now appear on selection instead of covering the settlement at all times.
Priority buildings carry an elevated amber ring, and shut-down structures show a red ground ring,
so their operational state remains readable directly in the 3D scene.
The terrain shader now combines its body-seeded large-scale palette with a source-quality generated
soil, grass and gravel albedo. Temperate worlds retain restrained natural color detail; alien classes
consume mainly the texture's luminance so their atmospheric and mineral palettes remain distinct.

Priorities are a playable ordinary campaign, meaningful economy expenses, deeper
colony decisions and the maintained Adaptive Research gameplay cutover. The active
Core continuation now uses a 100-system campaign profile and charges Credits for
infrastructure, ships, settlement expeditions and surface buildings. Colony services,
administration and active fleets create ongoing costs; powered surface trade hubs add
player-controlled revenue. The dedicated Economy page reconciles gross income, costs
and net flow against the authoritative simulation. `EARLY_ECONOMY.md` records the scale,
cost table and tuning basis. The current gameplay version is `0.0.7-dev.1`.
Player saves preserve versions 8/9, 10/11 and 12/13; Developer wraps the validated campaign
in a separate version 1 envelope. Earlier recovery details below are historical.

The New Game flow now continues from the graphical Story/Sandbox selector into a dedicated
Sandbox setup page. Players can randomize or enter numeric and memorable text seeds, see a live
seed-driven barred-spiral vector preview and the
resolved deterministic seed, copy the spoiler-free setup and restore the recommended fixed
100-system profile before confirmation. Saves retain the entered seed, internal seed, generator
version, selected Player species and option snapshot without rejecting older campaigns. The setup
now includes a portrait-backed selector for all four maintained species. Humans always retain Earth
in Sol; selecting a nonhuman civilization starts the Player on that species' naturally viable
homeworld while Humanity remains an AI civilization on Earth. The recommended setup now creates
a deterministic four-arm barred-spiral coordinate field with an elongated core and sparse outer
edge. At full-galaxy zoom, its 100 markers fill the galaxy presentation instead of occupying a
small box inside oversized art. Existing numeric callers retain the legacy disk profile. A
seeded vector detail layer keeps 420 arm/core lights sharp over the cinematic base, while 24
non-interactive distant galaxies add varied spiral, elliptical and edge-on shapes with depth
parallax around the playable galaxy. The balanced generator stores physical stellar class
separately from resources, anomalies and other content. Its exact 100-star quota matches the
roadmap, renders distinct stellar colors and sizes at both map levels, and drives physical survey
difficulty. Its planetary architecture includes exactly 18 planetless systems plus the agreed
sparse, medium, large and very-large groups; authored Sol keeps its eight planets. Fully surveyed
system intelligence names the primary physical star separately from system traits. The same
balanced profile now replaces `SYS-###` labels with 100 deterministic unique proper star names.
Generated planets use stable proper names and moons retain their named parent plus a stable
epithet. The dedicated naming seed stream prevents later resource or planet tuning from renaming
unrelated stars; legacy numeric campaigns retain their reconstructible designation scheme. Each
ordinary major civilization now has two reserved naturally viable expansion worlds within 340 map
units of home. The policy uses the authoritative species habitability evaluator, excludes all home
systems and unstable compact/hot stars, prevents guarantee overlap, and remains hidden until survey.
The interstellar-lane foundation creates a deterministic sparse connected graph with a
minimum-distance backbone and bounded local alternatives. The map draws a lane only after both
endpoints are known, shortest-route queries reach every system, and every starting system has at
least two links. Fully surveyed intelligence shows distance from home in ly and pc. Scout,
science, colony, outpost and military deployment orders now use the graph for authoritative
multi-leg travel. Exact ship designs set speed, maximum lane leg and fuel endurance; owned
colonies refuel fully and staffed resource outposts provide half-capacity service. Stable and
long-range propulsion research improves newly constructed ships while existing vessels keep
their launch performance. The map distinguishes lanes current ships can cross from lanes that
require better drives, and the fleet page shows exact route and fuel state.

A fully surveyed rare-resource world that is too harsh for colonization can now receive a
dedicated Sealed Resource Outpost Vessel. Construction reserves 8 million real specialist
personnel plus Industry and Credits; deployment charges its own authorization and follows the
same lane, range and fuel rules as other missions. Arrival creates a persistent staffed outpost
with no civilian tax income or automatic population growth, normal support/upkeep costs and
limited refueling. The Colonies page switches between normal colony and harsh-world outpost
planning based on the selected vessel. Completed powered Fabricators now process a finite,
body-scaled deposit into a bounded persistent local stockpile. The stockpile halts at capacity and stays outside
the civilization's usable Industry and cash until collected. The player can now construct a
100-unit Interstellar Bulk Freighter and dispatch it from a developed colony through the
outpost's Collect control. It follows authoritative lane, range and fuel rules, loads only the
stored amount, returns to its recorded home colony and delivers cargo as usable Industry there.
Cargo and both mission endpoints persist through a mid-run save.
New outposts record their initial reserve at founding; older saves resolve the same deterministic
value from the body's radius and mass. Extraction stops cleanly at depletion, remaining material
persists, and the Colonies and Surface views show reserve, storage and daily output together.
Surveyed sites identify a material family and bounded grade. Grade and environmental accessibility
modify actual processor yield, giving otherwise similar outposts different economic value without
creating hidden production.

Civilian population is now bounded by authoritative food and potable-water capacity instead of
growing forever. Natural capacity derives from exact-body area and species-relative environmental
fit, while sealed infrastructure provides a limited baseline. Powered Controlled Agriculture and
Water Reclamation add two billion people of category-specific support each. Growth slows toward
the lower capacity and shortages cause decline; both colony screens expose the limiting supply.
Opening tax and fleet-operation tuning now leaves a mature homeworld able to support its scout,
science and colony expedition while retaining every capital and recurring cost. Colony opportunity
cards also include the 120-Credit expedition authorization in `CanOrder`, preventing an apparently
valid action from failing only after selection. Harsh-world outpost cards apply the same rule to
their 90-Credit expedition authorization.

Housing is now a third authoritative population limit alongside food and potable water. Natural
settlement capacity uses the same exact-body area and species-relative environmental fit, while
sealed infrastructure provides the baseline. A powered Habitat Complex adds one billion housing
spaces and its closed-loop upgrade supplies three billion. The UI exposes housing and reports it
as the limiting shortage when appropriate.

Surface production now requires local workers. Each completed base or advanced complex has a
specific staffing demand, and 45% of represented local population supplies the initial operating
pool. Deterministic construction order assigns scarce staff; an unstaffed complex loses all output
but retains upkeep until population recovers. Both Surface and Colonies show used versus required
workforce and identify shortages. Occupational skill, wages and labor shared with orbital industry
remain later labor-model steps.

Food and potable water also have persistent local reserves. Surplus capacity fills a 30-day food
buffer and 7-day water buffer; production shortfalls drain those population-day stocks before
demographic decline begins. Every generated start and new settlement receives explicit initial
provisions, and Surface/Colonies show remaining reserve days. Save loading rejects negative or
non-finite reserve state and round-trip validation preserves exact quantities.

The active Core continuation now funds every directed Adaptive Research program from the
authoritative civilization treasury. A shared Player/AI campaign command charges a one-time
complexity-scaled authorization cost only after the research authority accepts the project and
requires enough cash for its first operating day. Complexity-scaled operating cost rises from Foundation
through Frontier work, assigned labs increase both RP throughput and daily spending, and a
partially funded program advances by only its funded fraction. An empty treasury produces no
RP progress. Research cards show daily and estimated total cost plus live funding percentage;
the Economy page includes actual research spending in its reconciled operating-cost total.
Spending and funding state survive campaign save/load, and normal AI will not open a new
program it cannot authorize and fund for its first day. Research choices show setup, daily and
combined estimated costs plus treasury runway after ordinary income and costs. A program whose
current income covers its burn is labeled sustainable. The campaign emits a single visible warning
when a program becomes underfunded and a recovery event when full funding returns, without flooding
the notification feed every simulation step. New programs also reserve complexity-scaled prototype
and validation funding, consume it across the Demonstrated, Engineering and Mature boundaries, show
the remaining reserve, and preserve it through Adaptive Research campaign schema 2. Requirement-specific
physical test assets and inputs remain later roadmap work. Disproven hypotheses consume and close
their remaining experimental reserve so removed projects cannot leave orphaned financial state.
Visible research cards now name the catalog's physical facility capability for the current or
starting stage, making clear that reserved money does not create a missing laboratory or test site.
An active program card is also a direct mouse control: it can pause ordinary work to stop daily
spending and resume only when its blockers are clear and the treasury can fund the first resumed day.
Hypotheses awaiting scientific resolution cannot bypass that resolution through Resume.
The Economy page's Research line now reconciles current operating burn with authorization already
paid for active programs and their remaining reserved milestone balance. Both capital fields are
stored with the project funding record, and negative authorization values fail save validation.
The full validation sweep also repaired two stale research fixtures: the biochemistry validator
now expects the catalog's actual eight reference profiles, and the collaboration benchmark uses
the canonical 400 RP per effective lab-year with its corresponding 8,640 RP joint result. These
were baseline drift from earlier accepted catalog/rate changes; all research validators pass.

The operations interface has direct pages for Economy, Research, Industry, Ships,
Exploration, Colonies, Logistics and Relations. Ship and colony pages now list owned
fleets/worlds and link back to their map location or colony surface. Affordability is
shown before capital orders. These local Core milestones require a fresh exact-head
Godot render/input and Windows package gate before publication or integration acceptance.
Relations now exposes declaration of war for legitimately identified contacts. Armed
owned fleets expose integrity plus Engage, Hold, Defend and Retreat commands on the Ships
page. Engage keeps foreign identity out of presentation: the matched Combat runtime chooses
the first deterministic co-located target that passes its hostility and attack preview.
Rejected engagement attempts disclose no peaceful or hidden target identity.
Military fleets can deploy to the star selected on the strategic map. Core validates the
fleet, destination and operational reach, clears system-local tactical orders, and then uses
the existing authoritative strategic movement path to travel and arrive.
On colony surfaces, the player can click a rendered building to select it. Incomplete sites
can be cancelled for half of their authorization credits while spent Industry remains spent;
completed buildings can be demolished without a refund. Removal is ownership checked and
immediately updates local power and production.
Completed surface complexes now contribute type-specific maintenance to the same daily
cash-flow calculation shown on the Economy page, including when a completed complex lacks
power. This makes unused surface capacity an ongoing economic decision.
The surface header exposes ordinary 1×, 2×, 3× and 8× simulation speeds alongside pause,
so a player can manage construction pacing without leaving the planet view.
PR #241 merged the complete 100-system economy, operations and surface-management slice
into `integration` at `a7a1ab4096fb5ca706778942dbf5b30703c93d73` after exact-head build,
Windows package and real Godot input/render/save/reload gates passed. The active follow-up
adds a first in-place upgrade tier: completed complexes consume visible Credit and stored
Industry costs, switch to advanced output/upkeep, retain position, and persist without a
save-format change.
The Research page follow-up introduces a reusable graphical horizon that contains only
completed, active and currently investigable knowledge. Unknown possibilities are absent
from the presentation model. Players can start an investigable program directly from its
node; the legacy six-step progression remains authoritative until Adaptive Research state,
effects and persistence are cut over together.
Colony specialization is now derived from completed surface construction: three matching
complexes form a Research, Industrial, Commercial or Energy district with a 25% matching
output bonus. The surface and colony list show current specialization and progress. Because
this is derived from existing saved buildings, it adds a real placement decision without a
new save field or mode-specific rule.
The 3D colony scene now derives its terrain, exposed rock, sky, fog and sunlight palette
from the occupied world's canonical environment. Temperate, frozen, hot, airless, oceanic,
reducing-atmosphere and generic rocky worlds are visually distinct while sharing the same
authoritative placement heightfield and saved coordinates.
Fresh human campaigns now begin with Earth plus dependent settlements of 100,000 people on
Luna and 250,000 on Mars. Both appear on the Colonies page, can be opened in orbit or landed
on, use their actual body environments, survive ordinary saves, and add scaled administration
costs. Campaign completion now requires an extrasolar colony rather than merely a second owned
settlement.
Owned surfaces render their established population around the protected colony hub. Hostile
worlds such as Luna and Mars use sealed habitat modules; naturally supported colonies use open
settlement towers. The cluster changes only across bounded population bands and does not alter
construction footprints or saves.
Players can now place Habitat complexes on hostile colony surfaces. A powered base complex
reduces that colony's explicit life-support cost by 20%; its closed-loop arcology upgrade
reduces 40%. Reductions stack only to 75%, consume power, add maintenance, and use the same
construction, upgrade, demolition and save rules as other surface buildings.
The Colonies page reports each world's actual post-infrastructure life-support charge and,
when reduced, its gross cost and reduction. It also exposes local building count and power
demand/supply so the player can identify a power shortage before landing.
The home system now offers an optional Asteroid Resource Network after Orbital Industry and
the Launch Complex. It costs 320 Credits plus 1,800 Industry, produces 1.50 Industry/day,
costs 0.18 Credits/day to operate, and appears in both the orbital map and logistics graph.
Completed Launch Complexes, Shipyards, and Warp Test Facilities also carry explicit upkeep.
Locked orbital markers and rejected construction orders now identify their missing technology
and prerequisite infrastructure by name instead of returning a generic unavailable message.
The opening guide uses the direct Research and Industry pages rather than obsolete cycling
instructions and presents asteroid extraction as an optional output/upkeep tradeoff when the
next required infrastructure is still technology-locked.
The Research and Industry pages no longer render redundant Next/Start controls; named horizon
nodes and project buttons are the player-facing order path.
Ships follows the same direct interaction: named design buttons start or queue vessels, the idle
card asks for a choice instead of implying a default selection, and locked shipyards show no
inert cycle/build controls.
Locked shipyards and rejected ship orders name the missing Spacecraft Construction,
Experimental Interstellar Transit and Orbital Shipyard requirements directly.
The visible Research horizon now uses full-width two-column program cards with description,
state color, progress and a clear graphical start affordance instead of small text-like buttons.
The Colonies page now presents each owned world as a compact visual card with grouped population,
administration, life support, power and specialization status plus normal View and Land controls.
The Economy page now renders income and each operating-cost category as aligned labeled rows,
replacing tab-delimited text that could collapse names and values together at runtime.
Industry and Ships now render available orders as three-column graphical cards with separate
title, cost, description and authorization state while retaining their direct command paths.
The top bar now includes a persistent recent-events center. Accepted capital orders and
player-visible research, construction, ship, exploration, colony and combat outcomes remain
available after the temporary command message disappears. The feed is ordered, limited to
32 entries, displays the newest 16, and clears when changing campaign or game mode so events
cannot leak between Player and Developer sessions.
Exploration now presents owned expedition state as graphical fleet cards with role imagery,
phase, destination, ETA and authoritative mission summary. Before the player owns an expedition
fleet, the page shows a visual dispatch prompt tied to the map command bar instead of a plain
paragraph saying that no missions exist.
The Logistics page now visualizes the authoritative Sol supply network through four live flow
cards and individual infrastructure-node cards. Players can see offered supply, demand, delivered
flow and shortfall at a glance, then identify which homeworld, settlement, resource, orbital or
shipyard node is supplying cargo or going underserved.
Relations now presents each observer-visible contact as a graphical diplomatic dossier. Political
and communication state, trust, hostility, fear, respect, cooperation, transit rights, agreements,
pending proposals and recent events occupy distinct visual cards above the existing validated
diplomatic actions. An unknown galaxy begins with a clear first-contact state rather than raw text.
System Inspection now renders a survey intelligence dashboard with progress, fog-safe signal cards
for stellar region, habitability, anomalies, rare resources and pre-warp life, plus a distinct
settlement-intelligence card. Unsurveyed targets retain explicit guidance while unavailable facts
remain absent rather than inferred from authoritative hidden state.

## Current shared integration recovery — 2026-09-08

- Accepted shared baseline at recovery: `integration` / `c529a1a765776c0940f88002410bc70db740d05a`.
- Core branch safely synchronized from `6b50f879` (285 behind, no unique commits).
- Current gameplay VERSION is `0.0.7-dev.1`; campaign save format is v9. The earlier research-local v6/0.0.6 snapshot below is historical.
- Accepted seams include deterministic plain-C# stepping/Industry allocation, pause safety, physical shipbuilding, persisted Diplomacy, observer-safe command/read models, Exploration/Colonization and Species contracts, scheduled autosave/backup recovery/day-zero checkpoint, and exact-own Combat status.
- Adaptive Research M19 implementation is accepted side by side with legacy gameplay research. Canonical continuation is `research/adaptive-research`; a gameplay cutover has not been accepted.
- Runtime release blocker #61 was independently reproduced on the exact accepted build: seven logged C# script-instantiation errors were ignored by the old smoke command. Testing/Release PR #222 adds Debug build plus semantic startup/error checks. It remains a release blocker until changed CI passes.
- Existing PR #218/#220 visual milestones and recovered unique children require review, not automatic merging. The 1,000 real-system data milestone #221 is recorded and remains a separate uncompleted dependency.
- Full branch classification and sources: [BRANCH_INVENTORY_2026-09-08.md](BRANCH_INVENTORY_2026-09-08.md). Current ownership and flow: [WORKSTREAMS.md](WORKSTREAMS.md).

## Retained earlier research-local snapshot

The sections below preserve their original milestone context. Use current shared recovery records and specialist handoffs for present branch/validation status; do not act on stale main-first directions.

## Repository / identity

- Working title: **Stellar Continuum**
- Repository: **`afterburn25/stellar-continuum`** (public)
- Engine/runtime: Godot 4.7.2 .NET, C# / .NET 8
- Architecture: Godot presentation/platform layer + plain-C# authoritative simulation core

## Gameplay baseline known to Adaptive Research

As of 2026-09-07 this workstream still records:

- gameplay version: **`0.0.6-dev.1`**
- validated gameplay merge: `91a2204b96ed08c2178875cbc8d5b0bc378372ad`
- save format: v6

Other gameplay workstreams own later gameplay status if changed.

## Adaptive Research workstream

- persistent branch: **`dev/adaptive-research`**
- owner: dedicated Adaptive Research / Technology chat
- scope: possibility graph, RP/Pressure/Labs, emergence/evidence/applicability, capability/maturation, competence/facilities/tacit knowledge, foreign technology/exchange, starting histories/runtime/view contracts, agenda/scientific culture/fair AI, long-run benchmarks, biochemical diversity, distributed scientific continuity, secrecy/compartmentalization, cross-polity scientific collaboration, research-facing integration/UI/validation

Other workstreams may consume research events/queries/capabilities but should not independently edit canonical research graph/schema files without coordination.

## Current public Adaptive Research seed

- **360 public/normal possibility nodes**
- **21 domains**
- **59 Research Pressure types**
- **16 alternative-solution sets**
- **14 applicability traits**
- **9 evidence types**
- **36 knowledge fields**
- **17 cross-lineage capabilities**

Exact secret discoveries, artifact chains, rare probabilities, and hidden special-AI eligibility remain outside public data.

## Durable research rules

- one shared hidden Technology Possibility Graph; never one fully visible universal tree or giant fixed per-species trees
- player and AI see only their current legitimate scientific horizon
- RP comes from physical Effective Research Labs; Pressure is contextual need/evidence and only a hard gate where explicitly configured
- early directed concurrency is 1 -> 2 -> 4 -> lab-capacity-only as real institutional capability matures
- functional dependencies use cross-lineage capabilities when implementation does not matter
- research knowledge is distinct from physical deployment
- field competence is theoretical / experimental / engineering; specialist facilities and tacit expertise matter
- Project Readiness is a bounded derived context efficiency, not stacks of arbitrary research modifiers
- foreign technology uses Understanding / Operability / Reproduction / Adaptation and never instantly matures a native node
- technology exchange is composed from actual records/data/hardware/tooling/experts/training/institutions; licenses are law, not physics; value is recipient-specific
- starting civilizations compose scientific history; future research remains adaptive and is not stored as a species tree
- agenda/scientific culture changes recognized attention and real capacity requests, not hidden-node visibility or direct RP multipliers
- fair AI uses the same visible horizon and authoritative blockers; harder AI gets planning quality, not hidden knowledge/free RP/labs/evidence
- runtime is sparse/event-index driven; no full graph per simulation tick and no per-frame hidden-graph UI query
- biochemical identity is composable population context, not a race ID or flat research modifier
- carbon-water is common reference, not universal; ammonia-rich, cryogenic-hydrocarbon, silicon/mineral, synthetic, and mixed populations share the same graph through applicability/capability context
- one civilization may contain multiple incompatible biochemical populations without duplicating the graph
- mature biochemical knowledge may be civilization-wide while operational applicability remains population/context scoped
- scientific truth/maturity, local codified access, local active practice, and physical deployment are distinct
- distributed research creates sparse context exceptions only when regions materially diverge; synchronized colonies have no explicit research-context object
- records/data propagate through actual communications; experts/tooling/prototypes/operating institutions do not teleport as data
- no universal distance research penalty; only actual communication/archive/institutional/political/practice constraints matter
- successor states inherit research from actual local archives, received records, experts, facilities/tooling/prototypes, training, and projects—not the old polity's full tech-list checkbox set
- classification applies to records/projects/assets, not scientific truth or physics
- classification does not directly change RP/project cost and cannot make deployed physical effects unobservable
- if secrecy slows research, the cause must be real reduced authorized labs/facilities/experts, validation limits, compartment integration, or secure-communications/archive constraints
- reclassification can stop future dissemination but cannot recall already distributed copies or un-leak compromised records
- Intelligence/Security owns espionage, theft, interception, compromise detection and protection actions; Adaptive Research owns research-side access/dissemination/foreign-tech consequences of factual outcomes
- scientific collaboration creates permission/coordination, never RP or research-speed multipliers
- every joint-research contribution references real participant capacity/assets; contributions may be unequal
- active joint directed research consumes each scientifically participating polity's own directed-program capacity
- all labs on one joint project use one canonical diminishing-return curve; multiple flags cannot create multiple scaling buckets
- joint projects do not merge technology trees or expose hidden partner nodes
- genuine scientific co-developers may advance through normal maturation; passive funders/hosts/result recipients are not automatically Mature
- equal shared records do not imply equal operability/reproduction; biology, facilities, materials, infrastructure and tacit practice remain participant-specific
- withdrawal removes future real contribution but cannot roll back completed work or recall delivered records
- Diplomacy owns agreement negotiation, payments, rights, treaty breach and political consequences; Adaptive Research owns scientific contribution/progress/result semantics
- secret/rare discovery details stay outside public research data

## Validated Adaptive Research milestones

1. PR #11 -> `f70e122134e87c1449582b573c5e2db8b045d311`: possibility graph + RP/Pressure/Labs.
2. PR #12 -> `95fa5c9e77642479eecc8f4183c91c06b3709f7e`: emergence/evidence/pressure dynamics.
3. PR #13 -> `101b01a1d6407fee2912c7e8b9175f196bb75ca9`: capability interoperability + maturation/hypotheses/setbacks.
4. PR #17 -> `64a4aaa74ca526c8d6d69b9d905a2e9c3e3a6bc6`: competence + specialist facilities + tacit knowledge + Project Readiness.
5. PR #30 -> `d7bdaa8ee67461ba1811121e9af4719de6583a8d`: foreign tech + transfer/licensing/brokerage + visible-only research UI.
6. PR #44 -> `859099ee3048a2788aa32b33c5ca46aeaee00df9`: composable starting histories + runtime event/query boundary + materialized view.
7. PR #53 -> `8e47ef537af6d35f8d60e9cf2c9953064d6858ef`: agenda + 12-axis mutable scientific culture + causal complacency/catch-up + fair AI planning.
8. PR #59 -> `d3916d3e6551c7a8b606716858d2d782581b1dac`: deterministic 500/1,000-year research divergence/state-soak benchmark harness.
9. PR #63 -> `1e69fa65c2ec0df2feef16bd30794b08f0b2000a`: 360-node species-neutral alternative biochemistry/exotic biospheres + multispecies applicability.
10. PR #73 -> `6c832fc07359ebb4fbe40c4dc079f19e13c11fca`: distributed scientific knowledge/regional continuity + successor inheritance/reintegration.
11. PR #78 -> `172c9c364b161e2e3deac88337429b5880a34e68`: research secrecy, special-access compartments, compromise interpretation, declassification/reclassification.
12. PR #83 -> **`b56b47c1f23312abde93e04cd8ac9caf819f0176`**: real-capacity cross-polity joint research, participant-specific contributions/results/withdrawal, classified collaboration, collaboration benchmarks.

None of these research design/data/tooling milestones promoted gameplay VERSION.

## Long-horizon benchmark baseline

### 500-year same-origin divergence

- minimum Mature-tree Jaccard distance: **0.457**
- Mature catalog fractions: orbital industrialist **0.253**, defense engineer **0.292**, biosphere adaptor **0.350**
- unique Mature nodes: **11 / 36 / 68**
- common Mature nodes: **47**

### 350-year military complacency/response

- initial hegemon lead: **10.85** benchmark units
- gap at year 180: **-12.65**
- leader attention **1.0 -> 3.0** after legitimate rival catch-up evidence
- final benchmark scores: leader **18.55**, challenger **33.40**
- no hidden catch-up multiplier, leader penalty, forced parity, or guaranteed comeback

### 1,000-year core research-state soak

- Generalist A: **89** node states / **124** recent events / **64** detailed history
- Generalist B: **105 / 156 / 80**
- Specialist C: **81 / 108 / 56**

## Biochemical applicability benchmark

- human carbon-water: **4 shared / 0 exotic native-specific**
- ammonia-rich: **10 / 6**
- cryogenic-hydrocarbon: **10 / 6**
- silicon/mineral: **12 / 8**
- exotic native-specific pairwise Jaccard distance: **1.000**

## Distributed-continuity benchmark

- communication: 1.75y latency; Absent -> Codified; no automatic practice
- 40y partition: theoretical loss 2, experimental loss 8–14, engineering loss 16–24
- successor fracture: shared foundation 10; A 15 total/5 specialist; B 14/4; Jaccard **0.474**
- 1000y / 120 regions: peak **5** contexts, **17** node-access exceptions, **10** field-practice exceptions, **9** pending transmissions; final contexts 3

## Secrecy benchmark

- restricted program: 48 scientifically eligible labs -> 14 authorized, direct secrecy multiplier **1.0**
- compartment integration blocked by missing manufacturing-process access until real authorization supplied
- classified capability observation: Understanding Unknown -> Observed, Reproduction None, records 0
- partial blueprint compromise: Characterized + Component Replication, native maturity false
- declassification: contexts 2 -> 6, archive copies 2 -> 6 after delivery, maturity unchanged
- reclassification: 4 existing copies remain 4 despite policy narrowing to 2 contexts
- 1000y secrecy soak peaks: **18** security records / **6** compartments / **3** known compromises

## Milestone #12 — cross-polity joint research / scientific collaboration

**Merged/validated through PR #83 at `b56b47c1f23312abde93e04cd8ac9caf819f0176`.**

Final validated PR head: `a6e34f9b53ef3874e4ce95faabd548fc455d4b3b`.

Established:

- five collaboration forms: Joint Directed Project / Shared Observation / Shared Facility / Expert Exchange / Joint Foreign Technology Study
- agreements create permission and contribution commitments but never RP/research-speed multipliers
- joint directed work consumes participant directed-program capacity when a participant actually performs directed research
- real labs/facilities/experts/data/evidence/samples/materials/tooling/computation only; same asset cannot be counted twice
- combined labs use one canonical project-wide diminishing-return curve
- no generic cross-polity coordination penalty; only actual communication/security/data/facility dependencies matter
- contribution and result rights are separate and may be unequal/asymmetric
- actual participation can build relevant field competence; passive treaty membership/payment/result receipt does not create practice
- genuine co-developers advance through normal maturation; passive participants use normal transfer/assimilation
- result applicability/operability/reproduction remains participant-specific
- classified joint projects can distribute only selected compartments while using an authorized integration context
- withdrawal removes real future capacity/assets but cannot reverse completed progress or recall delivered records
- stable collaboration runtime extension exposes **7 factual input events / 6 queries**
- collaboration state stores aggregate active contributions, references physical assets, compresses completed history, and never copies partner technology graphs

### Collaboration benchmark

Real labs/no treaty multiplier:
- A 20 labs -> **17.4** scaled units
- B 12 labs -> **12.0** scaled units alone
- correct combined 32-lab project -> **21.6** scaled units / **2160 RP/year before readiness**
- incorrect per-partner scaling would be **29.4** units and is explicitly rejected
- treaty multiplier **1.0**

Participant withdrawal:
- 24 labs -> **18.8** scaled units
- withdraw 8 labs -> 16 labs / **16.0** units
- capacity loss **2.8** units; stage progress preserved; delivered records not recalled

Hard facility withdrawal:
- `hazardous_foreign_tech_protocols` retains 10 labs but loses canonical `xenoscience_containment`
- project becomes `blocked_missing_specialized_facility`

Asymmetric foreign result:
- both participants Engineering Understood
- compatible participant: Adapted Operation / Subsystem Replication
- incompatible synthetic participant: Unusable / Component Replication
- native maturity false

Classified joint compartments:
- 4 compartments total
- A missing manufacturing process
- B missing theory + software/control
- designated integration context has all compartments; neither participant independently has complete package

Communication partition:
- 3-year data-link partition
- local observation continues
- cross-site correlation blocked pending remote dataset
- no generic penalty; existing local records preserved

1,000-year collaboration soak:
- **112** collaborations created
- peak **4** active collaborations
- peak **9** participant contribution records
- peak **4** pending result deliveries
- final active 2 / pending deliveries 0
- recent-event ring at bound 384; archived summaries 110/192

Final #12 validation passed collaboration + secrecy + distributed + biochemical + all core research validators/benchmarks and .NET restore/build. Exactly seven research-owned files changed.

## Known shared CI limitation — issue #61

The shared Godot runtime smoke can exit successfully while logging failure to instantiate `res://src/Game/Presentation/Main.cs`. Tracked in **GitHub issue #61** and outside Adaptive Research ownership.

Until fixed, do not claim semantic runtime health solely from that process step.

## Research validation stack

Core:

1. `validate_research_catalog.py`
2. `validate_research_maturation.py`
3. `validate_research_competence.py`
4. `validate_research_transfer_ui.py`
5. `validate_research_start_runtime.py`
6. `validate_research_agenda_ai.py`
7. `validate_research_benchmarks.py`
8. .NET restore/build
9. shared Godot process smokes with #61 caveat

Specialized:

10. `validate_research_biochemistry.py`
11. `validate_research_biochemistry_benchmarks.py`
12. `validate_research_distributed_continuity.py`
13. `validate_research_distributed_continuity_benchmarks.py`
14. `validate_research_secrecy.py`
15. `validate_research_secrecy_benchmarks.py`
16. `validate_research_collaboration.py`
17. `validate_research_collaboration_benchmarks.py`

## Campaign horizon / scalability target

- demo: ~100–150 meaningful years
- initial paid Early Access: ~**500 meaningful years**
- engineering soak: at least **1,000 simulated years** without unbounded state/save/performance growth
- no hard year-based game-over

## Next Adaptive Research action

**Milestone #13: plain-C# Adaptive Research runtime implementation foundation.**

Before editing shared simulation source, update `WORKSTREAMS.md` to reserve a dedicated research-owned runtime path and keep cross-workstream access behind the established event/query contracts.

Runtime foundation goals:

- load/validate the public research catalogs once into immutable indexed definitions
- introduce sparse per-civilization research state using stable IDs; do not copy the graph into each civilization
- implement visible node state, Research Pressure/evidence/applicability/capability indexes, active projects, Effective Research Lab allocation, directed-program capacity, and materialized-view revisions
- use events/indexes, never full graph scans per simulation tick
- implement capability/blocker/query interfaces first so shipbuilding/logistics/UI/AI can integrate without reaching into internal state
- keep distributed/secrecy/collaboration extensions as separately owned sparse modules layered on the same state model
- add deterministic plain-C# unit/smoke tests and save-serialization guards before wiring into presentation/gameplay
- do not replace the existing prototype gameplay research system until migration/acceptance is explicit
- gameplay VERSION remains unchanged until runtime integration is intentionally accepted

## Active visual-quality continuation

The current graphical slice adds guarded left-drag navigation across galaxy, system
and surface scales; wheel zoom crosses directly into a selected known system and
selected planet. The 100-system catalog remains legible as a compact sector over the
Milky Way overview, and surveyed planet cards expose radius, mass, gravity, temperature,
pressure, atmosphere and natural-satellite context. Established colony dressing includes
connected avenues, a population-scaled high-rise skyline, a landing pad and animated
civilian shuttles. Traffic now follows distinct landing-pad-to-horizon departure and arrival
paths with climb, descent and off-screen transit instead of circling the settlement. Surface camera controls follow the intended mouse scheme: left-drag pans,
middle-drag changes the view angle and the wheel now reaches a roughly eye-level street view
or a 1.2-kilometer colony overview.
Terrain color, surface breakup, reflectivity, sky, horizon, fog and sunlight now vary by
environment class, while a stable body-specific shader seed prevents two similar worlds from
reusing the exact same visible ground pattern.
The engine boot splash and campaign-loading layer now use dedicated
cinematic Milky Way/Earth artwork, a visible preparation status and progress treatment;
campaign creation or switching remains input-blocked until its authoritative state is ready.
PR #282 merged that boot/loading slice at
`38f583cf8b8bc3c8173db2f0b7015becf5166726`; a newly opened world remains paused
through the final loading frame. PR #283 merged four coherent human ship portraits at
`3f27738b991c68c6355cfaaa3e4d473a12554b66`; build choices and completed fleet rows
show the vessel. PR #285 merged the four authoritative species portraits and three-role
Terran leadership council at `9a9b4875cff32a6d01e2319838e5895478b41aef`.
Diplomacy uses a species portrait only after the contact's civilization is identified.

The active visual-feel slice adds brief category-colored responses to accepted orders
and major player-visible events. Surveyed stars receive distinct regional and orbital
treatments using existing archetype knowledge, without revealing undiscovered facts.

The next surface-quality pass is intentionally cosmetic over the existing free-placement
authority. Temperate established settlements use darker steel/glass materials, stepped
podium-and-tower silhouettes, repeated illuminated floor bands, roof equipment and two
connected district ring roads. Population still determines settlement density; placed
buildings, collisions, costs, output and saved coordinates remain unchanged. PR #287
merged that skyline foundation at `820193c44984936f64d4a34718c59b7b31b05cc2`.
The follow-up widens the default surface framing, lowers temperate terrain glare and fills
the road network with six plazas, park trees, low-rise blocks and illuminated street posts;
PR #288 merged it at `c9c184ebe5de25e7a5a36890e6350baf8d9719d7`.
The next map-legibility slice separates all 100 playable catalog stars from decorative
background stars and frames the campaign's local sector within the Milky Way overview.
PR #289 merged that scale and interaction treatment at
`2ef505b2e27777462d6c0a0071659ca68ec3fa2c`. Research programs now receive deterministic
vector emblems colored by scientific domain and state, plus an at-a-glance active,
available and mature summary; the observer-safe research horizon remains unchanged.

New Player games now open a graphical game-type choice before confirmation. Story
Campaign has its own illustrated card but is disabled beneath a visible Coming Soon
overlay. Sandbox has separate Milky Way artwork and is the only enabled choice; it
continues into the existing confirmed, backed-up 100-system campaign creation path.

Planet surfaces now blend a generated 1254-pixel temperate soil, grass and gravel albedo into the
body-seeded terrain shader while preserving environment-specific color on dry and hostile worlds.
The Sandbox setup and landed-surface header were compacted so every required control remains usable
at the supported 1280x720 viewport. The full six-design shipyard now has maintained high-resolution
artwork, including the Sealed Resource Outpost Vessel and Interstellar Bulk Freighter, with registry
coverage enforced by quality validation. A clean exact-head rendered journey produced 23 captures
and passed 110 real-input checks without loader or runtime exceptions.

The planet construction catalog now opens from a dedicated graphical Build control instead of
permanently covering the settlement. Its expanded multi-column dock stays below 300 pixels at
1280x720, clips long card descriptions cleanly, and Escape closes the catalog before returning to
orbit. The collapsed view preserves more than 600 pixels of vertical world framing while retaining
selected-building actions. Exact-head rendering passes 23 captures and 112 real-input checks.

Owned-fleet presentation now carries the vessel's persistent design identity through to its artwork
path. Specialized colony-role craft therefore retain the Sealed Resource Outpost Vessel portrait,
while freighters retain their cargo-vessel portrait after construction; legacy fleets continue to
use the stable role fallback. Quality validation also requires every production design to own a
distinct existing artwork source.
Game build, Core 48/48 and quality 9/9 pass after the fleet presentation change.

The full Milky Way overview now layers a deterministic 42-galaxy deep field over the surrounding
black sky. Elliptical, spiral and edge-on silhouettes vary in scale, distance tint and rotation,
while an exclusion ellipse keeps the primary Milky Way readable and the 100-system sector remains
fully framed. Exact-head rendering passes 23 captures and 113 real-input checks.

Star-system identity now renders in a bordered orbital information plate below the milestone strip.
The system name, presentation scale and survey status no longer overlap guided-campaign controls,
while the orbital diagram retains its existing pan, zoom, planet selection and infrastructure layer.
