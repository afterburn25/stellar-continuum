# Adaptive Research starting-profile validation

Gate 052 ports the initialization-only behavior of
`AdaptiveResearchStartingProfiles.cs`. It composes historical fragments into an
owned civilization state, deferred competence/institution/tacit seeds, and the
one-time basic-science horizon events. It does not apply deferred expertise,
create campaign saves, or precompute a civilization's future research tree.

The retained actual-source generator is
`tests/Stellar.AdaptiveResearchStartingProfiles.ParityGenerator/Program.cs`. It
loads the real C# runtime and composer from `Game.csproj`, pins invariant
culture, and records all eight canonical reference profiles. Each row contains
the complete supported state, deferred values, initial events, and SHA-256
fingerprints immediately before and after the production call. The fixture also
contains 42 source boundary rows for ordered loader and shape failures, duplicate
definitions and dictionary keys, checked institution aggregation, first-wins
capability/evidence duplication, mixed error ordering, optional wrong-type/null
behavior, Unicode whitespace, named nonfinite JSON rejection, unknown
references, and public argument validation.

For malformed cases, the fixture retains each changed relative path and exact
changed UTF-8 bytes. The native consumer applies those bytes to a random,
exclusively-created directory under the system temporary directory and matches
the retained before/after directory fingerprints. It resolves and validates the
owned scratch parent before cleanup, rejects escaping changed paths, and never
removes a pre-existing directory. Exact mapped source categories and semantic or
shape messages are compared. The bare `NaN` parser diagnostic is compared by
category because System.Text.Json and nlohmann report different locations and
wording.

Duplicate-key rejection is scoped to dictionary-shaped source inputs; a
retained duplicate scalar case verifies last-property behavior. Dictionary-key
duplicates are detected during native parsing, so in a synthetic payload that
also contains an earlier semantic catalog or fragment fault, that parser check
can preempt the source's later dictionary construction. The fixture establishes
the supported individual failures and mixed composition validation order rather
than universal precedence for multiply malformed JSON.

Definitions load in index file order and preserve fragment, profile, map, merge,
and stable order. Only initial node-state writes use a graph-depth-sorted view;
applicability and mature effects retain original composed seed order.
Institution output uses UTF-16 ordinal order, including supplementary-plane
identifiers. Pressures and competence use the source `Math.Max`/`Math.Min` NaN
behavior, and institution aggregation is checked for Int32 overflow. Mature
effects enable deployment IDs in initial state and remain distinct from later
runtime permission events. Tacit assets remain deferred values.

Regenerate the maintained fixture from the repository root:

```powershell
dotnet run --project tests/Stellar.AdaptiveResearchStartingProfiles.ParityGenerator/Stellar.AdaptiveResearchStartingProfiles.ParityGenerator.csproj -c Release -- data/research/v1 native-tests/fixtures/adaptive-research-starting-profiles.json
```

The accepted isolated Debug and Release builds compiled the maintained native
source paths with `/W4 /WX /permissive- /fp:precise`. Object and program database
outputs used explicit `/Fo` and `/Fd` paths under the ignored gate directory.
Both configurations replayed all 50 actual-source rows. The fixture SHA-256 is
`A70BD6F1EA041EF55BC1E571B77179EF19031C25886BEB85EF5ECA913644049A`;
the canonical research-directory fingerprint is
`2E76D70C9E270E69DF4BFC66B4135419720A0BA5CCA0B2D61DD5906FA22975B1`.

`AdaptiveResearchStartingProfileComposer` is move-only and borrows a stable
`AdaptiveResearchRuntime`; the runtime must outlive it and remain unmoved.
Returned ID spans borrow the composer. Composition results own their state and
DTO values; the native test mutates a deep copy and verifies the original remains
unchanged.

The maintained focused CTest invocation passed 1/1 after integration (`work/native-052-starting-focused.log`). Its target is `stellar_research_start_tests`: the longer original target exceeded Windows object/module-map path limits in this checkout. The short target changes build output paths only. The combined suite will run after the following authority/recovery integration.

The exact committed engine 0.1.24 export (`febf1d7cb42ecd52b017c861376737ed9c0ea183`) subsequently passed the combined 61/61 CTest and 20/20 Python suite with `sourceDirty: false`, seven sealed runtime files and relocated launch/campaign validation. See `work/native-024-clean.log` and HANDOFF.md for the exact package.
