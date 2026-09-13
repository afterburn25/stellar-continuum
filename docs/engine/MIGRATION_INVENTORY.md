# Stellar Continuum engine migration inventory

Baseline audited: branch `engine/stellar-engine-migration`, commit `97091aee` (tagged `migration-baseline/stellar-continuum-0.1.7`), version `0.1.7-alpha`. This is an inventory for a controlled C++23/SDL3/Vulkan migration. Existing gameplay and save compatibility are the constraints; this document does not authorize a rewrite. The pending territorial influence work remains separate on `feature/territorial-influence` / PR #323 (tip observed as `27eb67ff`) and is not part of this baseline.

## Shape of the repository

The project is a Godot 4.7.2 .NET application targeting .NET 8 (`Game.csproj`), with 345 C# source files, 122 presentation files, 206 simulation files, 8 campaign files, 4 persistence files, 3 diagnostics files, and one units file. There are 191 test files, 28 Python validation/capture scripts, 50 developer tools, 76 data files, 171 tracked asset files, and one main scene (`scenes/Main.tscn`). `project.godot` and `export_presets.cfg` are the runtime/export entry points. `Main.tscn` wires `Main`, `AudioDirector`, sidebar, demo, controls, missions, inspection, logistics, relations, and menu nodes.

The application boundary is concentrated in `src/Game/Presentation` (122 C# files) and the Godot scene. The simulation is comparatively well isolated: `src/Game/Simulation` contains 206 files and the authoritative step coordinator explicitly says Godot supplies accepted simulation time only (`GalaxySimulationStepCoordinator.cs`). Persistence is detached JSON serialization in `src/Game/Persistence`; campaign/session orchestration is in `src/Game/Campaign`.

## Classification by gameplay domain

| Domain | Classification | Concrete inventory and migration seam |
|---|---|---|
| Galaxy, generation, stars, bodies | PURE GAME LOGIC | `Simulation/Generation` (GalaxyGenerator, PlanetaryBodyGenerator, SolCatalogPreset, NearbyStarCatalog); astronomy data is embedded from `data/astronomy/hyg-nearby-500-v1.json`. Rendering of the map is presentation-owned. |
| Coordinates, distances, units | PURE GAME LOGIC | `Units/InterstellarDistanceUnits.cs`, generation and exploration models; retain deterministic numeric contracts and formatting tests. |
| Clock/calendar | PARTIALLY ENGINE-DEPENDENT | `Simulation/Time/CampaignCalendar.cs` and simulation clock logic are portable; `Presentation/Main.cs` owns frame/process input and calls accepted elapsed time. |
| Civilizations and species | PURE GAME LOGIC | `Simulation/Models`, `Simulation/Species` (morphology, perception, habitability, population/cohorts, xenobiology); JSON/data driven, no Godot imports found in simulation. |
| AI | PURE GAME LOGIC | `Simulation/AI` plus research agenda/planning services. Inputs are state/read models; scheduling/threading remains an engine/runtime integration concern. |
| Economy, industry, construction | PURE GAME LOGIC | `Simulation/Economy`, `Industry`, `Construction`; command/result and snapshot patterns are portable. UI feedback is presentation-dependent. |
| Logistics, trade, resource outposts | PURE GAME LOGIC | `Simulation/Economy`, `Simulation/Colonization`, logistics models and `docs/LOGISTICS_SYSTEM.md`; preserve exact affordability, reach, reserve and shortage semantics. |
| Colonies and stations | PURE GAME LOGIC | `Simulation/Colonization`, `Construction`, settlement models; surface visuals and node trees are presentation. |
| Ships, fleets, shipbuilding | PURE GAME LOGIC | `Simulation/Shipbuilding`, fleet/order models, fleet crew/species logic; combat capture tools are engine-dependent consumers. |
| Combat | PARTIALLY ENGINE-DEPENDENT | `Simulation/Combat` and `Simulation/Massive` contain rules, readiness and resolution; `tools/Main.MassiveCombatFixture.cs` and capture tools bind the test/demo to Godot. |
| Research/knowledge | PURE GAME LOGIC | `Simulation/Research`, `Knowledge`, `Adaptive` and `data/research/v1/*.json`; runtime contracts and benchmarks are portable. Research UI is Godot presentation. |
| Diplomacy | PARTIALLY ENGINE-DEPENDENT | `Simulation/Diplomacy` state/command logic is portable; `Presentation/DiplomacyWorkspaceView*`, transmission styles and portrait loading are Godot-bound. |
| Missions, exploration, events | PARTIALLY ENGINE-DEPENDENT | `Simulation/Exploration` and mission/event models are portable; event routing, captions, and capture harnesses depend on scene/audio services. |
| Territory | PURE GAME LOGIC (pending) | No merged baseline territorial subsystem is assumed. Keep PR #323 / `feature/territorial-influence` isolated until its contracts are reviewed and ported separately. |
| Save/load and recovery | PARTIALLY ENGINE-DEPENDENT | `CampaignSaveService.cs`, `CampaignStatePersistenceService.cs`, `DeveloperCampaignPersistenceService.cs`, `CampaignSessionService.cs` use `System.Text.Json`; file paths and Godot lifecycle hooks are boundary concerns. Preserve schema/version/legacy handling. |
| UI and navigation | ENGINE-DEPENDENT | 122 files in `Presentation`, mostly `using Godot;`, Node/Control containers, draw calls, Texture2D/ResourceLoader, input and viewport/display APIs. Replace behind a stable read-model/command interface. |
| Rendering and map visuals | ENGINE-DEPENDENT | `Main.cs`, `CinematicArt.cs`, visual style classes and map render helpers use Godot Canvas/Texture2D/ImageTexture and draw APIs. No portable renderer exists in baseline. |
| Input/window/diagnostics | ENGINE-DEPENDENT | `Main.cs`, `Diagnostics/SupportLogger.cs`, `tools/WindowPointerProbe.cs`; direct `Input`, `DisplayServer`, `RenderingServer`, Engine version and viewport/window lifecycle calls. |
| Audio/voice | ENGINE-DEPENDENT | `Presentation/AudioDirector.cs`, `Presentation/Audio/Voice/*`; AudioStreamPlayer, AudioServer buses/effects, `GD.Load`, Godot FileAccess plus Windows SAPI and offline neural worker. Keep voice/event contracts portable, backend replaceable. |
| Assets/shaders | ENGINE-DEPENDENT | `assets/visual` includes UI SVG/theme resources, PNG/JPG textures and imports; shader files are Godot `.gdshader`. Audio includes WAV/MP3. Asset paths use `res://` in presentation. |
| Tools and validation | ENGINE-DEPENDENT harness, portable assertions | 28 Python scripts and 50 C# tools capture Godot scenes, screenshots, startup, performance, voice and massive combat. Treat their gameplay assertions as migration acceptance tests while replacing harness adapters. |
| Tests | MIXED | 191 files: C# project checks for research/species/combat/voice plus Python Godot smoke, package/download and screenshot checks. Keep pure simulation checks runnable without graphics; retain headless/render smoke separately. |

## Direct engine coupling

The only Godot project dependency is `Godot.NET.Sdk/4.7.2`; the application has no SDL/Vulkan abstraction. `rg` shows 78 asset files with engine import/resource formats and direct `using Godot;` concentrated in presentation, diagnostics and tools. Representative hard dependencies are `Main : Node2D`, `AudioDirector : Node`, `AudioStreamPlayer`, `Texture2D`, `GD.Load`, `ResourceLoader`, `FileAccess`, `Input`, `DisplayServer`, `RenderingServer`, `AudioServer`, `DrawString`, `GetNode`, `QueueFree`, and scene `res://` paths. Simulation files are intended to remain free of these APIs; this boundary should be enforced during extraction.

## Dependency map and extraction order

`Data/JSON + deterministic models -> Simulation services/step coordinator -> Campaign/session and persistence -> presentation read models/commands -> Godot scene/UI/audio/render/input`. The safest native extraction starts with `Simulation/Models`, `Units`, `Time`, `Generation`, then economy/logistics/colonization/shipbuilding/research/diplomacy/combat services. Persistence can follow once canonical snapshots and schema tests are frozen. The native host should own time/input/window/render/audio adapters while the simulation accepts explicit commands and elapsed simulation time. UI classes should not be ported as gameplay logic.

Native C++ extraction opportunities already visible in the code are the pure simulation directories, `CampaignCalendar`, distance units, JSON data contracts, read-model records, and detached save envelope conversion. Keep C# Godot presentation as a compatibility shell during the first migration slices. Audio voice backends, screenshot tools, and Godot resource import handling are late adapter work.

## Tests, assets, and known integration risks

The test surface includes dedicated adaptive research runtime/outcome/strategic/foreign technology projects, species mechanics checks, massive combat validation and benchmark artifacts, voice core checks, Windows demo packaging/download checks, and Godot smoke/screenshot checks. Existing scripts validate research models, visual assets, startup failure, player expedition, screenshot capture, demo packaging, and massive combat. These tests encode more behavior than the scene files and should become the migration acceptance matrix.

Assets are not merely cosmetic: `data/research/v1` is copied into output by the project file; nearby-star JSON is embedded; portraits and cinematic textures are loaded from `res://`; UI themes and SVG navigation icons are scene resources; WAV/MP3 playback and voice profiles are runtime-coupled. Preserve relative asset IDs and save references while changing loaders.

Known baseline risks from the repository are Godot resource lifetime/audio cleanup (comments in `AudioDirector` mention stale native wrappers and explicit drain checks), window/display and high-refresh capture sensitivity, startup failure handling, and performance-sensitive galaxy/map rendering. Existing handoff artifacts and docs cover research continuity, voice lifecycle, visual QA, logistics, species mechanics, and map performance. Treat these as compatibility evidence, not permission to redesign gameplay. The 0.1.7 baseline includes the galaxy/star-rendering repair merge; territorial influence remains pending and must not be silently folded into this inventory’s baseline.

## Migration invariants

1. Preserve deterministic seeded generation, simulation step semantics, IDs, command/result behavior, and JSON save schema before changing presentation.
2. Keep pure simulation compilable and testable without Godot, SDL, Vulkan, a window, audio device, or GPU.
3. Introduce native adapters at the current presentation boundary; do not make simulation types know renderer/input/audio handles.
4. Run existing C# behavioral checks and equivalent native checks before deleting or bypassing Godot paths.
5. Maintain `res://` asset identity and migration mappings for saves and data; record any intentional schema change explicitly.
