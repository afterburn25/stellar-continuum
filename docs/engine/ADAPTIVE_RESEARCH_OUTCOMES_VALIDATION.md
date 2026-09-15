# Adaptive Research outcomes validation

Gate 058 ports `AdaptiveResearchOutcomeDefinitions.cs`,
`AdaptiveResearchOutcomeState.cs`, and `AdaptiveResearchOutcomeRuntime.cs`.
It does not include outcome snapshots, campaign persistence, or rules from other
research subsystems.

## Retained source evidence

The managed generator at
`tests/Stellar.AdaptiveResearchOutcomes.ParityGenerator/Stellar.AdaptiveResearchOutcomes.ParityGenerator.csproj`
loads the production C# runtime through `Game.csproj`. It produced 82 actual-source
rows against `data/research/v1` with canonical input fingerprint
`2E76D70C9E270E69DF4BFC66B4135419720A0BA5CCA0B2D61DD5906FA22975B1`.

The rows cover the canonical policy and load-time side-discovery index, five
portable SHA-256 vectors including a multi-block vector and UTF-8, all nine
planned and applied outcomes, readiness adjustment, side-discovery slot wrap,
operation without a pressure runtime, named non-finite years, public rejection
paths, Unicode whitespace, outcome-state ordering and bounded history, negative
history limits and partial failure, invalid enum categories, and 32 retained
exact-byte malformed catalog cases. Every production call records the research
directory fingerprint before and after it.

Fixture SHA-256:
`DF9B002792450465A6437B1A08D9FEB4647F27512BBFA53352AB130810A78195`.

Managed generator SHA-256:
`B894BBD9D24BFAB92B25660E975498BFD0DD38C79DE64091320C146AC638B2B9`.

Generate the fixture from the repository root:

```powershell
dotnet run --project tests/Stellar.AdaptiveResearchOutcomes.ParityGenerator/Stellar.AdaptiveResearchOutcomes.ParityGenerator.csproj -c Release -- data/research/v1 native-tests/fixtures/adaptive-research-outcomes.json
```

## Native strict replay

The native harness invokes only typed production calls inside expected-error
catches, projects owned results after each catch, applies the exact retained
malformed bytes to exclusively owned scratch directories, and verifies matching
source fingerprints before and after each load. It also proves temporary
dependency rejection, runtime and catalog moves, state-copy independence, weak
identity cleanup, caller-aliased string safety, portable SHA vectors, and guarded
integer overflow.

Run both configurations from the repository root:

```powershell
python work\058-research-outcomes\build_strict.py debug
python work\058-research-outcomes\build_strict.py release
```

The script contains all `.obj`, executable, and PDB output under the ignored
gate directory by passing explicit `/Fo` and `/Fd` paths.

On 2026-09-13, both strict `/W4 /WX /permissive- /fp:precise` configurations
compiled, linked, and reported `validated 82 actual-source Outcome rows` with
exit code 0. The retained generator was also invoked through `dotnet run` with
no arguments and with a missing research root. Both runs printed the complete
exception and stack followed by `cwd`, `research-root`, and `fixture`, and both
exited with code 1 without opening a graphical error dialog.

## Boundaries

Semantic catalog and runtime errors compare exact mapped source categories and
messages. JSON reader/type/number diagnostics compare typed categories because
the parser wording is platform-specific. The native JSON parser rejects the
token `1e400` as a numeric-format overflow before materialization; System.Text.Json
materializes positive infinity and the source loader then reports the semantic
invalid-weight error. That retained row proves both safe rejections and records
the category difference explicitly.

Duplicate keys are rejected only in source dictionary-shaped policy objects.
As with the source parser, malformed combinations containing duplicate dictionary
keys can fail before later semantic validation; universal ordering across mixed
malformed faults is not claimed.

The source performs unchecked arithmetic for attempt, counter, and sequence
increments. Native controlled-writer entry points reject overflow before mutation
to avoid undefined behavior. Negative `maxPerNodeRecent` and negative
`maxRecent` retain the observed source behavior, including the latter's summary
and sequence mutation before its terminal range error.

Maintained target: `stellar_research_outcome_tests`; CTest: `adaptive_research_outcomes_parity`. Production and fixture hashing use portable SHA-256; no Windows-only API is required. Combined engine0.1.27 validation passed 67/67 CTest and 20/20 Python checks (`work/native-027-testing.log`).
