# Stellar Engine Windows export

Engine 0.1.47 adds a native shipyard workspace with canonical readiness, timed orders and confirmed cancellation refunds, plus source-currency research presentation. Fleet selection and canonical travel remain integrated. Export validates actual map, funded-research and fleet-order save/load flows using isolated files and a restricted runtime path. The fleet input is a maintained actual-source 20-system Player17 fixture; fresh/research runs use 500 systems. Test inputs are not shipped in the package. This remains an incomplete C++23 client; the preserved Godot game is the full playable reference. Neither native executable requires Godot or .NET. See [client scope](NATIVE_CLIENT_VALIDATION.md), [research](NATIVE_RESEARCH.md) and [fleets](NATIVE_FLEETS.md) and [shipyard](NATIVE_SHIPYARD.md).

## Developer setup

Windows x64; Visual Studio 2022 Build Tools (Desktop development with C++, x64 compiler and Windows SDK), Python 3.12+, CMake 3.28+ and Ninja. Tested locally with MSVC 19.44.35228, CMake 4.4.3 and Ninja 1.13.2. The exporter locates a registered C++ toolchain through vswhere and configures its environment; an incomplete Visual Studio installation is not a valid compiler.

From repository root:

```powershell
python -m venv .tools/build-tools
.tools/build-tools/Scripts/python.exe -m pip install cmake==4.4.3 ninja==1.13.2
python tools/stellar-export/stellar.py build windows-testing
python tools/stellar-export/stellar.py export windows-headless
python tools/stellar-export/stellar.py export windows-benchmark
```

The native build also supports standard CMake configure/build/CTest presets from an x64 developer prompt. `--fresh` refreshes compiler detection so a stale/incomplete toolchain cache cannot silently persist. Configuration takes place in ignored `build-native/`; no existing game project is overwritten. Cold compilation has a 1,800-second limit and the complete CI job is bounded to 45 minutes; see [the measured build budget](NATIVE_BUILD_BUDGET.md). Configuration, test and runtime-check deadlines remain unchanged.

| Export preset | Current behavior |
| --- | --- |
| windows-development | Debug native foundation; application PDB included |
| windows-testing | RelWithDebInfo foundation; runtime package excludes symbols |
| windows-native-preview | RelWithDebInfo native galaxy preview plus headless diagnostics; pinned SDL3 plus declared UI font and licenses included; installed Vulkan driver required |
| windows-headless | Release foundation; runtime package excludes symbols |
| windows-benchmark | Release foundation, catalog/founding/fresh initialization, and retained legacy campaign-step benchmarks; no rendering/FPS claim |
| windows-release | Fails with an explicit graphical-parity explanation; never substitutes headless output for a playable game |
| windows-steam | Deferred and explicitly rejected |

Presets live in `export/stellar-presets.json`; CMake configurations live in `CMakePresets.json`. Each successful export produces a unique directory and ZIP under `Builds/Windows`, plus a validation JSON sidecar. The directory contains `stellar-continuum.exe`, `Configuration/runtime-config.json`, a README and `build-manifest.json`. The configuration currently describes the host/version; scenario options are command-line controlled. There is no native video/settings subsystem yet.

## Package guarantees and validation

The runtime links the C++ runtime statically and only imports allowlisted Windows system DLLs. PE machine type must be AMD64. No Godot, .NET, Python, compiler, CMake, Ninja or Vulkan SDK is needed by this native executable. Non-system dependencies cause export failure until deliberately supported and packaged; the exporter does not copy arbitrary developer DLL directories.

Version resources embed game/engine versions and source commit. The manifest records source commit, dirty state, configuration, UTC build ID, content version, mode, architecture, runtime imports and SHA-256/size of every runtime file. Explicit fields state gameplay parity is false. Headless packages require no graphics assets; the native preview declares its required font and license. No custom precompiled shaders are required yet. Only selected runtime files are copied. Manifest checks reject tampering, omitted/extra files, duplicate/escaping paths, source/object files and public symbols. Validate an existing directory with:

```powershell
python tools/stellar-export/stellar.py validate Builds/Windows/<export-directory>
```

Build/export runs CTest and Python integrity/recovery checks. Pure client research/fleet/layout/input tests also run in the headless CI configuration. Native-preview export additionally performs six real graphical runs: map save/load, research start/advance/save/load and fleet order/advance/save/load. Complete paused Player17 recaptures must match. Research requires positive funded progress; fleets require a newly issued owned route, positive saved transit progress and advancing simulation time. Then an independent copy launches in a temporary folder with a Windows-system-only PATH. It creates and restores a foundation checkpoint, resolves the packaged astronomy catalog, initializes a fresh campaign, and advances a retained campaign simulation while checking its deterministic diagnostic state. Failures produce a terminal error and nonzero status; partial output is marked `EXPORT_FAILED.txt` and is not zipped as validated.

This relocated test is **not clean-machine certification**: it runs on the development machine. A separate Windows VM/device without development tools remains a required release gate. Hashes detect integrity changes; they are not a digital signature/authenticity guarantee.

## Physical catalog use

```powershell
.\\stellar-continuum.exe --headless --generate-galaxy --systems 500 --seed 8374837 --repeat 1 --catalog-output galaxy.json
```

Supported sizes are 250, 500, 1000, and 2500. Add `--plan-homes` for the default seven-faction homeworld preview. Output remains physical/planning data before the full civilization seeder; it is not a campaign or game-save-v16. Packages embed astronomy JSON/README and dependency licenses; relocated restricted-PATH validation resolves assets beside the executable.

Use `--found-civilizations --civilizations 6 --ancients 1 --player-species terran_baseline` to emit the founding catalog before colonies. Use `--seed-colonies` for colony seeding before fleets; only this mode includes authoritative `constructionStates`, together with the economic projection derived from those states and additive surface-support previews. The separate simulation command below owns retained stepping. Save-v16 and graphics/UI/audio remain open; funded economy, biology, demographics, logistics, currency, and construction library ports are explicit projections.

## Headless use

```powershell
.\stellar-continuum.exe --version
.\stellar-continuum.exe --headless --systems 500 --ticks 100 --workers 4
.\stellar-continuum.exe --headless --ticks 5 --save first.scf
.\stellar-continuum.exe --headless --ticks 5 --load first.scf --save second.scf
```

Foundation checkpoints are versioned/checksummed synthetic distance scenarios. They deliberately reject game save-v16, malformed/truncated data and existing output paths. They are not campaign saves. An existing file is preserved; choose a new output path. Pending writes are retained for diagnosis. CLI failures report exception type, message, engine/source and working directory.

Benchmark reports keep foundation distance, catalog/founding/fresh initialization, and campaign-step timings separate. Campaign results measure actual ordered `Advance` calls and exclude rendering. They do not provide FPS, save-v16, Adaptive Research, diplomacy, or complete-game performance claims. Clean engine 0.1.22 passed 56/56 CTest and 20/20 Python checks at commit `cca07b249fdd3631a225758507fa4fd4d506b2e5`; exact evidence is in `work/native-022-clean.log` and its packaged validation report. All six remote workflows passed. The following 0.1.23 source checkpoint adds three maintained research parity tests; its exact committed export is a separate validation step. Engine 0.1.20 development export passed 50/50 CTest and 20/20 Python checks and relocated execution, but its manifest correctly recorded a generated untracked PDB as `sourceDirty: true`. It is not a clean package; see `HANDOFF.md` for exact evidence and the separate remote compilation timeout.

## Remaining graphical release gates

1. SDL3/window/input and Vulkan renderer parity, then UI/audio adapters, reviewed licenses and explicit runtime dependency collection.
2. Asset-ID preserving runtime packs, texture/audio conversion and precompiled shader deployment; validate missing assets/shaders before player launch.
3. Real campaign/save-v16 migration and full game-loop regression checks.
4. Branded executable icon, graphical startup, settings defaults, crash dumps/build correlation and separate developer symbols.
5. Exported visual/input/audio/save smoke tests plus separate clean Windows machine launch and realistic CPU/GPU/memory benchmarks.
6. Only then enable `windows-release` and consider old-engine removal. Linux/macOS exports remain future backends; Core and Engine foundations contain no Windows gameplay logic.


## Fresh-campaign diagnostic

`--headless --seed-campaign` is a verified diagnostic mode, not a player save. It emits complete seeded campaign state with post-reservation colonies, home systems fully surveyed, nearby detections, and hidden core knowledge. It adds whole-initialization benchmarks; prior preview timings remain distinct.

## Retained campaign simulation diagnostic

```powershell
.\stellar-continuum.exe --headless --simulate-campaign --systems 500 --ticks 40 --step-days 0.25 --repeat 1 --catalog-output simulated.json
```

Each repeat creates a fresh campaign and retains one campaign state and one
coordinator across every requested step. Initialization timing is reported
separately from actual step mean, nearest-rank p95, and peak time. The report
includes total simulated days, allocation and event-row counts, and a timing-free
final-state digest. Repeats must produce byte-identical final diagnostics.

The optional output is `stellar-campaign-simulation-diagnostic-v1`, not a
player save. The command covers the ordered legacy coordinator phases currently
implemented in Core. It does not claim integrated Adaptive Research, diplomacy,
all campaign commands, rendering, audio, UI, or full gameplay parity.
