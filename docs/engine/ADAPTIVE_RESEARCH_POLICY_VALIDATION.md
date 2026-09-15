# Adaptive Research facilities and progress policy validation

This draft ports the two retained C# immutable loaders. It consumes the public
Gate 042 catalog by const reference only during loading, then owns ordered
capability, institution, requirement, stage and readiness definitions. Public
views borrow immutable owned storage; the catalog itself is move-only. Lookup
pointers remain valid until the owning catalog is moved or destroyed. Institution
and stage-requirement lookups scan owned vectors, so their `noexcept` contract
does not hide a temporary `std::string` allocation.

The facility loader deliberately collects all declared capabilities from every
listed file before it validates institutions and stage requirements. The progress
policy preserves the C# `0.000001` stage/readiness validation thresholds and its
readiness lookup behavior, including `Math.Clamp`/NaN behavior. Readiness and
stage validation use stable ordering, matching LINQ `OrderBy` for equal keys.
Strict JSON cannot directly contain NaN. A JSON number outside the native
parser's finite-double range is a parser-boundary difference: nlohmann rejects
it while `JsonElement.GetDouble` can produce infinity before source validation,
so the draft makes no semantic parity claim for huge-exponent JSON.

`../research-policy-oracle-043` is intentionally outside the repository. It
uses the actual retained C# loaders with invariant cultures and writes
`adaptive-research-policy.json`. Its malformed rows mutate temporary copies,
never `data/research/v1`. `policy_parity.cpp` consumes the actual fixture and
compares complete projected canonical definitions and query results with exact
integers/order and a 1e-12 absolute floating tolerance. The 28 source rows include
2,969 direct facility queries across every catalog node and all eight maturity
values, unknown node/enum queries, empty capability and institution IDs, duplicate
institutions and stage requirements, named invalid stage-band lookups, and finite
negative/fractional/scientific numeric diagnostics. Native semantic failures map
source `InvalidDataException`/`InvalidOperationException` to the loader-specific
native exception class and compare exact messages. Missing-file, malformed-JSON,
and JSON-type rows verify their source categories and separate concrete native
boundary diagnostics. Every row records an FNV-1a byte fingerprint of its complete
top-level input directory immediately before and after the production call. The
native replay compares both fingerprints outside the exception boundary, proving
that it received the same bytes as the source call and did not mutate them.

The fixture is regenerated from the retained source with invariant culture. Its
reviewed SHA-256 is `BC5DA13A05BE749E42B0DA4CC9F325360C4E9537AC255615402618A56EDF5BEC`.
Both Debug and Release parity builds compile the three public headers
together with `/W4 /WX`; each run reports 28 source rows, 2,969 facility queries,
and 370 catalog nodes.

The retained Windows standalone Debug reproduction must direct both compiler and
linker databases into the ignored build directory:

```powershell
New-Item -ItemType Directory -Force build-native/policy-parity-debug | Out-Null
cl /nologo /std:c++latest /EHsc /MDd /Od /Zi /W4 /WX /Ithird_party /Icore/include /c core/src/adaptive_research_catalog.cpp core/src/adaptive_research_facilities.cpp core/src/adaptive_research_progress_policy.cpp native-tests/adaptive_research_policy_tests.cpp /Fobuild-native/policy-parity-debug/ /Fdbuild-native/policy-parity-debug/vc140.pdb
link /nologo /debug /out:build-native/policy-parity-debug/adaptive_research_policy_tests.exe /pdb:build-native/policy-parity-debug/adaptive_research_policy_tests.pdb build-native/policy-parity-debug/adaptive_research_catalog.obj build-native/policy-parity-debug/adaptive_research_facilities.obj build-native/policy-parity-debug/adaptive_research_progress_policy.obj build-native/policy-parity-debug/adaptive_research_policy_tests.obj
```
