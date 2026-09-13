# Adaptive Research Pressure parity validation

Gate 055 ports the immutable pressure catalog, sparse pressure support state,
and pressure runtime from the actual C# implementation. It also adds the
private weak state identity used to reproduce `ConditionalWeakTable` ownership
without serializing an address or identity value.

## Source evidence

The retained generator uses invariant culture and invokes the actual C#
catalog, state, and runtime. The fixture contains:

- the complete 59-rule catalog, 152 metric-signal index entries, and 71
  event-signal index entries;
- 15 exact-byte catalog inputs: 14 replayed by native code and one explicitly
  recorded source-only `1e400` parser boundary;
- 12 sequential low-level state operations;
- 20 sequential runtime commands with full core state and pressure support
  before and after every command; and
- same-state/different-runtime and equal-civilization-ID/different-state
  `ConditionalWeakTable` isolation observations.

The catalog cases cover the hardcoded 0.75/0.25 weights and ignored
`metric_target` contents, optional signal arrays, retained duplicate signals,
deduplicated reverse indexes, duplicate rule failure, scalar duplicate-last
behavior, unknown and missing rules, catalog mismatch, invalid decay and
memory floor, invalid runtime policy, and the required `metric_target`
property. Each changed file is retained as validated base64 plus its SHA-256.

The `1e400` response value is accepted as positive infinity by the C# JSON
reader and its domain validation. The native JSON dependency rejects this
number during parsing, so the fixture labels that record `SourceOnly` and the
native test asserts the documented rejection rather than claiming parser
equivalence beyond representable `double` input.

The native duplicate-rule check runs in the JSON parse callback because the
JSON library would otherwise collapse duplicate object keys. On a document
that combines a duplicate rule with an earlier independent semantic error,
this parse-time guard can report the duplicate before the C# loader reaches
that semantic error. This is the same bounded combined-malformed ordering
limit recorded for the retained Gate 052 loader; single-fault loader cases and
the exact duplicate-rule input match the source.

Fixture SHA-256:
`9FE9DDBF4021FD5ADC3E302612861724186A0D565C6B0BE55991CC91913EC894`.

Canonical research-data fingerprint recorded by the managed generator:
`2E76D70C9E270E69DF4BFC66B4135419720A0BA5CCA0B2D61DD5906FA22975B1`.

## Native ownership evidence

The native consumer additionally verifies that civilization-state moves
transfer identity, deep copies and copy assignment create fresh identities,
self-assignment preserves identity, move assignment transfers the incoming
identity and expires the target identity, the expertise sidecar is independently
owned, runtime moves preserve support addresses, separate runtimes remain
isolated, equal civilization IDs remain isolated, expired identities cannot be
recovered after exact `State` object-address reuse, a throwing value factory
does not insert an entry, and bounded maintenance removes expired weak entries.
The identity is absent from snapshots, hashes, and revision counters.

## Reproduction

Generate the source fixture:

```powershell
dotnet run --project tests/Stellar.AdaptiveResearchPressure.ParityGenerator/Stellar.AdaptiveResearchPressure.ParityGenerator.csproj -- data/research/v1 native-tests/fixtures/adaptive-research-pressure.json
```

Run strict native replays:

```powershell
cmake --build build-native/testing --config Release --target stellar_research_pressure_tests
ctest --test-dir build-native/testing -C Release -R adaptive_research_pressure --output-on-failure
```

Both strict C++23 `/W4 /WX` runs completed successfully with:

```text
adaptive_research_pressure_tests: 59 rules, 14 catalog cases plus 1 source-only parser boundary, 12 state operations, and 20 runtime commands plus identity lifetime probes passed
```

The focused strict proof placed object files and PDBs under an ignored scratch
directory. Invoking the managed generator without arguments and with a
missing research root returns exit code 1 with exception type, message, stack,
working directory, research root, and fixture diagnostics. The native consumer
also returns a nonzero exit with type, message, working directory, fixture, and
research-root diagnostics.

Maintained focused verification: `work/native-055-pressure-focused.log`, exit 0.
All four pressure, civilization-state, authority, and expertise-recovery CTests passed.
The first incremental attempt reused an older object because scratch promotion
preserved the old source timestamp; refreshing the changed source/header timestamps
forced the correct rebuild. That failed attempt is retained separately and is not
counted as passing evidence.

Combined integration passed 65/65 CTest and20/20 Python checks in `work/native-026-testing.log`.
