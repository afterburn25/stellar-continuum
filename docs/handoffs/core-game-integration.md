# Core integration — full-game modes, cinematic maps and colonies

The current presentation and mouse-navigation candidate is recorded in [Cinematic presentation handoff](cinematic-presentation.md). It includes responsive 1080p/720p layout, organized inspectors, timed construction/upgrades, direct ship travel orders and 3D orbital structures. Consult its exact-commit validation record before release.

## Active 100-system playable-foundation continuation

The current economy presentation replaces the temporary universal-Credit bridge with
species-specific sovereign money. Humans display United Earth Dollars; Pelagic,
Compact High-Gravity and Cryogenic civilizations display the Tide Mark, Forge Crown and
Thermal Ledger with distinct denomination scales. Construction, shipbuilding, research,
colonization, colony surfaces, fleet upkeep and Developer resource feedback all use the
active civilization currency. Internal normalized treasury fields stay unchanged for save
compatibility, and the future Interstellar Credit is not exposed before it exists.
The follow-up makes civilian taxation depend on represented employment. Each colony derives
working-age population and infrastructure-limited job capacity; Industrial Automation raises
that capacity, while outposts remain outside ordinary civilian taxation. The Colonies page shows
employed versus working-age population and the resulting rate. Opening balance is preserved at
the baseline employment rate.
Staffed surface complexes now add their explicit jobs to the broader civilian employment base,
bounded by working-age population; their economic contribution therefore includes both direct
output and the tax activity created by those jobs.
Treasury health is now explicit on the Economy page: surplus, deficit with calculated reserve
runway, or depleted. The deficit/depletion messages name current recovery actions rather than
leaving a negative flow as an unexplained counter.
The authoritative economy now carries unpaid base operations as persistent arrears. Revenue pays
old obligations before rebuilding reserves, the last current-operations payment fraction is saved,
and invalid negative/nonfinite arrears fail closed. Fresh Industry and legacy science now scale
with the paid fraction of current base operations, closing the previous loophole where an insolvent
civilization continued producing at full speed. The Economy page displays the sovereign arrears
amount and payment coverage. This closes the zero-treasury free-cost hole while leaving explicit
service-priority controls and wider fleet/population consequences for a later slice.
The player-facing stored Industry counter is now labeled **Materials** and explains its physical
flow: industrial labor, mines and fabricators create processed inputs; storage caps them; freight
delivers outpost stock; construction and shipyards consume them. Internal `Industry` names remain
for save compatibility and established simulation APIs. Economy cards expose stored/capacity and
funded daily output. Colony surfaces show the same funding percentage, and outpost extraction rates
and status now reflect actual operating coverage rather than advertising nominal free production.
Fleet transit and detailed surveys scale with that shared operating capacity. Zero funding preserves
route/fuel/mission state while suspending movement, freight transfer and colony/outpost founding;
mission status gives the player an explicit funding-recovery reason.
Completed surface complexes now expose a direct Shut down/Restart control. Disabled complexes use
no workers or power, create no output or district bonus, incur no upkeep and remain visibly marked
on the 3D surface. The enabled flag persists in the surface-building payload with a backward-safe
true default.
Surface administration is now a persistent progression gate. A new colony's level-1 Command Center
supports 16 modules, the starting homeworld's level-2 Planetary Hub supports 32, and a level-3 center
supports 64. Upgrades spend sovereign currency and stored Materials through simulation authority.
The surface header exposes the current level, capacity, affordability and upgrade action, while the
central model visibly expands by tier. Sealed resource outposts retain their separate eight-module
limit. Pre-feature saves default to level 3 so existing 64-module settlements are not truncated.
The L1→L2 order requires the completed Industrial Automation Program. L2→L3 consumes the live
Adaptive Research `orbital_industry` capability supplied by established Orbital Manufacturing;
the disabled player control reports the missing requirement and the authority revalidates it.
The same authority uses surveyed physical radius for the final footprint gate: bodies below
0.35 Earth radii cannot expand past 32 surface modules and report orbital development as the path.
Surface building authorizations are no longer flat across all worlds. A bounded multiplier derived
from the occupied body's gravity, atmosphere, pressure, temperature and radiation adjusts base and
upgrade currency costs. Earth stays at 1.00×; the surface palette shows exact adjusted quotes and
its economy tooltip explains the local factor. Commands and cancellation refunds recompute the
same value, preventing presentation/authority price drift.
Administration upgrades use the same multiplier for currency and Materials, and their existing
surface action presents the adjusted values.
Advanced surface upgrades now use the same Adaptive Research authority as orbital construction.
Practical Fusion Power, Advanced Additive Manufacturing, Interplanetary Trade Standards and
Closed-Loop Recycling respectively gate advanced power, fabrication, trade and habitat complexes.
The selected-building action remains visible but disabled with its exact research blocker; the
simulation command accepts a required capability view and rejects direct bypasses without mutation.
Completed surface structures now have a persistent Priority toggle beside Shut down. Workforce and
power allocation sorts priority operations first and retains deterministic ID order within each band.
The control allows survival infrastructure or key production to stay online during a shortage;
authority, save validation and UI status all consume the same 0/1 operating-priority state.
The scene renders priority with an elevated amber halo and shutdown with a red ground halo.
Completed labels are selection-only, while incomplete sites keep visible progress, reducing text
clutter without hiding actionable construction state.
The surface shader now tiles `temperate-ground-albedo-v1.png` beneath its body-seeded procedural
variation. Temperate terrain uses restrained source chroma, while alien classes use luminance or a
small chroma contribution so the new microdetail does not turn every planet into Earth.

The graphical New Game selector now opens a real Sandbox setup page before confirmation. It
supports random, legacy numeric and normalized text seeds, shows their deterministic internal
value, renders a live seed-driven barred-spiral vector preview, copies a spoiler-free setup and
records the entered seed plus the fixed recommended option
snapshot and generator version in ordinary campaign saves. Old saves may omit metadata and old
numeric creation retains the established generator defaults. The immediate follow-up implements
all four maintained species as portrait-backed Player-start choices. Human starts remain on Earth
in Sol. A nonhuman choice assigns the Player to that species' naturally viable homeworld while the
Human Commonwealth remains on Earth under AI control; the selected species persists in generation
metadata and deterministic validation covers every nonhuman option. The galaxy generator then implements
the seeded four-arm barred spiral and maps its full bounds across the fitted galaxy overview while
retaining legacy disk generation for numeric callers. A bounded seeded vector layer adds 420
sharp arm/core lights, and 24 non-interactive background galaxies vary by morphology, apparent
depth, tint, scale, rotation and parallax. The next slice adds an optional persisted physical
stellar class without changing legacy saves, enforces the roadmap's exact Balanced 100-star deck,
renders surveyed stars by class and uses class in survey hazards. It also enforces the exact
18/22/42/14/4 planetary-architecture groups, including legitimate planetless systems and authored
Sol. The following naming slice replaces `SYS-###` in balanced Sandbox campaigns with 100 unique
proper names from a dedicated seed stream. Generated planets receive stable proper names and moons
receive stable parent-linked epithets; authored Sol and legacy numeric reconstruction remain intact.
The next fairness slice reserves two non-overlapping nearby natural expansion worlds for each
non-ancient major civilization. It uses the authoritative species habitability evaluator, excludes
every home system and unstable compact/hot stars, and caps opening distance at 340 map units.
Twelve additional deterministic seeds validate every major start. Build, Core 39/39, Quality 8/8,
Simulation 22/22 and UI contract 22/22 pass locally. The current navigation slice adds a pure,
deterministic sparse lane network with a connected minimum-distance backbone, three-nearest local
alternatives and shortest-route queries. The map draws observer-known lanes, starts retain at least
two links, and surveyed inspection reports home distance in ly and pc. Movement remains on the
provisional reach adapter pending multi-leg fleet state, drive range and fuel integration.
The complete repository sweep found and repaired two pre-existing Adaptive Research validation
baselines: reference-profile count 7→8 and collaboration base rate 100→400 RP/year with expected
joint output 2,160→8,640. Both now match canonical data, and all 15 research validators pass.

The latest Core slice connects Adaptive Research to the live treasury. Directed programs now
carry a complexity-scaled daily operating cost derived from assigned Effective Research Labs;
the campaign deducts actual spend after the base economy step and advances RP only by the funded
fraction. Zero funding produces zero progress, partial funding produces proportional progress,
and normal AI will not start a project without its first day of funding. Research cards expose
daily burn, estimated total operating cost and live funding percentage. Economy cash flow adds a
Research programs row and includes actual research spending in operating costs and net flow.
The last spend/funding values persist in existing campaign economy payloads with backward-safe
defaults. A shared campaign command now charges Player and AI projects a one-time 0.5/2/7.5/25
Credit authorization cost across Foundation/Developing/Advanced/Frontier complexity. It requires
authorization plus first-day funding, charges only accepted projects, and exposes setup, daily and
estimated combined cost in the Research UI. Research cards also calculate treasury runway from the
post-authorization balance and the civilization's net flow before research; programs covered by
current income are labeled sustainable. Crossing into underfunding emits one campaign event and
returning to full funding emits one recovery event, with persisted funding fraction preventing
per-step notification spam. New projects reserve 0.3/1/3/8 Credits of prototype and validation
funding across the four complexity bands. The reserve is consumed at the three maturity boundaries,
shown on active research, and persists in backward-compatible Adaptive Research campaign schema 2;
invalid overspent or orphaned reserves fail closed. Requirement-specific physical experimental inputs
remain explicit follow-up scope. A disproven hypothesis closes its remaining experimental reserve;
schema-1 campaigns migrate without inventing a retroactive treasury charge.
Research choices and active cards also display the authoritative facility capability for the
starting/current stage; this is presentation of the existing physical gate, not a money-based bypass.
Active cards now offer mouse-driven Pause/Resume actions through the campaign command boundary.
Resume checks first-day operating cash and existing eligibility, and rejects hypothesis-resolution
pauses so the low-level resume path cannot skip a required scientific outcome.
The Economy Research line now displays active-program authorization paid, remaining milestone
reserve and daily operations. Authorization persists in the schema-2 funding record and invalid
negative values fail closed; a historical completed-program ledger is still follow-up scope.

Local validation after this slice: the shared game and Core project compile; all 13 executable
validation projects pass, including Core 38/38, Simulation 22/22, Logistics 4/4, Quality output,
Species checks and every standalone Adaptive Research suite. The maintained visual-assets and
research-catalog validators pass; the catalog retains its pre-existing graph-depth warnings.
The cost scale was reduced after the first run correctly exposed that early research spending
prevented the required Launch Complex. The retuned scale preserves paid research while restoring
both the ordinary Player settlement path and the 24x Developer path.

Adaptive Research is now the live player and AI research path. PR #268 merged the
species-safe 370-node campaign state, format-v15 persistence, lab-based Research page,
deterministic outcomes and temporary gameplay-capability bridge into `integration` at
`0903b0349eaa400e77df03509e6c3724621c0d7c`. Exact head `72d7023` passed both builds,
research outcomes, the complete native screenshot journey and both exported Windows
startup gates. The rendered repair also keeps operations pages above the map toolbar.

The current continuation disables the retired linear research step in integrated
campaigns, gives AI civilizations observer-safe Adaptive agenda selection, and makes
the guide and Research page share a reachable warp priority. Strategic distance
pressure reveals Prototype Warp only after its science and spacecraft prerequisites;
the completed physical Warp Test Facility supplies its specialist experiment capacity.
Core now follows the entire path and proves that the facility gate blocks bypasses.

Integrated construction and shipbuilding now use Adaptive Research directly. The Industry
page, system-map infrastructure markers, authoritative order checks, shipyard, campaign
objective and strategic AI share the same injected capability views. Orbital Manufacturing
grants the registered Orbital Industry capability; Controlled Warp Field uses established
Adaptive knowledge; Experimental Interstellar Transit controls first-generation interstellar
designs. Compatibility readers remain for isolated legacy tests and old-save tooling only.
A maintained regression deliberately sets the retired prototype flags and proves they cannot
bypass the Adaptive gates.
The subsequent cleanup removes the live one-way legacy technology projection entirely.
Adaptive Research now updates only the shared civilization development stage when Experimental
Interstellar Transit is achieved; it no longer writes retired fixed-tree completion flags.
Integrated campaign economy steps also stop banking the retired Science currency and report
zero legacy Science throughput. Existing save values remain intact for compatibility; finite
Effective Research Labs and their power state are the live research-production model.
Idle Industry is now bounded by a visible reserve capacity derived from colony infrastructure
and completed industrial/orbital projects. Production feeds active construction first, then
surplus storage is capped. Existing and explicit Developer-granted over-cap reserves are
preserved but cannot grow further while above capacity.

The current local Core continuation narrows new campaigns to 100 systems and builds
out the first playable management loop before any larger-galaxy expansion. Earth/Sol
remains the Human origin and other civilizations retain their distinct home systems.

Credits now fund infrastructure, ships, colony expeditions and freely placed surface
buildings. Administration, population services and active fleets create recurring costs.
A powered trade hub adds surface revenue. The new Economy page shows reserves, Earth
purchasing-power reference, gross revenue, each operating-cost category and reconciled
net daily flow. Construction, ship, surface and settlement screens expose affordability
before orders are placed.

The operations pages now connect owned state back to the map: Ships lists active fleets,
activity, location and upkeep with a Locate action; Colonies lists owned worlds with direct
orbital View and 3D Surface actions. Local validation after these changes passes the shared
build, Core runtime 26/26, simulation 22/22 and quality 8/8. A fresh exact-head Godot
screenshot/input gate and exported Windows startup gate remain required before publishing
or accepting this continuation. After the upgrade slice, local validation passes the shared
build with zero warnings/errors, Core runtime 28/28, simulation 22/22, quality 8/8, UI
contracts 22/22, Windows contracts 9/9 and the visual-asset validator.

The Relations page now issues war declarations through the observer-safe Diplomacy command
service. Armed fleet rows show integrity and current orders, and issue Hold, Defend and
Retreat through Core's matched Combat runtime. Engage Hostiles supplies the missing attack
action without passing foreign fleet IDs through presentation: the Combat command runtime
checks co-location, its live Diplomacy hostility policy and its normal attack preview, then
selects the first valid target in stable order. Failure is generic and non-mutating.
Deploy to Selected makes military movement player-accessible from the same fleet row. Core
requires an owned active military fleet, a real selected destination and supported operational
reach; accepted travel resets stale local Combat orders and flows through Exploration's shared
strategic movement and arrival processing.

The first follow-up colony-management slice makes rendered surface structures directly
selectable. An incomplete site can be cancelled with a 50% authorization-credit recovery and
no Industry recovery; a completed structure can be demolished without a refund. Core owns the
authorization and immediately removes the building's power demand/supply and production. The
native capture contract selects an actual 3D structure and exercises the visible action.

The next colony slice adds an upgrade action to selected completed structures. Base power,
science, fabrication and trade complexes each have one advanced form with explicit Credit and
available-Industry costs. Upgrades are authoritative, ownership checked and atomic; advanced
types remain unavailable in the build palette. They retain the exact saved position and use the
existing validated type identifier, so formats 12/13 need no schema change. Advanced models add
an illuminated crown, and the real-input capture selects and upgrades the rendered lab before
save/reload verification.

The next presentation seam replaces the Research page's repeated option list with a graphical
visible horizon. Its projection includes only completed, active and currently investigable
legacy nodes; future or blocked nodes never reach the UI. An investigable node is a real button
that calls the existing research command authority. This is intentionally a presentation seam,
not a partial Adaptive Research state cutover: the maintained runtime must enter campaign
persistence, stepping and gameplay capability effects as one separately validated milestone.

Colony placement now has an emergent specialization decision. Three completed complexes in
one functional family create a Research, Industrial, Commercial or Energy district and add
25% to that family's powered output. Advanced buildings count toward their base family.
Specialization and progress are visible on the surface and owned-world list, and are derived
entirely from existing saved construction state. Core validates activation and immediate
deactivation after demolition; no persistence version changes.

Colony surfaces now select a world palette from canonical environment facts. Temperate,
frozen, hot, airless, oceanic, reducing-atmosphere and rocky classes drive shader terrain,
exposed rock, sky, fog and sunlight colors. The visual class reaches presentation only after
the player has opened an owned colony; it does not change placement physics or persistence.

The human 2050 start is now multi-world: Earth, Luna (100,000 people) and a young Mars
settlement (250,000) are ordinary saved colonies in Sol. The Colonies page exposes orbital
and surface navigation for all three, including Mars's environment-driven 3D terrain. Tiny
dependent settlements pay 0.12 Credits/day administration, scaling to the established full
1 Credit/day at 250 million people. The guide and dashboard require an extrasolar settlement
for campaign completion, so the opening holdings cannot skip the exploration arc.
The surface hub now includes a bounded established-settlement cluster derived from population
and exact habitat requirements. Luna/Mars render sealed domes while Earth renders open towers;
all modules stay inside the already protected hub footprint and remain visual-only.
Habitat support is now an active colony decision: a fifth surface building reduces the local
cost 20% while powered, and its closed-loop upgrade reduces 40%. Multiple powered complexes
cap at 75%; loss of power immediately removes their reduction. Native capture places the
Mars habitat from the real build menu.
The orbital Colonies overview consumes the same derived surface output and reports the net
life-support bill, gross bill and active reduction together with building count and local
power demand/supply. It no longer displays an unreduced charge after habitat construction.
The home-system map also projects the established orbital construction projects beside
the star with distinct silhouettes and locked/available/active/complete state. Active work
uses the real construction progress; clicking a silhouette opens Industry operations. This
presentation adds no new save or simulation state.
Asteroid Resource Network is now an optional ordinary project after Orbital Industry and a
completed Launch Complex. It adds 1.50 Industry/day, 0.18 Credits/day upkeep, a connected
resource-site logistics node, and a distinct three-asteroid orbital marker. Launch Complex,
Shipyard, and Warp Test Facility upkeep is also included in the authoritative Economy flow.
Construction lock reasons are centralized in the registry and reused by command rejection,
orbital marker state and map feedback. Native capture clicks the locked asteroid marker and
requires both Orbital Industry and Orbital Launch Complex to be named.
The opening guide now points to direct project choices and surfaces the asteroid network's
1.50 Industry/day versus 0.18 Credits/day optional tradeoff between core-path builds.
Research and Industry removed their older cycling controls. Native capture now starts the
Research Network through its named `Chooseresearch_network` project control.
Ships also uses its named design buttons as the sole visible build/queue path. Its idle card asks
the player to choose a design, while an early locked shipyard renders without obsolete cycle and
build controls; native capture verifies that state.
Ship design eligibility now has one authoritative lock-reason query reused by the page and order
rejection, with exact capability and facility names instead of a generic prerequisite failure.
The bounded native capture allowance is six minutes after the expanded end-to-end journey reached
all screenshots and checks but exceeded its older five-minute shell limit under normal runner variation.
Research horizon possibilities are now large two-column program cards with visible descriptions,
state color, progress and a `BEGIN RESEARCH` affordance while retaining observer-safe node filtering.
Owned colonies now render as bordered world cards with grouped population, support, power and
specialization metrics and compact icon actions, replacing dense paragraph rows and oversized buttons.
Economy cash-flow details now use aligned two-column rows for income and operating costs rather
than a tab-formatted label, keeping names and daily values visually separate at 720p.
Generic Industry and Ships choices now use a compact three-column operation-card grid with title,
cost, description and authorization state instead of single-line menu buttons.
The top bar now exposes a bounded recent-events center instead of relying on the temporary status
line for important outcomes. Accepted capital orders and observer-filtered research, construction,
ship, exploration, colony and combat events are retained in sequence (32 maximum, newest 16 shown),
with unread count and campaign/mode reset isolation. Core validation covers ordering and bounds;
the native input gate starts real Research and Industry orders, opens the center, verifies both
messages and dismisses it through the visible close control.
The Exploration page now consumes a bounded observer-safe mission projection and renders graphical
fleet cards with role icon, phase, destination, ETA and the simulation's own status summary. Its
empty state points players to the map dispatch controls. The native gate verifies the visual empty
state on a real early campaign; active card fields remain sourced from `ExplorationReadModel`.
Logistics now projects its reconstructible home-system network into four visual flow metrics and
per-node infrastructure cards. Supply, demand, delivered flow, shortfall, node status and corridor
count remain read-only outputs of the existing logistics allocator. Native acceptance requires the
metric grid and at least the three represented Human Sol settlement nodes in the early campaign.
Relations now shapes the existing observer-safe presentation into a contact dossier with separate
political, communication, relationship, access, agreement, proposal and event cards. Existing
proposal, response, access and war commands remain authoritative and unchanged. Native acceptance
requires the graphical no-contact dossier during the ordinary early campaign.
System Inspection now consumes a dedicated fog-safe snapshot and renders survey progress, five
intelligence-signal cards and a separate known-settlement card. Unknown and partially surveyed
targets provide operational guidance without populating hidden facts. Native acceptance requires
the fully known Sol signals and Earth settlement card through the ordinary Inspect destination.

The first published follow-up exposed an earlier 720p navigation regression before the surface
journey: adding Economy as the tenth rail destination left Menu partially below the rail after a
1600×900 to 1280×720 resize. The rail now uses compact icon-button height and spacing so every
destination remains fully visible without scrolling at the supported minimum viewport.
The native gate then reached the complete three-site surface journey and showed that 1×
construction exceeded its old 90-second render budget from ordinary starting Industry. The
surface header now provides real 1×–4× player controls; the capture uses the visible 4× control
and still advances the ordinary simulation with no Developer resource grant.

Starting baseline for this milestone: integration `334004d15c1f0cff7ee6dc345c8de225a2603de4`, PR #233,
exact tested source `2b450a02f2625ce408ff6f9f7f9f660b98ab97fa`. All six CI runs passed,
including 43 real-input acceptance checks, 13 Godot captures and native Windows
startup. The earlier Adaptive Research helper crash was repaired; no new scratch
executable is part of this work.

The user has expanded the objective to a full game with separate Player and Developer
modes. Ordinary rules remain shared; Developer alone exposes explicit test tools and
24x time. Its campaign has a separate version 1 envelope, independent backups and a
persistent ToolsUsed marker. PR #239 records exact-source acceptance and build evidence. Player save boundaries reject Developer state. Legacy
demo files import only when no Developer save or backup exists and remain intact.
See `docs/GAME_MODES.md` for user flow, exact commands and next full-game priorities.

The graphics milestone remains cinematic direction B plus freely navigable
3D planet surfaces. The map now connects Milky Way overview, stellar region, orbital
system and focused planet. Original Milky Way/nebula backgrounds, native-resolution
GPU planet materials, observer-safe canonical appearances and layered Saturn rings
support those views. The existing regional simulation catalog and travel coordinates
remain authoritative. Fresh humans still start on Earth; old saves keep their homes.

Owned solid-world colonies open a real 3D surface with graphical building previews,
free X/Z placement, rotation, terrain/footprint validation and distinct models.
Generators, science labs and fabricators spend shared industry over simulation time;
completed powered structures affect economy. Model state persists in formats 12/13,
while saves without placed structures retain formats 8/9 or 10/11. Surface navigation
blocks strategic-map commands, and campaign/context changes close stale surface views.

See `docs/CINEMATIC_MAP_AND_SURFACE.md` for controls, budget rules, scope, provenance
and acceptance requirements. This is a bounded colony area, not full-planet terrain
streaming. No unrelated subsystem expansion or main promotion is included.

The combined candidate requires new model/persistence tests, real wheel/drag/placement
input captures, visual review at 1600x900 and 1280x720, all existing research/runtime
suites, and native Windows package verification. Only the exact tested source may be
merged into integration or supplied as the new demo. Local C# compilation is useful
but does not replace shader/render validation.

The sections below preserve the recovery history and previous demo investigation.

Current branch: `work/core-game-integration`. Recovered accepted baseline `c529a1a765776c0940f88002410bc70db740d05a`; Core was 285 commits behind with no unique work and was safely fast-forwarded. The complete 96-branch recovery snapshot is `docs/BRANCH_INVENTORY_2026-09-08.md`.

## Accepted after recovery

Testing/Release PR #222 passed full CI including Debug assembly, strict Godot error inspection and `STELLAR_RUNTIME_READY IntegratedMain` proof, then merged into `integration` at `32b8361c61f3c40965aa32629365e1a423befad6`. This repairs the independently reproduced #61 false-green runtime gate; prior process-exit-only results are insufficient evidence.

## Candidate assembled for combined validation

- Preserve recovered known-good backup during the first primary repair save, reusing the existing Core child `work/core-preserve-recovered-backup`.
- Existing Visual Assets PR #218 plus validator parity/safety/contrast checks, 46 unchanged original SVG resources and production-candidate manifest.
- Existing Galaxy PR #220 plus observer/campaign-safe bounded snapshots, GUI-first pointer routing and correct spatial-scale markers. Core resolved startup-hook conflicts while preserving strict runtime proof and hides the Visual strategic overlay while in a star-system view.
- Existing UI branch: dedicated wrapped shipyard status row and naturally stacked scrollable command/exploration panels. Core updated integrated refresh calls to the new presentation helper names; simulation ownership is unchanged.
- Existing Exploration child `work/colonization-shared-body-resolver`: shared bodyless compatibility resolution without retargeting explicit bodies, using transported population species and observer-visible availability; extended regressions validated by the family lead.
- Existing Species child `work/habitat-support-capability-foundation`: compiler-explicit deterministic reducer check and recovered lineage handoff. Original `work/species-race-mechanics` is superseded as confirmed by #16 and byte-identical accepted readiness code; its historical unique commits remain intact.
- Corrected shared WORKSTREAMS/CHAT_HANDOFF/PROJECT_STATE instructions that incorrectly pointed active research at the retired dev branch or main-first flow. No main modification or branch deletion.

## Validation and limits

Specialists ran actual-source topology/spatial/Species/Exploration checks and existing Python gates. Exploration's complete source-linked suite passed 22 central tests plus 51 initializer groups. Core ran eight visual contract regressions and seven smoke regressions. The combined candidate still requires full Release/Debug/Godot/quality validation and exact-head screenshots; the screenshot workflow now runs on Core and rejects semantic startup errors.

The local NuGet/Godot package path is not usable; full engine verification is performed through GitHub CI, not represented as a local pass. A Research scratch runner caused Windows CLR dialogs and was repaired separately on canonical `research/adaptive-research` at `aea15e410d75094b78ae2c41d12a58d599bcc7ef`; Windows/Linux topology CI and all23 local research suites passed. That branch's unfinished M20/catalog work is not pulled into this gameplay candidate.

## Focused milestone: playable Windows demo

The user has explicitly prioritized a playable demo. All requested branch families
have been recovered; further subsystem expansion is deferred. See
`docs/PLAYABLE_DEMO_MILESTONE.md` for the acceptance loop and priorities.

The ordinary campaign works through research, construction, physical scout/science/
colony ships, reconnaissance, full survey and settlement. Three seeds pass without
granted resources or knowledge. Opening pacing was the main weakness: first settlement
required 16.5–17.75 minutes at continuous 4x. Optional Play Demo uses seed 20260908,
the same rules and a bounded 24x clock (at most four quarter-day steps per frame),
reaching settlement in 164.98 active seconds plus player choices. Normal speeds
and balance remain unchanged. Demo save/restart/resume and backup tests preserve
the normal slot byte-for-byte.

The UI now gives next steps and ETA, visible ship building, Home and selected-star
commands, system navigation, and a clear-map panel toggle. Normal/demo replacement
requires confirmation and a successful checkpoint. Input is blocked behind the menu.
Exit is cancelled if saving fails; persistent menu errors and a command-feedback
strip explain failures above the active map view.

The first combined runtime exposed incomplete SVG imports. Build, screenshot and
export workflows now wait for import completion and reject engine errors and aborted
scans. The Windows exporter required the shared Game.sln; its single project retains
the existing shared assembly.

Published candidate `cc304a6e4c7cba90809169282d6572b1b42c4457` passed the complete
build, screenshot capture and actual exported Windows startup. Its verified package
is in [Windows demo run 34275646050](https://github.com/afterburn25/stellar-continuum/actions/runs/34275646050).
Subsequent interface/feedback fixes require fresh exact-candidate CI and visual review.
PR #223 is the combined review record; do not reuse an older artifact as evidence
for newer source.

After acceptance, prioritize actual player feedback on this short loop and Windows
hardware behavior. Defer the unconnected diplomatic presence producer, foreign-vessel
target identity, large-galaxy profiling, astronomy catalog #221 and Research M20
until they are required by a separately assigned milestone. Existing branches and
their unfinished work stay preserved. No promotion to main is part of this work.

Three specialist slots execute leads in waves; completed agents are not claimed as continuously running. Each branch family retains its existing ownership and handoff. Publication uses authenticated GitHub Git-data operations with exact tree verification and non-forced ref updates because local git push authentication is unavailable; local implementation commit metadata may differ from the published commit, but content and established remote ancestry are preserved.

## Full-game continuation — Adaptive first colony and player pacing

PR #278 merged the maintained end-to-end Player campaign into `integration` at
`52f31574ccd0210b0cc54aa43cc408098ee241a5`. The acceptance path now uses all
thirteen live Adaptive Research projects and physical research, launch, shipyard
and warp-test infrastructure before building scout, science and colony ships,
surveying four systems and settling 250M conserved passengers. The exact head
passed the build, Adaptive outcomes, two Windows package/startup jobs and real
Godot screenshot capture. Screenshot setup is restricted to canonical Ubuntu
package sources so unrelated runner feeds cannot block visual validation.

The next pacing slice raises ordinary Player maximum speed from 4× to 8× while
leaving costs, simulation steps and authority unchanged. The measured seed-20260908
first-colony path remains day 5787.75 and falls from 24.12 to 12.06 ideal active
minutes. Developer acceleration remains the separately gated 24× option.

The active graphical continuation replaces middle-button-only navigation with guarded
left-drag panning on the galaxy, orbital-system and colony-surface cameras. Short left
clicks retain selection and building interaction, while drags cannot issue those actions.
Wheel zoom enters the selected reconnoitred system and selected planet without requiring
a double-click. At full-galaxy scale the complete 100-star catalog is plotted as a visible
compact sector, with the bitmap fading before close zoom can expose low-resolution detail.
Surveyed world inspection includes physical statistics and parent/moon context. Surface
dressing adds avenues, high-rise towers, landing infrastructure and deterministic ambient
civilian shuttle traffic scaled to colony population.

PR #280 merged this spatial/colony slice into `integration` at
`dc40304a458f35987e5d9e9429b043a7e9c5422a` after the exact-head game build,
Adaptive outcome gate, two Windows packages/startups and real Godot input/render
capture all passed. The next isolated visual slice adds the generated cinematic
startup artwork at `assets/visual/loading/stellar-continuum-splash.png` as both the
engine boot splash and a real input-blocking campaign preparation layer. Its source,
purpose and generation provenance are registered in `docs/ASSET_MANIFEST.md`.

That loading presentation, the four-ship portrait family, and the species/leadership
portrait set are integrated through PRs #282, #283 and #285 with exact-head Windows,
build, Adaptive outcome and real Godot capture checks. The active follow-up adds brief
event-driven action effects and distinct surveyed-star rendering. Keep those effects
driven by the existing observer-visible notification feed; do not create a second event
authority.

## Full-game continuation — authoritative interstellar travel

The generated lane graph now controls ordinary Exploration and Colonization reach. Each
fleet has a 360 ly initial maximum leg range, retains its final mission destination, and
persists an ordered queue of intermediate lane waypoints. Player and AI orders use the
same route assessment. A fleet may consume several legs during a large simulation step,
detects and observes each system it physically reaches, and cannot accept a new route
while between systems. Routes beyond current leg range fail with a useful reason rather
than silently flying directly across the galaxy.

Travel state is additive and backward compatible: older saves with a direct destination
and no waypoint queue continue their existing leg, while new saves preserve route and
range through a mid-flight round trip. Save validation rejects invalid ranges, unknown
waypoints, orphan waypoint queues, and queues that do not end at the mission destination.
Galaxy route graphics now follow every lane segment, and mission ETA sums the remaining
route instead of measuring a straight line to the final target.

Local validation at this milestone: game build succeeded with zero warnings/errors and
the simulation suite passed 23/23 central checks, including the new deterministic route,
range rejection, mid-flight persistence and final-arrival regression. The known nullable
warning remains in the test-only diplomatic communication fixture.

The immediate follow-up moves performance out of a generic fleet default and into the
ship registry. First-generation scout, science, patrol and colony designs now define
420, 400, 340 and 300 ly maximum legs respectively, alongside their existing distinct
speeds and sensors. Newly seeded and constructed ships inherit and persist both exact
design identity and range. Legacy fleets without a design ID retain the compatible
360 ly fallback. Save validation accepts that legacy state and rejects unknown or
role-incompatible design identities. Simulation remained 23/23 and Core 39/39 after
the change, including construction inheritance checks.

The fleet page now exposes the operational facts needed to make those rules playable:
exact design name, current speed in ly/day, maximum lane leg in ly, and remaining route
legs/distance. A shared route-metrics query supplies both this display and mission ETA,
so the interface cannot disagree with movement about a multi-leg course. The graphical
course remains segmented through every remaining waypoint. Game build, simulation
23/23 and quality 8/8 pass on this interface slice.

Fleet travel now consumes finite design-specific fuel endurance: scout 1200 ly,
science 1100 ly, patrol 800 ly and colony 750 ly. Operational reach rejects a route
that cannot reach its next owned refueling point, reports the required and available
endurance, and projects the remaining reserve for an accepted route. Movement consumes
the same endurance per ly and owned colonies refill vessels on arrival. Fuel capacity
and remaining endurance persist with strict finite bounds; older saves receive a full
1000 ly compatibility tank. The fleet page shows the live endurance. The ordinary and
accelerated first-colony campaigns still complete at the same strategic milestone;
game build, simulation 23/23 and Core 39/39 pass.

Core military deployment now consumes the same accepted route and waypoint queue as
science, scout and colony orders. Destroyed fleets and local survey resets clear stale
waypoints. The command regression verifies a military course ends at its stated mission
target, and Core remains 39/39 with the first-colony campaign intact.

Adaptive propulsion capability now affects ships constructed after discovery. Reliable
FTL from Stable Warp Drive raises speed 18%, maximum lane leg 30% and endurance 35%.
Extended FTL range from Long-Range Warp Architecture raises the original design values
35%, 75% and 75% respectively. Existing fleets retain their persisted launch values;
research does not refit ships remotely. The shipyard cards show the effective propulsion
generation and exact performance before the player spends credits or industry. The
prototype capability bridge remains compatible for legacy tests/campaigns. Game build,
simulation 23/23 and quality 8/8 pass.

Known lane graphics now encode present traversal capability: solid cyan lanes fit at
least one active player ship's maximum leg, while dashed amber lanes require a better
drive. This is derived from live fleet performance, so newly researched construction
changes the strategic map only after an improved ship actually launches. Build and
quality 8/8 pass. Harsh-world staffed outposts remain a separate incomplete roadmap
slice and are not represented as ordinary colonies.

The harsh-world outpost foundation now has an explicit persistent ResourceOutpost
settlement kind. Its staffed crew does not reproduce as a civilian population and does
not generate ordinary colony tax revenue, while administration, population services,
habitat support and surface upkeep still cost money. An owned resource outpost provides
half-capacity refueling; civilian colonies provide full service. The owned-world page
labels outposts explicitly. Unknown settlement kinds fail save validation, and legacy
saves default to Colony. Game build and simulation 24/24 pass, including outpost economy,
crew, support and save continuity.

The dedicated outpost mission is now playable. The shipyard exposes a Sealed Resource
Outpost Vessel costing 950 Industry, 130 Credits and 8 million specialist personnel. Its
persisted design identity safely shares the Colony fleet role without being admitted by
ordinary colony planning. On the Colonies page, selecting this vessel switches the planning
window to fully surveyed rare-resource worlds that are too harsh for its crew species to
colonize. The exact-body command charges a 90-Credit deployment authorization, uses the same
lane, maximum-leg and fuel reach rules as other missions, and transfers the vessel's actual
personnel into a ResourceOutpost settlement on arrival. Habitable worlds direct the player
to use a colony ship; occupied, native, un-surveyed, unreachable and already-reserved systems
remain blocked with explicit reasons. Game build is clean; simulation 25/25, Core 39/39,
quality 8/8, logistics 4/4 and Species checks pass. The next outpost slice is bounded power,
extraction, storage and freight production rather than free passive resource income.

Outpost extraction is now power-bound and storage-limited. A newly founded sealed hub
produces nothing. A completed, powered Fabricator processes the confirmed deposit at its
represented Industry rate into a local material stockpile; each processing complex adds
100 units to the hub's initial 25-unit capacity. The stockpile stops exactly at capacity,
survives save/load, and contributes no empire Industry or trade income until a future freight
service is represented. Unpowered processing reports offline, and the Colonies page shows
live extraction, storage and the current blocking status. Ordinary colony Fabricators retain
their existing direct Industry output. Game build and simulation 25/25 pass with capacity,
power, economic-isolation and persistence coverage.

Outpost stockpiles now have a represented freight path. The shipyard exposes an
Interstellar Bulk Freighter costing 800 Industry and 90 Credits, with a 100-unit hold,
350-ly maximum lane leg, 1,000-ly fuel endurance and 0.14 Credits/day operations. A
Collect control on each staffed outpost dispatches the first idle freighter stationed at
a developed colony. Core validates ownership, origin, current mission state, extraction
activity and authoritative lane/fuel reach. The vessel loads only material physically in
the outpost stockpile, returns along a second validated lane route, and converts cargo to
usable Industry only on arrival at its recorded home colony. Cargo, mission endpoints and
the mid-return route survive save/load. The Ships page shows freight phase and hold usage.
Simulation 26/26, Core 39/39, quality 8/8 and Species checks pass.

Sealed outposts now obey their small hub role on the surface. Their initial hub supports
eight player-placed modules rather than an ordinary colony's 64-building envelope, and
the surface header displays the correct capacity. They can place power, extraction,
science and habitat support modules, but cannot create a civilian Trade Hub to bypass
the freighter economy. Command validation and save loading both enforce the capacity.
Game build remains clean and simulation remains 26/26.

The landed surface screen now presents the same outpost economy directly: processor output
is labeled extraction, the local stockpile and capacity are visible, and the live blocker is
available as the operations tooltip. It no longer mislabels local material as empire Industry
or claims civilian trade income that the authoritative economy does not award. Build and
quality 8/8 pass.

Population can no longer grow past unrepresented food and water. Exact occupied-body area,
species-relative natural habitability and solvent suitability establish natural capacity, with a
small sealed-infrastructure baseline. Powered Controlled Agriculture and Water Reclamation each
add two billion people of food or water support. Growth approaches zero at carrying capacity and
shortage produces bounded decline; Colonies and Surface expose both capacities, the sustainable
population and its limiting supply. Opening tax yield is 0.75 Credits per billion per day and
fleet operations are retuned to 0.08 scout / 0.12 science / 0.16 colony / 0.35 military / 0.14
freighter Credits per day, allowing a mature homeworld to fund the required first expedition
without removing infrastructure, ship or deployment costs. Colony planning now treats the
120-Credit deployment authorization as part of action availability. Core passes 40/40, simulation
26/26, quality 8/8 and Species checks pass after this slice.

The next labor slice removes free operation from surface complexes. Forty-five percent of local
population forms the first bounded workforce pool, while each building requires 15,000–80,000
workers according to its function and upgrade tier. Stable construction order assigns scarce
staff before local power allocation. Unstaffed complexes produce no power or economic/support
output but retain upkeep, and return automatically when population is sufficient. Surface and
Colonies display available versus required workers and identify the shortage. Core passes 41/41;
occupational skills, wages and cross-sector labor allocation remain follow-up scope.

Housing now joins food and potable water as the third carrying-capacity gate. Exact-body natural
capacity and sealed infrastructure establish the baseline; a powered Habitat Complex adds one
billion housing spaces and its closed-loop upgrade supplies three billion. Growth and shortage
decline use the lowest of all three capacities, while Surface and Colonies show housing directly.
The harsh-world outpost planner now also includes its 90-Credit deployment authorization in
action availability, matching the ordinary colony planner and the authoritative order command.

Local food and potable-water reserves now buffer shortages causally. Stocks are measured in
population-days, fill from represented surplus production, and cap at 30 food days and 7 water
days. A deficit consumes the matching reserve before population decline begins. New starts and
settlement expeditions carry explicit provisions, exact quantities persist in Player saves, and
Surface/Colonies show days remaining. Game build, Core 41/41 and simulation 26/26 pass.

The current visual milestone adds a generated high-resolution temperate ground albedo to the
body-seeded surface shader, with restrained blending so Mars and other hostile palettes remain
distinct. It also fixes the 1280x720 Sandbox setup and landed-surface header layouts. Shipyard art
coverage now includes all six registered designs; the new outpost vessel and bulk freighter each
have distinct maintained 1254x1254 artwork and quality validation rejects any missing mapping or
source file. Exact-head Godot evidence at `work/terrain-texture-capture-final16` validates 23 rendered
screens and 110 real pointer/keyboard checks, including Earth and Mars surface construction,
save/reload, all six ship textures and a real named ship build. The capture log contains no loader,
unhandled runtime or screenshot-driver errors.

The surface build catalog is now a collapsible multi-column dock opened by a visible Build button.
Newly opened surfaces prioritize the 3D settlement with only a slim contextual action bar; Escape
closes the expanded catalog before leaving the surface. Long descriptions stay clipped within their
cards, and the full catalog remains below 300 pixels tall at 1280x720. Exact-head evidence at
`work/surface-collapsible-capture` passes all 23 captures and 112 real-input checks without runtime
errors, including ordinary construction, reload continuity and Mars placement.

Completed fleet rows now consume an exact design-derived artwork path from the observer-safe owned
fleet snapshot. This fixes the shared Colony-role fallback that could show a Resource Outpost Vessel
as an ordinary colony ship; legacy fleets still resolve by role. Quality validation requires all six
production designs to map to distinct existing artwork files and remains 9/9.
The game build is clean and Core remains 48/48.

Galaxy overview now fills the previously empty surrounding sky with a deterministic 42-object deep
field. The field includes elliptical, spiral and edge-on silhouettes with varied apparent distance,
tint, scale and rotation, drawn outside the primary Milky Way disk so the local 100-system sector
stays legible. Exact-head evidence at `work/distant-galaxy-capture2` passes 23 captures and 113
real-input checks with no runtime or loader errors.

The system name and survey status now use a dedicated orbital information plate positioned below
the First Colony milestone strip. The prior overlap on the Sol view is gone without moving the
orbit diagram or changing hit geometry. Exact-head rendering remains 23/23 captures and 113
real-input checks.

Rare-resource outposts no longer extract indefinitely. Each confirmed site exposes a deterministic
finite reserve scaled by its body's radius and mass; new foundations record it explicitly and older
saves resolve it compatibly. Simulation consumes the lesser of funded processor output, free
storage and remaining material. A depleted site produces zero, keeps collected stock available for
freight and reports the blocker on both colony and landed-surface views. Invalid negative, nonfinite
or over-cap reserves fail save validation. Build is clean; simulation 26/26, Core 48/48 and quality
9/9 pass, including depletion, legacy initialization, persistence and existing freight.

Deposit choice now has an operational consequence. Every rare-resource body derives a stable
material family and Marginal/Standard/Rich/Exceptional grade. A bounded accessibility factor uses
the exact world's gravity, pressure, radiation and temperature, and the product of access and grade
scales the powered processor's real extraction rate. Colony and surface views expose material,
grade, yield and accessibility; validation covers varied deterministic profiles and bounded output.

The outpost funding diagnostic now formats its percentage invariantly. Linux runners previously
inserted a locale-specific space in `0 %`, causing the simulation gate to fail even though extraction
was correctly zero; the player-facing status and cross-platform assertion now agree on `0%`.
The screenshot driver now verifies the last accepted-action effect category rather than requiring
the short animation to remain active after a PNG completes. Slow Linux software rendering could
take 19 seconds to encode the research screen and legitimately let the effect expire before the
later assertion; the check still requires both actions to have triggered real visual feedback.

Surface infrastructure now has a first physical-maintenance loop. Every completed complex persists
condition, loses it deterministically when enabled operations are underfunded, scales useful output
with remaining condition and stops below the 15% operating threshold. Shutdown prevents operating
wear. The player can select the structure, inspect condition and efficiency, and spend an exact
damage-scaled Materials quote to restore it. Repair authority rejects foreign, missing, incomplete,
fully healthy and unaffordable targets without mutation. Existing saves without a condition field
load at full condition; invalid condition fails closed. Game build is clean; Core 49/49, simulation
26/26 and quality 9/9 pass.

Surface construction progress is now readable as physical work rather than one unexplained meter.
The authoritative material fraction deterministically selects preparation, foundations/utilities,
primary structure, equipment installation and commissioning, while the selected-site bar and 3D
label show phase progress and remaining Materials. Pause, allocation and save/resume behavior remain
unchanged; the phase is derived and adds no migration-sensitive persisted field.
The Colonies page summarizes average physical condition and damaged/failed complex counts, turning
the Land action into the direct route from empire-level warning to surface repair.

Surface workforce and grid allocation no longer use raw construction order for every default tie.
Explicit player Priority remains first; otherwise generators, water reclamation, controlled
agriculture and habitat support receive automatic essential-service precedence before discretionary
research, fabrication and trade. The surface selection status distinguishes ESSENTIAL SERVICE from
PLAYER PRIORITY. A focused regression proves the automatic order and deliberate player override.

The first bounded grid-storage module is playable. A Grid Battery Complex costs 35 sovereign budget
units plus 320 Materials, needs 10,000 workers and daily upkeep, stores 12 grid-power-days, and moves
at most 4 GW with 90% charge and discharge efficiency. Stored energy is local to the physical complex,
survives save/load, starts empty in older saves and fails closed if corrupt. Allocation uses the actual
simulation interval, including treasury and sustenance calculations, so an almost-empty battery cannot
support a long tick as if every day were the first. The landed and colony views show GWh state plus
charging/discharging GW, and the battery has a distinct 3D bank visual.

Physical outpost freight now includes bounded cargo handling. The Interstellar Bulk Freighter moves
at most 20 material units per day while loading or unloading. Basic hubs provide 4 units/day and the
new buildable Cargo Terminal adds 20 units/day when staffed and powered, up to the vessel limit. Partial
loads remain persistent, unfunded operations halt handling, and full Industry storage leaves cargo
aboard. Surface, Colony and Fleet views expose the physical bottleneck. Release build and simulation
validation 26/26 pass.

Surface maintenance now uses the exact occupied environment. Gravity departure, vacuum or extreme
pressure, severe temperature and radiation produce a bounded maintenance-exposure multiplier. It
accelerates condition loss only when maintenance is underfunded; fully funded operations remain stable
and shutdown still prevents wear. Surface and Colonies expose the value, and Core 51/51 plus a clean
release build cover Earth-normal and faster Mars deterioration.
