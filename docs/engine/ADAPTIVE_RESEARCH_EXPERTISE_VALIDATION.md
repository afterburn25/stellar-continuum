# Gate 050 Adaptive Research expertise foundation validation

This gate ports the immutable expertise catalog and its separate sparse state
sidecar. It does not attach expertise to the primary research state and does not
include readiness, service, authority, runtime, or snapshot integration.

The actual C# generator runs under invariant current and UI culture. It records
the complete canonical catalog and runtime policy, four competence-vector query
results, and 35 sequential state commands with complete state before and after
each invocation. The state sequence covers missing-field defaults, timestamps
including nonfinite values, replacement and no-op revision behavior, multiple
removals and LIFO slot reuse, institution conditional removal and inactive
archetype lookup, tacit replacement/removal, validation order, null/empty
contexts, and accepted or rejected nonfinite values.

Twenty-six malformed source cases run against independently and exclusively
claimed temporary copies of the canonical data. They cover combined
missing/type checks, array and element kinds, null strings, duplicate parse
order, registered field membership before related references, exact stage and
readiness weight sums, tacit component and duplicate order, institution field
validation, catalog identity, and runtime-policy property ordering. Both source
and native harnesses verify containment and cleanup of their scratch roots.

Native-only probes prove state copies own independent values, moved state remains
usable, and the final safe revision succeeds while the next revision is rejected
before signed overflow. A separate translation unit includes all existing
Adaptive Research public headers with the new headers.

Two source generations are byte-identical at SHA-256
`ABF97AB86A626BC1EAA75CC3B66385C64BFD5AC4C564634B6E63C3D655BFAE96`.
Strict isolated Debug and Release builds use MSVC C++23 `/W4 /WX /permissive-
/fp:precise /utf-8` with explicit `/Fo` and `/Fd` paths and replay the complete
fixture successfully.

The malformed matrix verifies the represented loader validation branches and
their source ordering. It does not claim byte-for-byte parity for every operating
system file error or every low-level JSON parser diagnostic.

```powershell
dotnet run --project tests/Stellar.AdaptiveResearchExpertise.ParityGenerator/Stellar.AdaptiveResearchExpertise.ParityGenerator.csproj -c Release -- data/research/v1 native-tests/fixtures/adaptive-research-expertise.json
python work/050-research-expertise-foundation/build_strict.py debug
python work/050-research-expertise-foundation/build_strict.py release
python work/050-research-expertise-foundation/build_headers.py
.tools/build-tools/Scripts/ctest.exe --test-dir build-native/testing -R '^adaptive_research_expertise_parity$' --output-on-failure
python tools/stellar-export/stellar.py build windows-testing
```

The promoted tree passed the focused expertise parity test and the maintained
Windows validation: 59 of 59 CTest tests and 20 of 20 Python tests passed. A
direct invocation without required arguments returned exit code 1 and reported
the exception type, message, current directory, fixture, and research root.
