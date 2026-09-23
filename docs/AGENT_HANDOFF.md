# Agent handoff — start here

> **STELLAR CONTINUUM IS NO LONGER A GODOT/C# PROJECT.**
>
> **CURRENT ARCHITECTURE:**
>
> **CUSTOM STELLAR ENGINE**
>
> **C++**
>
> **C++**
>
> **DO NOT MIGRATE BACK TO GODOT OR INTRODUCE UNITY/UNREAL.**

Snapshot: 2026-09-20. Repository: `afterburn25/stellar-continuum`.
Continue from **`cpp/codex-native-architecture-integration`**, not `main` or an
old research/editor branch. The default branch does not represent this native
working build. Engine `0.1.64`; game `0.1.14.2-dev`; source of truth:
[`export/runtime-config.json`](../export/runtime-config.json).

> **`work/foundation-1-30-codex-integration`** merges the
> `engine/foundation-expansion-1-30` engine work (save history, replay,
> input mapping, localization, developer tools, engine libraries) onto this
> line without replacing its renderer/artwork/installer systems. Evidence:
> [foundation-merge receipt](validation/2026-09-20-foundation-merge.md).
> It awaits PR integration into `cpp/codex-native-architecture-integration`.
> Consumer-audit conclusion and final validation:
> [consumer-audit receipt](validation/2026-09-22-consumer-audit.md).

## Read in this order

1. [Project state](PROJECT_STATE.md) and [verification receipt](validation/2026-09-20-development-sync.md).
2. [Architecture](ENGINE_ARCHITECTURE.md) and [capability matrix](ENGINE_CAPABILITIES.md).
3. [Known issues](KNOWN_ISSUES.md), [technical debt](TECHNICAL_DEBT.md), [roadmap](ROADMAP.md).
4. [Celestial/Sol status](CELESTIAL_CONTENT_STATUS.md), [asset pipeline](ASSET_PIPELINE_STATUS.md), [installer](WINDOWS_INSTALLER.md).
5. [Developer QA](DEVELOPER_QA.md), [workstream policy](DEVELOPMENT_WORKFLOW.md), and root [AGENTS.md](../AGENTS.md).

## Reproduce the native build

Install Git with Git LFS, Visual Studio 2022 Build Tools with Desktop C++ and a
Windows SDK, CMake 3.28+, Ninja and Python 3.12+ for tooling. Actual audit compiler
and Python version are recorded in the receipt. A working Vulkan device is
required for real graphics tests. Ordinary builds use embedded shaders and do
not require a shader SDK; regeneration requires the pinned shader toolchain.

In an **x64 Native Tools Command Prompt for VS 2022**:

```bat
git clone --branch cpp/codex-native-architecture-integration https://github.com/afterburn25/stellar-continuum.git
cd stellar-continuum
git lfs pull
cmake --preset windows-native-preview
cmake --build --preset windows-native-preview --parallel 4
ctest --preset windows-native-preview --parallel 2 --output-on-failure
set STELLAR_NATIVE_EXE=%CD%\build-native\preview\stellar-continuum.exe
python -m unittest discover -s tools/stellar-export -p "test_*.py"
```

The normal graphical development preset is `windows-native-preview`
(RelWithDebInfo, tests enabled). `windows-development` is Debug **without** the
graphical client by default. `windows-testing` and `windows-headless` provide
non-graphical configurations, but Windows-only image/tool tests can still exist.
Do not run configure and build simultaneously against one build directory.
The CMake file grants `/bigobj` specifically to the large persistence test.

```bat
build-native\preview\stellar-continuum-native.exe --dev-game
build-native\preview\stellar-continuum.exe --help
```

Development launch reads copied loose assets. `StellarCooker` and
`tools/build-cooked-game.ps1` construct isolated cooked content. After a full
target release is validated, `tools/build-update-installer.ps1` builds an
exact-base changed-files update. **The user wants update installers for future
downloads**, not repeated full packages. Follow [Windows installer](WINDOWS_INSTALLER.md)
for complete release commands and rollback/repair limitations.

## Current state and next workstream

The native game has real research, colonies/economy, fleets/logistics,
diplomacy, combat, multiple map views and Developer tools. Celestial rendering,
supplied planet/giant/ring/moon art, a faint shared sky, cooked packages and
per-user installation/update/repair are integrated. Current limitations and
failed checks are explicit in [PROJECT_STATE.md](PROJECT_STATE.md); this is an
unfinished development build, not a production-ready certification.

Recent work includes corrected cooked flare memory reservations, persistent
local crash reports, small updates, major Sol moon orbits, canonical appearance,
ice optics, sharper rings, independent flare timing and scoped simulation
lookup/image-memory optimizations. [Progress](DEVELOPMENT_PROGRESS.md) links the
owners and evidence. Do not infer that a historical report describes the latest
behavior when a newer correction supersedes it.

**Standalone engine platform:** `stellar-engine.exe` is the engine-only tools
host (no game module). Its Projects tool drives the full game-project loop:
`engine::EngineProject` manifests (`project.stellar.json`), `create_project`
scaffolding (base package, content dirs, `mods/`, generated consumer
`CMakeLists.txt`, windowed ECS starter `src/main.cpp`), asset import,
generic `scan_content` cooking into project-namespaced packages, and
BUILD/RUN against the exported `engine-sdk/` beside the shell (headers,
prebuilt libs, SDL3 runtime + default font, `stellar::engine`/`stellar::cooker`/
`stellar::platform` consumer targets). `stellar-editor.exe` is the separate
authoritative-world editor (galaxy/system/body workspaces, annotations,
undo, atomic project documents). Both are registry rows in
[ENGINE_CAPABILITIES.md](ENGINE_CAPABILITIES.md).

**Recommended next workstream: native validation and release reliability.**
Start from this branch in an isolated checkout; fix the failures recorded in
the receipt before beginning the 30-item engine expansion. Use a descriptive
new branch for that work, e.g. `work/native-validation-reliability`, after
checking whether one already exists. Do not change the default branch or merge
this integration branch to main without explicit integration intent.

## Preserve these contracts

- Core owns gameplay and observer privacy; UI controllers issue validated
  commands and read snapshots. Do not create UI-only research/economy/AI rules.
- `PlanetAppearance`, stable IDs, accepted/rejected pools and source hashes are
  shared authority. Preserve supplied art, cloud policy, axes and tidal locking.
- Keep Sol bodies and moon parents reserved, Pluto/Charon barycentric handling,
  Triton's retrograde inclination and analytic multiple-star hierarchy.
- Separate strategic time from cosmetic rotation/tumble and the persisted
  stellar-activity clock. High game speed must not multiply flare frequency.
- `ImagePreparationQueue`, image byte reservations, GPU resource bounds and
  asset registry validation are correctness controls, not optional warnings.
- Keep Player/Developer save separation, recovery/migration, and maintenance
  locks/transactions. Never use real user saves for destructive tests.
- Preserve the current planetary colony screen; superseded surface-view files
  were intentionally removed. Do not resurrect them from old workflow targets.
- Large artwork is in Git LFS. Original import masters, rejected pixels,
  screenshots, cooked packages, compiler output, saves and credentials are not
  a substitute for source-controlled code and reviewed manifests.

## Engine-first and GitHub communication rules

If a requested feature exposes a reusable engine gap: identify its owner,
extend Stellar Engine in C++, test the capability, integrate Core/App consumers,
and update the capability registry. Do not fake behavior, hardcode a one-off
workaround, duplicate a subsystem or silently omit a requirement.

Each significant workstream needs an appropriate branch, meaningful commits,
updated status/limitations, a decision record when architecture changes, and a
handoff before stopping. GitHub must carry that context; chat is not the project
database. Do not force-push or erase unrelated work. A failed test must remain
visible until its cause and correction are reviewed.
