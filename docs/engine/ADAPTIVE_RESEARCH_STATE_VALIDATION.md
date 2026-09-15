# Gate 044 Adaptive Research civilization state validation

The actual C# oracle invokes `AdaptiveResearchCivilizationState` internal mutators by pre-resolved reflection. Only `MethodInfo.Invoke` or the requested public query occurs inside each catch; `TargetInvocationException` is unwrapped to its production inner exception. Input construction, state snapshots, result enumeration and JSON encoding occur outside. Culture is invariant.

The retained fixture contains 74 sequential operations and seven constructor failures. Every row records all state owned by this gate before and after: identity, both revisions, directed-program stage, total/assigned/free labs, node states, pressures, evidence, civilization traits, applicability contexts, capability keys, facility capabilities, deployment events and active projects. Expertise is explicitly marked `pending-separate-gate`; it is neither serialized nor represented as an invented empty native value. Outcome, agenda, pressure-support and foreign-technology sidecars and snapshot restore are also outside this state-only gate.

The fixture measures Dictionary and HashSet runtime enumeration through remove/reinsert sequences. Native storage uses reusable sparse slots and mutation-built contiguous read caches, preserving those measured positions while keeping all `noexcept` queries allocation free. A mutation rebuilds only its affected cache; scalar mutations rebuild none. `ApplicabilityContexts` returns an owned snapshot because the source property allocates a dictionary and sorts each trait collection; `GetApplicabilityTraits` retains runtime set order. The property ordering uses decoded UTF-16 code units to match `StringComparer.Ordinal`; an actual private-use/BMP versus supplementary-character case distinguishes it from UTF-8 byte order.

Coverage includes replacement and duplicate/no-op revision rules, pressure clamp and near-equality behavior, nonfinite pressure/node/project values, lab rejection and near equality, null versus empty evidence and capability contexts, contextual evidence matching, locale-independent ASCII ordinal-ignore-case archived resolutions, paused-project exclusion from assigned labs, project summation order, and `MarkViewDirty` changing only the view revision. Constructor and applicability-context validation include actual NBSP and U+2003 whitespace cases.

Native copy construction is tested as a deep owned clone: mutations to the copy do not affect the original. Move ownership is then mutated successfully. These are native value/lifetime guarantees, not claims about C# reference identity. A separate native checked-revision probe accepts `INT64_MAX - 1` and rejects `INT64_MAX` with `Adaptive Research state revision space is exhausted.` before signed overflow.

The independently regenerated source fixture is byte-identical at SHA-256 `45C0B9CD2CE7925AF0002DD37982C66C569CB4F3B0EC79475C168652CF0294EA`. Strict MSVC C++23 Debug and Release compile with `/permissive- /fp:precise /utf-8 /W4 /WX` and explicit `/Fo` and `/Fd` outputs inside ignored draft folders. Integer revisions and enum values compare exactly; finite doubles use relative tolerance `1e-12`; named nonfinite values compare exactly.

The maintained Windows build passed all 53 CTest cases, including `adaptive_research_state_parity`, and all 20 Python recovery and package-integrity tests. The native test catches terminal failures and returns a nonzero exit code.

Regenerate the retained source fixture and run the focused native parity test with:

```powershell
dotnet run --project tests/Stellar.AdaptiveResearchState.ParityGenerator/Stellar.AdaptiveResearchState.ParityGenerator.csproj --configuration Release -- native-tests/fixtures/adaptive-research-state.json
.tools/build-tools/Scripts/ctest.exe --test-dir build-native/testing -R adaptive_research_state_parity --output-on-failure
```
