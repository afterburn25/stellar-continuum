# Stellar Continuum Asset Manifest

Status: **Authoritative early-release asset registry**  
Owner: `work/visual-style-assets`

This registry prevents duplicate visual concepts, records provenance/status, and maps production-candidate visuals to the gameplay concepts that currently exist in shared integration.

## Immediate station and player-fleet production — 2026-09-13

The [Human Modular Orbital Station](content/HUMAN_MODULAR_STATION.md) is first:
bare cores with empty hardpoints, research-gated modules and visible upgrades
supporting hub, defense and shipyard/industry roles. Concept images and a proposed
assembly/research design are available; models and runtime behavior remain queued.
The [player-fleet queue](content/PLAYER_FLEET_ROADMAP.md) follows with the
Pathfinder Scout, then completes the other five existing Terran designs and shared
parts, icons, effects/audio and shipyard assets. Existing portraits and procedural
geometry are references. New source/model/material packs remain queued; their
presence in the roadmap does not promote them to production-candidate or ready.
Register completed files here and in Engine Assets with their existing design IDs,
source/provenance, dependency list, version and actual validation state.

## Status vocabulary

- **Concept candidate** — design reference only; no model, gameplay integration or final visual approval is implied.
- **Production candidate** — intended for the playable build; import/runtime validation has passed, but final integrated-context review may still be pending.
- **Production ready** — validated in the actual integrated screen/background at intended sizes and approved for continued use.
- **Temporary** — intentionally provisional and expected to be replaced.
- **Superseded** — retained only for continuity/history; do not use in new work.

## Human modular station concepts — 2026-09-13

Design ID: `human_modular_orbital_station`. These references are not runtime
models or default loadouts. Full prompts, reference roles and image hashes are in
[station provenance](art/HUMAN_MODULAR_STATION_PROVENANCE.md).

| Asset | Purpose | Status |
| --- | --- | --- |
| `assets/visual/stations/human-station-bare-cores-v1.png` | Empty hull and mounting-frame progression at the first four tiers | Concept candidate |
| `assets/visual/stations/human-modular-station-tiers-v1.png` | Equipped examples after researched modules are built | Concept candidate |
| `assets/visual/stations/human-station-module-upgrades-v1.png` | Three visible levels for defense, shipyard, habitat and power modules | Concept candidate |
| `assets/visual/stations/human-station-vertical-expansion-v1.png` | Tier 5 and Tier 6 stack empty upper decks above the retained Tier 4 base | Concept candidate |
| `assets/visual/stations/human-station-tier7-starbase-v1.png` | Tier 7 encloses the existing stack into a solid starbase while retaining empty module interfaces | Concept candidate |
| `assets/visual/stations/human-station-built-in-docking-v1.png` | Core-integrated docking clamps and telescoping personnel tunnel; available without optional modules | Concept candidate |
| `docs/content/HUMAN_STATION_DESIGN.json` | Proposed slots, module levels, empty starting state and unbound research prerequisites | Concept candidate; gameplay not implemented |

## Runtime visual system

### Cinematic revision — 2026-09-10

| Asset / implementation | Purpose | Provenance | Status |
|---|---|---|---|
| `assets/visual/space/deep-field-v2.png` | Full-frame distant galaxies, used only at galaxy scale | Original OpenAI image generation; prompt recorded in `CINEMATIC_ASSET_PROVENANCE.md` | Production candidate |
| `assets/visual/space/milky-way-layer-v2.png` | Face-on barred spiral layer | Original OpenAI image generation; same provenance record | Production candidate |
| `assets/visual/space/galactic-dust-detail-v1.png` | Mip-filtered cloud and dust material inside the procedural galaxy mask; never a map-object layer | Original OpenAI built-in image generation on 2026-09-11; exact prompt and SHA-256 in `CINEMATIC_ASSET_PROVENANCE.md` | Production candidate |
| `assets/visual/ui/{panel,button,button-hover,button-pressed}-frame.svg` | Scalable nine-slice panel and button frames | Original project SVG geometry | Production candidate |
| `assets/visual/shaders/system_sky.gdshader`, `SystemSkyBackdrop.cs` | Deterministic local stars and nebulosity; no galaxy imagery | Original project shader and seeded layout | Production candidate |
| `Spatial/OrbitalStructureView.cs` | Shipyard, launch complex and mining-network 3D models and construction phases | Original procedural meshes and materials | Temporary; replace with authored production meshes |
| `SurfaceBuildingVisuals.cs` | District high rises and playable module silhouettes | Original procedural geometry and facade shaders | Temporary; further art pass required |

These entries supersede earlier galaxy backdrop usage. Existing Sol image credits and their resolution limits remain applicable; no claim is made that bitmap source detail increases when enlarged.

Sol uses nine original NASA planetary image assets in `assets/visual/sol/`.
Their exact source URLs, credits, hashes, projection framing and color/resolution
limits are recorded in [Sol appearance sources](SOL_VISUAL_SOURCES.md). The same
record ships as `PLANET_IMAGE_CREDITS.md` in the Windows demo. Confirmed Sol bodies
use these images; procedural and incompletely surveyed worlds retain class-level
materials. Saturn's rings are original presentation geometry.

| Asset / family | Path | Purpose | Provenance | Status |
|---|---|---|---|---|
| Deep-Space Instrumentation tokens | `assets/visual/ui/visual_tokens.json` | Canonical color, geometry, spacing, typography-size and motion roles | Original project-authored | Production candidate |
| Shared Godot Theme | `assets/visual/ui/stellar_continuum_theme.tres` | Project-wide PanelContainer/Button/Label treatment via `gui/theme/custom` | Original project-authored from tokens | Production candidate |
| Runtime palette | `src/Game/Presentation/VisualPalette.cs` | Semantic token-color mirror for direct-drawn presentation | Original project-authored | Production candidate |
| Runtime icon loader | `src/Game/Presentation/VisualIconLibrary.cs` | Lazy cached loading of committed SVG assets through stable `res://` paths | Original project-authored | Production candidate |
| Semantic navigation icon family | `assets/visual/ui/navigation/nav_*.svg` | Twelve color-coded 64×64 rail silhouettes for research, economy, construction, shipyard, exploration, colonization, logistics, relations, inspection, home, galaxy and settings | Original project-authored SVG geometry; no external references or copied assets | Production candidate |
| Research lock icon | `assets/visual/icons/research/icon_research_locked.svg` | Observer-safe locked research state | Original project-authored SVG geometry | Production candidate |
| Strategic visual map | `src/Game/Presentation/Main.VisualMap.cs` | Complete regional scene with stellar glow, survey arcs, colony and fleet markers from observer knowledge | Original project-authored | Production candidate |
| Integrated map hook | `src/Game/Presentation/IntegratedMain.Visuals.cs` | Selects the complete graphical regional renderer; the command shell owns HUD presentation | Original project-authored | Production candidate |
| Cinematic main-menu backdrop | `src/Game/Presentation/MainMenuBackdrop.cs` | Slow, restrained presentation of the startup artwork with a readable command-panel veil | Original project-authored code consuming the project startup art | Production candidate |
| Cinematic main-menu artwork | `assets/visual/loading/stellar-continuum-splash.png` | Retained main-menu backdrop and Story Campaign card | Generated for this project with OpenAI's built-in image generation tool on 2026-09-09; prompt requested original text-free cinematic strategy-game art | Production candidate |
| Earth-orbit loading artwork | `assets/visual/loading/stellar-loading-splash.png` | Loading backdrop with a separate live progress display | User-supplied artwork, locally edited with OpenAI's built-in image tool on 2026-09-11 to remove only the baked loading display; source, hash, and exact edit prompt in `art/LOADING_SPLASH_PROVENANCE.md` | Production candidate |
| New-galaxy loading artwork | `assets/visual/loading/stellar-galaxy-generation.png` | New Game generation screen with live stage progress and a beginner tip | User-supplied artwork, edited with the built-in image tool to remove baked UI; exact prompt and hash in `art/LOADING_SPLASH_PROVENANCE.md` | Production candidate |
| Saved-game loading artwork | `assets/visual/loading/stellar-save-loading.png` | Save restoration screen with live stage progress and a beginner tip | User-supplied artwork, edited with the built-in image tool to remove baked UI; exact prompt and hash in `art/LOADING_SPLASH_PROVENANCE.md` | Production candidate |
| Four-arm campaign galaxy | `assets/visual/space/campaign-galaxy-four-arm-v1.png` | Matched 100-system campaign overview, Sandbox card and continuous overview-to-region transition | Generated for this project with OpenAI's built-in image generation tool on 2026-09-10; exact prompt and implementation limits are recorded in `FUN_VISUAL_VERTICAL_SLICE.md` | Production candidate |
| Main score and interaction cues | `assets/audio/music/claimed-by-the-void-loop.mp3`, `assets/audio/sfx/*.wav` | Continuous menu/campaign music plus hover, confirm, discovery, construction, launch and alert feedback | Main score supplied by the user; SHA-256 `25C81BEE74C37DC91F0895FA68DB72B026C028C65D951634D07CD4AE0B325FA2`. SFX remain original deterministic additive synthesis authored for this project. No creator, license, or rights claim is inferred. | Production candidate |
| First-generation human ship portraits | `assets/visual/ships/*.{jpg,png}` | Image-led shipyard choices and portraits for completed Pathfinder Scout, Science Vessel, Patrol Corvette, Colony Ship, Resource Outpost Ship and Interstellar Bulk Freighter fleets | Generated for this project with OpenAI's built-in image generation tool on 2026-09-09; individual prompts requested a coherent text-free hard-science-fiction fleet family. Resource Outpost Ship and Bulk Freighter added 2026-09-10 with distinct sealed-habitat/refinery and cargo/loading silhouettes for role readability | Production candidate |
| Species representatives | `assets/visual/species/*.jpg` | Observer-safe contact portraits for the four authoritative playable biology definitions and the active civilization identity | Generated for this project with OpenAI's built-in image generation tool on 2026-09-09 from exact `SpeciesCatalog` morphology, habitat and perception facts | Production candidate |
| Diplomatic communications rooms | `assets/visual/species/*-communications-v2.png` | Four panoramic enclosed command-room scenes, each representative at a species-appropriate terminal; 2172×724 source-quality PNGs | Original OpenAI built-in image generation edits, 2026-09-11; project species art references and full prompts recorded in `docs/art/DIPLOMACY_COMMUNICATIONS_PROVENANCE.md` | Production candidate |
| Terran leadership council | `assets/visual/leaders/*.jpg` | Civil Administration, Science Directorate and Fleet Command portraits on the campaign page | Generated for this project with OpenAI's built-in image generation tool on 2026-09-09; text-free, role-specific near-future human portrait prompts | Production candidate |
| Temperate colony ground detail | `assets/visual/surface/temperate-ground-albedo-v1.png` | Repeating surface albedo detail beneath the procedural terrain palette; alien worlds consume luminance only or restrained chroma | Generated for this project with OpenAI's built-in image generation tool on 2026-09-10; prompt requested a source-quality square, top-down, text-free, seamless natural soil/grass/gravel game texture with flat lighting | Production candidate |
| Main-menu presentation | `src/Game/Presentation/MainMenuLayer.cs` | Existing Continue/New Game/Quit behavior with v1 hierarchy/colors over procedural background | Original project-authored | Production candidate |
| Rendered visual QA record | `docs/SCREENSHOT_VISUAL_QA_2026-09-08.md` | Findings from real Godot screenshot run `34253094688` | Project QA record | Current |

The startup menu is intentionally above gameplay HUD layers (`MainMenuLayer` CanvasLayer 100) so dynamic gameplay labels cannot render through the menu.

### User-supplied main score

`assets/audio/music/claimed-by-the-void-loop.mp3` is the user-provided source file
`claimed_by_the_void_loop.mp3`, copied without transcoding. Its SHA-256 is
`25C81BEE74C37DC91F0895FA68DB72B026C028C65D951634D07CD4AE0B325FA2`.
The runtime uses one non-spatial `AudioStreamPlayer` with Godot's MP3 loop flag so
menu and gameplay context changes preserve the same playback position. The older
`menu-continuum.wav` and `deep-space-operations.wav` files are retained as
**Superseded** historical assets and are not deleted.

### Human ship portrait provenance

The four square 1254×1254 source images were generated independently for the named
designs so each silhouette reflects its real gameplay role. Prompts shared a graphite,
titanium and deep-navy near-future human design language with restrained cyan propulsion,
credible scale, no text, logos, border, UI or watermark. Design-specific direction was:

| Runtime file | Design direction |
|---|---|
| `pathfinder-scout.jpg` | Compact fast reconnaissance craft, twin drive pods and forward sensor array |
| `deep-space-science-vessel.jpg` | Long laboratory spine, habitat ring, dishes, interferometer booms and radiators |
| `patrol-corvette.jpg` | Armored compact escort, recessed point defenses and restrained weapons |
| `interstellar-colony-ship.jpg` | Vast settlement carrier with habitat, greenhouse, cargo and industrial seed modules |

Committed JPEGs are runtime-optimized at quality 92 and retain the full 1254×1254 crop.
The lossless generated files remain external source material rather than bloating the
playable package. Integrated Godot screenshot validation must confirm all four textures
load and appear in both shipyard choices and finished-fleet rows.

### Species and leader portrait provenance

The four species source portraits were generated at 1254×1254 from the authoritative
biology catalog: upright carbon-water Terrans; radial four-manipulator high-pressure
pelagics; dense horizontal high-gravity quadrupeds with two tool manipulators; and
radial multipedal cryogenic hydrocarbon organisms with bioluminescent, chemical and
vibration communication. No portrait changes a species trait or reveals an unidentified
contact. The three Terran leader portraits depict civil, scientific and fleet roles in
the same early-interstellar visual setting. They are presentation identities only;
leader gameplay effects require an authoritative leader system before being added.

All seven committed JPEGs use the complete 1254×1254 crop at quality 92. Lossless
generated sources remain external. Prompts required no text, logos, border, UI or
watermark. Real Godot capture loads every portrait at runtime and records the campaign
leadership view.

## SVG technical contract

All production-candidate icons are original project-authored vectors with:

- `24 x 24` SVG viewBox and declared size;
- approximately 2-unit safe padding;
- `1.8` optical root stroke;
- rounded caps/joins;
- neutral `#E6F0F6` source color for semantic tint/modulate at runtime;
- transparent background;
- no embedded text, script, raster image, external href, or data URI.

They are validated by `scripts/validate_visual_assets.py` and have been rendered at 16/20/24/32/48 px during development.

The project imports SVGs at 4× resolution for crisp larger emblems. Controls retain
explicit display sizes and the shared button theme caps icon width at 22px.

## Graphical navigation icons — 9

Directory: `assets/visual/icons/navigation/`. Original project-authored vectors following the same scalable stroke contract; production candidates for the graphical demo shell.

| Asset | Filename | Current concept |
|---|---|---|
| Galaxy | `icon_nav_galaxy.svg` | galaxy navigation |
| Home | `icon_nav_home.svg` | home navigation |
| System | `icon_nav_system.svg` | system navigation |
| Ships | `icon_nav_ships.svg` | ships navigation |
| Menu | `icon_nav_menu.svg` | menu navigation |
| Close | `icon_nav_close.svg` | close navigation |
| Back | `icon_nav_back.svg` | back navigation |
| ZoomIn | `icon_nav_zoom_in.svg` | zoom in navigation |
| ZoomOut | `icon_nav_zoom_out.svg` | zoom out navigation |

## Core strategic icons — 16

Directory: `assets/visual/icons/core/`

| Asset | Filename | Current concept |
|---|---|---|
| Pause | `icon_hud_pause.svg` | Simulation pause/resume control |
| Speed | `icon_hud_speed.svg` | Simulation speed vocabulary |
| Save | `icon_hud_save.svg` | Save campaign |
| Support | `icon_hud_support.svg` | Support/diagnostics/help |
| Research | `icon_action_research.svg` | Generic research action |
| Construction | `icon_action_construction.svg` | Generic construction action |
| Exploration | `icon_system_exploration.svg` | Exploration/survey system |
| Logistics | `icon_system_logistics.svg` | Logistics network/system |
| Relations | `icon_system_relations.svg` | Diplomacy/relations system |
| Colony | `icon_map_colony.svg` | Established settlement/colony |
| Scout | `icon_map_scout.svg` | Scout role; reused by ship-role family |
| Information | `icon_status_info.svg` | Informational/inspection state |
| Warning | `icon_status_warning.svg` | Caution/strategic threat |
| Success | `icon_status_success.svg` | Success/completed/accept state |
| Unknown | `icon_status_unknown.svg` | Unknown/unresolved; reused by survey/diplomacy |
| Hostile | `icon_status_hostile.svg` | Hostile/dangerous; reused by diplomacy/combat |

Current runtime consumption includes PlayerControls, ExplorationMissionPanel, SystemInspectionPanel, LogisticsNetworkPanel, RelationsPanel, main strategic map markers, and fleet/colony overlays.

## Economy resource icons — 3

Directory: `assets/visual/icons/resources/`

This family intentionally mirrors only resources present in `CivilizationEconomyState`; no speculative food/supply/mineral family is pre-created.

| Asset | Filename | Authoritative relationship |
|---|---|---|
| Credits | `icon_resource_credits.svg` | `Credits` / `LastCreditsPerSecond` |
| Industry | `icon_resource_industry.svg` | `Industry` / `LastIndustryPerSecond` |
| Science | `icon_resource_science.svg` | `Science` / `LastSciencePerSecond` |

## Construction project icons — 5

Directory: `assets/visual/icons/construction/`

This family mirrors the current `ConstructionRegistry` exactly.

| Asset | Filename | Registry ID |
|---|---|---|
| Planetary Research Network | `icon_construction_research_network.svg` | `research_network` |
| Industrial Automation Program | `icon_construction_industrial_automation.svg` | `industrial_automation` |
| Orbital Launch Complex | `icon_construction_orbital_launch_complex.svg` | `orbital_launch_complex` |
| Orbital Shipyard | `icon_construction_orbital_shipyard.svg` | `orbital_shipyard` |
| Warp Test Facility | `icon_construction_warp_test_facility.svg` | `warp_test_facility` |

## Ship-role icons — 3 new + shared Scout

Directory: `assets/visual/icons/ships/` plus shared Scout.

| Asset | Filename | Registry/role |
|---|---|---|
| Scout | `icon_map_scout.svg` | `warp_scout` / `FleetRole.Scout` |
| Science vessel | `icon_ship_science_vessel.svg` | `science_vessel` / `FleetRole.Science` |
| Patrol corvette | `icon_ship_patrol_corvette.svg` | `patrol_corvette` / `FleetRole.Military` |
| Colony ship | `icon_ship_colony_ship.svg` | `colony_ship` / `FleetRole.Colony` |

The Scout asset is deliberately reused rather than cloned. The map overlay renders all four player fleet roles with distinct silhouettes and role colors; route lines remain derived from legitimate own-fleet destination state.

## Survey map-state icons — 3 new + shared Unknown

Directory: `assets/visual/icons/map/` plus shared Unknown.

| State | Filename | Authoritative relationship |
|---|---|---|
| Unknown | `icon_status_unknown.svg` | `SystemSurveyLevel.Unknown` |
| Detected | `icon_map_detected.svg` | `SystemSurveyLevel.Detected` |
| Partially Surveyed | `icon_map_partially_surveyed.svg` | `SystemSurveyLevel.PartiallySurveyed` |
| Fully Surveyed | `icon_map_fully_surveyed.svg` | `SystemSurveyLevel.FullySurveyed` |

Runtime rule: common-catalog Unknown stars remain visually quiet at map scale; a selected Unknown target receives the explicit Unknown icon. Detected/Partial/Full states use dedicated shapes so survey progress is not brightness/color-only.

## Diplomacy icons — 10 + shared Unknown/Hostile

Directory: `assets/visual/icons/diplomacy/`.

| Concept | Filename | Authoritative relationship |
|---|---|---|
| Contact | `icon_diplomacy_contact.svg` | `ContactAwareness` / observer-visible contact |
| Peace | `icon_diplomacy_peace.svg` | `DiplomaticPoliticalState.Peace` / peace agreement |
| War | `icon_diplomacy_war.svg` | `DiplomaticPoliticalState.AtWar` |
| Ceasefire | `icon_diplomacy_ceasefire.svg` | `DiplomaticPoliticalState.Ceasefire` / ceasefire agreement |
| Access granted | `icon_diplomacy_access_granted.svg` | `AccessPermission.Granted` |
| Access denied | `icon_diplomacy_access_denied.svg` | `AccessPermission.Denied` |
| Trade | `icon_diplomacy_trade.svg` | Trade agreement/offer |
| Agreement | `icon_diplomacy_agreement.svg` | Generic agreement/proposal |
| Claim | `icon_diplomacy_claim.svg` | `TerritorialClaimSnapshot` |
| Dispute | `icon_diplomacy_dispute.svg` | `TerritorialClaimResponse.Disputed` |

Shared `icon_status_unknown.svg` and `icon_status_hostile.svg` remain the canonical Unknown/Hostile political-state symbols. The Relations panel currently consumes contact/proposal/peace/ceasefire/accept/reject vocabulary without altering diplomatic rules.

## Combat icons — 6 + shared Warning

Directory: `assets/visual/icons/combat/`.

| Concept | Filename | Authoritative relationship |
|---|---|---|
| Hold | `icon_combat_hold.svg` | `MilitaryOrderType.Hold` |
| Defend | `icon_combat_defend.svg` | `MilitaryOrderType.Defend` |
| Attack | `icon_combat_attack.svg` | `MilitaryOrderType.Attack` |
| Retreat | `icon_combat_retreat.svg` | `MilitaryOrderType.Retreat` / retreat events |
| Damage | `icon_combat_damage.svg` | `CombatEventType.DamageApplied` |
| Destroyed | `icon_combat_destroyed.svg` | `CombatEventType.FleetDestroyed` |

Shared `icon_status_warning.svg` remains the canonical strategic threat symbol.

## Current production-candidate screen treatment

The main menu uses `MainMenuBackdrop.cs`: a deterministic 92-star procedural field, restrained orbital arcs around a distant stellar focus, and a dark lower-left planetary limb. It uses no large texture, continuous animation, fake interface text, third-party art, or hidden gameplay data.

Existing menu controls and actions remain UI-owned and unchanged in function. The visual workstream only changes hierarchy, color, theme, backdrop, and z-order needed to keep the overlay visually intact.

## Known temporary / fallback presentation

- `Main.cs` still draws legacy prototype star circles, colony rings and some fleet marks underneath the new `Main.VisualMap.cs` overlays. These are temporary fallback marks until captured-context review confirms the new shapes are sufficient by themselves.
- `Main.Shipbuilding.cs` still owns a dedicated Shipbuilding HUD line and legacy science-marker presentation. A screenshot-confirmed layout collision between that HUD and PlayerControls is reported to UI workstream #27 rather than silently repositioned here.
- Godot fallback font remains temporary; no font with uncertain redistribution terms has been added.
- `Stellar Continuum` remains a working title; no trademark symbol or supposedly cleared commercial logo is authorized by this manifest.

## Research iconography boundary

Technology-specific icons remain intentionally deferred. Shared integration still contains a small legacy technology registry while Adaptive Research #179 owns an evolving possibility graph. Visual work will wait for a stable presentation/category contract rather than hardening transitional technology IDs or creating hundreds of speculative icons.

## Provenance rules

Production assets must be one of:

- original project-authored vectors/code;
- original generated artwork with generation provenance recorded;
- project-owned commissioned/contributed artwork with rights recorded;
- appropriately licensed third-party material with license/source recorded.

Public visibility on the web is not sufficient provenance.

For future generated raster art, record generator/tool family, creation date, intended crop/aspect ratio, and whether the committed file is source-quality or runtime-optimized.

## Validation

Run:

```bash
python3 scripts/validate_visual_assets.py
python3 scripts/test_validate_visual_assets.py
```

The normal `work/**` build validates the visual contract and runs Godot 4.7.2 headless editor/runtime smoke tests. `.github/workflows/screenshots.yml` also runs on `work/visual-style-assets` so real integrated main-menu/campaign/colony/relations frames are available for visual review.

No icon or coherent visual family is promoted to **Production ready** solely because it imports; integrated rendered-context review is required.

The 2026-09-08 recovery audit retained all 46 vectors and their recorded original
project-authored provenance. Token/palette/Theme values agree; the missing runtime
`Exploration` role is now exposed using the existing canonical color. The gate now
checks actual values, contrast and registered loader paths in addition to SVG safety.
It rejects unsupported SVG elements/attributes, including CSS and all href forms,
and enforces rounded caps/joins and inherited stroke width.

Readiness remains **Production candidate**. The saved screenshot QA record predates
the latest integrated map/button consumption. Map survey symbols currently draw at
9/11/12.5 px (selected Unknown at 14 px), below the 16 px development proof size;
their final legibility and overlap with legacy marks require actual rendered review.
The validator does not prove SVG path bounds/safe padding, optical recognizability,
composited contrast, keyboard behavior or screen layout. See
`docs/handoffs/visual-style-assets.md` for the recovered gate evidence and integration
request.
