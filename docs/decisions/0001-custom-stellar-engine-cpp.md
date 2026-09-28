# ADR 0001 — Custom Stellar Engine and C++ runtime

Status: **Accepted architecture constraint**, recorded 2026-09-20.
This records the user's existing decision and the code now present; it does not
invent a historical migration completion date.

## Context

Older README/handoff files still described Godot, C# and .NET. The active native
branch contains `stellar_engine`, `stellar_core`, a C++ native client, C++
headless runner, CMake tests, asset cooker and Windows maintenance executables.
Stale entry documents could send a new developer back to the abandoned runtime.

## Decision

Stellar Continuum uses custom **Stellar Engine**, **C++ engine code** and
**C++ game code**. CMake currently requires C++23. SDL3 and Windows APIs supply
platform services; SDL is not the game-rule engine. Core remains authoritative,
Engine owns reusable capabilities and App owns presentation/adapters.

Godot/C#/.NET is **deprecated as the game runtime**. Do not migrate back or
introduce Unity/Unreal. Retained legacy code and fixtures may be read for parity
and migration evidence; optional historical generators do not make .NET a
native-game requirement.

## Consequences

- Missing reusable capability is implemented in Stellar Engine in C++, tested,
  integrated and documented; no fake feature or duplicate UI simulation.
- Native build, save compatibility and actual renderer evidence govern claims.
- `AGENT_HANDOFF.md`, `PROJECT_STATE.md` and `ENGINE_ARCHITECTURE.md` are the
  current entry points. Historical records carry explicit supersession notices.
- Migration history does not prove complete feature parity, all-platform support,
  late-game scalability or a production-ready release.
