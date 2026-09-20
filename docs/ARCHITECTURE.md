<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> Godot/C#/.NET references below are legacy implementation or fixture provenance,
> not the current runtime or instructions to restore it.
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Architecture

## Core rule

The galaxy simulation is not the Godot scene tree. Godot owns presentation, input, audio, rendering, and platform integration. The simulation is plain C# data and services so it can be stress-tested, profiled, serialized, and eventually moved to native code selectively if profiling proves that necessary.

## Layers

1. **Simulation** — deterministic time, galaxy generation, civilizations, fleets, colonies, economy, diplomacy, knowledge, history.
2. **Decision AI** — consumes only each civilization's current knowledge snapshot. It never receives authoritative enemy state behind fog of war.
3. **Presentation** — renders whatever the player is currently allowed to see.
4. **Diagnostics** — bounded event history, performance counters, system information, and exportable support bundles.
5. **Persistence** — campaign saves, autosaves, dormant-galaxy strategic snapshots, and versioned migrations.

## Performance principles

- No per-population-person simulation; populations are aggregated.
- Fleets make strategic decisions as fleets rather than giving every ship a full empire-level AI.
- Different subsystems tick at different frequencies.
- Expensive work is scheduled and cached instead of repeated every frame.
- Caches are bounded or explicitly invalidated.
- Maximum game speed is constrained by sustainable simulation throughput.
- Distant/departed galaxies use compressed strategic simulation rather than full-detail simulation.
- Logs and telemetry are bounded so diagnostics cannot cause late-game degradation.

## Fair AI contract

AI civilizations can reason from:

- direct sensor observations
- remembered observations
- uncertainty ranges
- intelligence reports
- treaty/trade information they legitimately possess
- known historical events

They cannot query hidden player fleets, exact unseen colony state, unknown technology, or other authoritative information that would violate fog of war.

## Public repository and hidden content

The project is intentionally public. Public design documents describe the framework for undocumented discoveries but do not enumerate secret triggers, exact probabilities, complete artifact chains, or rare hidden outcomes. Those are intentionally withheld from public documentation so discovery remains meaningful.
