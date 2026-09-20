# Stellar Continuum

Stellar Continuum is a space civilization strategy game built with **custom
Stellar Engine**, **C++ engine code** and **C++ game code** (C++23).
The current native development branch is `cpp/codex-native-architecture-integration`.
Godot/C# is the deprecated predecessor, not the current runtime.

**Start with [the agent handoff](docs/AGENT_HANDOFF.md).** This is an unfinished
Windows x64 development build. See [current build/test results](docs/validation/2026-09-20-development-sync.md)
before treating it as a release baseline.

## Build and run

Use Git LFS, Visual Studio 2022 C++ Build Tools + Windows SDK, CMake 3.28+ and
Ninja. Run in an x64 VS developer command prompt:

```bat
git lfs pull
cmake --preset windows-native-preview
cmake --build --preset windows-native-preview --parallel 4
ctest --preset windows-native-preview --parallel 2 --output-on-failure
build-native\preview\stellar-continuum-native.exe --dev-game
```

The SDL3 dependency is downloaded from its pinned, hash-verified archive. A
working Vulkan device is needed for graphical tests. The installed game does
not need Python, Godot, .NET or a compiler. Required artwork is stored in LFS;
pointer-only checkouts cannot build/package the graphical game.

## Developer documentation

- [Project state](docs/PROJECT_STATE.md)
- [Engine architecture](docs/ENGINE_ARCHITECTURE.md)
- [Engine capability matrix](docs/ENGINE_CAPABILITIES.md)
- [Development progress](docs/DEVELOPMENT_PROGRESS.md)
- [Known issues](docs/KNOWN_ISSUES.md) and [technical debt](docs/TECHNICAL_DEBT.md)
- [Engineering roadmap](docs/ROADMAP.md)
- [Agent handoff](docs/AGENT_HANDOFF.md) and [GitHub workflow](docs/DEVELOPMENT_WORKFLOW.md)
- [Celestial and Sol content](docs/CELESTIAL_CONTENT_STATUS.md)
- [Asset pipeline](docs/ASSET_PIPELINE_STATUS.md) and [Windows installer/updates](docs/WINDOWS_INSTALLER.md)

Historical source and records remain for provenance and compatibility. They do
not override the current C++ architecture. The canonical working title is
Stellar Continuum; commercial naming clearance is still pending ([branding](docs/BRANDING.md)).
