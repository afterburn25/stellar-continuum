# Solar Economy / Logistics — playable demo handoff

Updated 2026-09-08. Existing branch `work/solar-economy-logistics` was safely fast-forwarded from accepted `c529a1a` to Core candidate `42bce3f`. Issue #26 history was reviewed. The initial audit added the maintained normal-progression test at cd5eeb9; the subsequent Core-authorized demo implementation is described below. No new logistics system, normal-campaign rebalance, save migration, push, or direct integration/main change was made.

## Demonstrated result

The normal generated campaign can progress through research, construction, physical ships, reconnaissance, full science survey and founding a second colony. No funds, technology, survey knowledge, fleet, population, location or completion progress was injected. The actual `GalaxySimulationStepCoordinator` advances the real simulation in quarter-day increments; ordinary subsystem command boundaries issue the player's orders. Default generation retains 120 systems, eight pre-warp civilizations and two ancients. Other civilizations continue their normal simulation.

| Seed / player species | Warp day | First colony day | Full surveys needed | Ideal active time at 4x / Normal |
| --- | ---: | ---: | ---: | --- |
| 20260908 / Terran | 3941.75 | 3960.25 | 1 | 16.50 / 66.00 minutes |
| 12345 / Terran | 3941.75 | 4098.25 | 7 | 17.08 / 68.30 minutes |
| 1337 / cryogenic hydrocarbon | 4244.00 | 4260.25 | 1 | 17.75 / 71.00 minutes |

Seed 20260908 was repeated and produced identical milestone timing, Industry, population and settlement. Wall-clock equivalents use the existing clock's 1 day/second Normal and 4 days/second Maximum. They exclude pauses, choosing commands and frame throttling; they are lower bounds on a player's session time.

## Minimum acceptance route and weak points

1. Run construction and research together. The tested useful construction order is Research Network, Industrial Automation, Orbital Launch Complex, Orbital Shipyard, Warp Test Facility. The first two improve throughput; the other three satisfy ship/research prerequisites.
2. Research Fusion Propulsion, Deep-Space Sensor Networks, Orbital Industry, Exotic Field Theory, Warp Field Control, Prototype Warp Drive. Orbital Industry waits for the Launch Complex; Prototype Warp waits for the Test Facility.
3. Order one Pathfinder Scout, Deep-Space Science Vessel and Interstellar Colony Ship. The colony order reserves exactly 250 million real inhabitants; completed ships contain actual crew/passenger species. Prerequisite rejection is exercised before progression.
4. Use the observer-safe mission plan to send the scout and science vessel to survey targets. Scout reconnaissance cannot substitute for a completed science survey. Continue surveying until the real colony opportunity plan exposes an orderable exact body, then issue that exact colony-fleet order.
5. Found the second colony on the commanded body, transfer all 250 million passengers with their species unchanged, and consume the physical colony ship. This is the automated demo acceptance milestone.

The main weakness is time before meaningful space interaction, not a logistics deadlock. For the Terran fixture, Warp Field Control lasts 918.75 days (3.83 minutes at 4x), followed by Prototype Warp's 1296.75 days (5.40 minutes at 4x). All five existing construction projects are finished by day 2645, leaving the final research wait with no new construction choices. Cryogenic Prototype Warp takes 1464 days (6.10 minutes at 4x). After this wait, accumulated Industry completes all three queued ships in just 0.75 simulated days. Approximately 14,000 Industry remains at settlement. Population stays positive and comfortably exceeds the colony reservation requirement; science stays finite and nonnegative. A different seed can require several survey missions before a suitable unoccupied body appears.

For this milestone, prioritize a reliable short demo opening, visible next prerequisites and progress/ETA, and proving the whole loop in the UI. Seed 20260908 supplies a reproducible first settlement after one survey; selecting a demo scenario or changing progression timing belongs to Core/product tuning. Do not add freight, endurance, repair allocation, new resource types, new labs or broad economy rebalance to solve the demonstrated waiting problem. Existing provisional operational reach remains explicitly provisional and imposed no hidden support block in these runs.

The live campaign still uses the established six-technology `ResearchSimulation`. Adaptive Research is not yet the campaign authority; unfinished Adaptive Research expansion is not a dependency of this demo route.

## Maintained validation and local evidence

`tests/Game.CoreRuntime.Validation/DemoProgressionValidation.cs` is registered in the existing Core runtime suite's caught test list, with a bounded 6000-day failure and state diagnostics. It checks the actual first-colony loop and conservation boundaries instead of pre-completing prerequisite state. Repository-supported invocation from the root is:

```text
dotnet run --project tests/Game.CoreRuntime.Validation/Game.CoreRuntime.Validation.csproj -p:UseAppHost=false
```

Full Godot project restore remains unavailable locally. The new test was additionally compiled with all actual `src/Game/Simulation` source files into a package-free temporary managed console project with `UseAppHost=false`; no simulation stubs were used. Four seeded executions (three unique seeds plus deterministic repeat) passed with exit 0 in one caught `dotnet` process. Existing nullable warnings in colonization/exploration remain. This is source-level progression evidence, not a Godot UI or release-build pass.

Local recovery evidence is in `work/demo-economy-checks/` outside the repository: `DemoChecks.csproj`, caught `Program.cs`, `compile.log` and `progression.log`. The run used `dotnet <work>/demo-economy-checks/bin/Debug/net8.0/DemoChecks.dll 20260908 12345 1337 20260908` with the economy repository root as working directory. No standalone apphost, automatic retry loop or background task was created. Temporary build artifacts remain outside Git; only the maintained test and this handoff are proposed for integration.

## Optional playable demo implementation

Core explicitly prioritized shortening the demonstrated opening wait. The menu now offers **Play Demo — guided 24x opening** with seed 20260908 and unchanged campaign generation, costs, production and prerequisites. **Continue Demo** loads the separate `saves/demo-autosave.json` primary or backup and resumes demo guidance/24x by explicit slot choice. It does not infer mode from the seed or change the save format. Normal New Game and its 1–4x speeds remain unchanged.

Demo guidance is first in the existing scrolling sidebar, above commands. It shows the current objective, next available research/construction action, and production-based approximate ETA. The player still chooses and issues every ordinary research, construction, ship and mission command. The separate Resume Demo at 24x control restores accelerated time after choosing a slower speed or pausing.

At demo speed, each frame accepts at most one simulated day and executes at most four substeps of at most 0.25 days. Diplomacy processes each resolved substep at its own accepted day; autosave occurs after the completed frame. Backlog is capped at two days, so a suspended/stalled frame cannot schedule an unbounded later catch-up. Normal frame timing retains its existing path. Demo autosave cadence is 720 days (~30 real seconds at 24x), with a 48-day failure backoff; normal autosave remains 30 days.

New Campaign and restarting Play Demo use a shared confirmation dialog and checkpoint the current slot before switching. The normal campaign slot is never used for demo checkpoints. The menu remembers its prior speed, including demo24x, and exposes an actual overlay/modal input-blocking property for Core's keyboard/pointer guards. Core wires the New Game toolbar/N shortcut through `MainMenuLayer.RequestNewCampaign`, and Menu/Escape through `ShowMenu`. Save-on-exit failure now cancels exit and keeps the campaign open; the tree has automatic quit acceptance disabled.

Additional maintained tests cover ordinary demo starting state, objectives/ETA, normal speed preservation, paused/time-conserving bounded substeps, one-hour stalled-frame bounds, separate-slot save/resume/restart and backup recovery, and unchanged normal-save bytes. The full legitimate campaign route also runs through the matched live-style strategic AI/Diplomacy/Combat composition and the new 60-frame/second 24x budget: first settlement at **164.98 active seconds** (day3959.6), leaving time for decisions and pauses within the targeted 3–5 minute opening. This is a deterministic simulation timing result, not a measured human play session.

Source-linked managed checks passed with exit0; evidence is `work/demo-economy-checks/demo-compile.log` and `demo-run.log`. Full Godot assembly/runtime and visual interaction verification remain Core/Testing gates. No standalone apphost or background retry loop was created.
