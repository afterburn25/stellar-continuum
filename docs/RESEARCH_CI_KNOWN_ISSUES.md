# Adaptive Research CI — Known Shared Issues

This file records shared validation limitations discovered while running Adaptive Research CI. It does not transfer ownership of non-research gameplay/Testing code to this workstream.

## Godot runtime smoke can false-positive on script instantiation error

Tracked as **GitHub issue #61 — CI runtime smoke is false-positive on Main.cs C# instantiation error**.

Discovered from GitHub Actions run `34163593221`, job `101870178023`.

The .NET build completed successfully, but the Godot runtime smoke command logged:

```text
ERROR: Cannot instantiate C# script because the associated class could not be found. Script: 'res://src/Game/Presentation/Main.cs'. Make sure the script exists and contains a class definition with a name that matches the filename of the script exactly (it's case-sensitive).
```

The Godot process nevertheless exited with a success status, so GitHub marked the runtime smoke step successful.

### Consequence

Until issue #61 is fixed by the appropriate Testing/Release/gameplay owner, Adaptive Research PRs may report that the **runtime smoke process step completed**, but must not claim the Godot runtime is semantically clean solely from that step.

Research-owned acceptance can still rely on:

- Adaptive Research validators/benchmarks;
- .NET restore/build;
- research-only changed-file audit;
- Godot process-level editor/runtime invocation as supplemental information, with the known false-positive caveat.

### Ownership boundary

Adaptive Research does not own `Main.cs` or the shared gameplay/runtime smoke implementation. No gameplay-source fix should be made from `dev/adaptive-research` merely to close issue #61.

Recommended Testing/Release gate behavior:

- capture/runtime-test Godot output;
- fail when the project cannot instantiate its main C# script/main scene;
- ideally assert that the expected main scene/class actually initialized rather than relying only on process exit code;
- distinguish benign shutdown/editor warnings from real startup/runtime engine errors.
