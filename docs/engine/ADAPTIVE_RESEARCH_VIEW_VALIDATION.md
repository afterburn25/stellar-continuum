# Gate 048 Adaptive Research view validation

Gate 048 ports only the read-only `AdaptiveResearchViewBuilder` projection. The
catalog, eligibility evaluator, and progress policy are borrowed immutable
dependencies that outlive and remain unmoved for the builder lifetime. Returned
DTOs own their strings, blockers, and collections. The input civilization state
is borrowed only during `build()` and is compared in full before and after every
source and native invocation.

The source generator runs with invariant current and UI culture and writes all
setup operations, complete source input state, result/error, and complete
post-call state. Its 20 native-parity cases cover an empty state, all eight
maturities, node/edge ordering, both prerequisite relationship kinds, hidden
prerequisite sanitization, pressure privacy and hard targets, stored zero and
named nonfinite values, active and paused projects, active scientific and stage
facility blockers without start/capacity blockers, null/empty/fallback contexts,
progress clamping, readiness boundaries, first-unknown source enumeration order,
and unknown-node versus invalid-stage error order. The C# null-state call remains
one explicitly labeled source-only record because a C++ reference has no null
equivalent.

Two independent source generations are byte-identical at SHA-256
`0C076EDA2C10946C2F9CE1F5BBB6CEF05E334E7E40B15DD821E4DE0749A47B12`.
Strict isolated MSVC C++23 Debug and Release builds passed with `/W4 /WX
/permissive- /fp:precise /utf-8` and explicit ignored `/Fo` and `/Fd` outputs.
The maintained Windows build passed all 57 CTest cases, including the new view
parity test and prior research gates, plus all 20 Python recovery and package
integrity tests.

Regenerate the fixture and run the focused maintained replay from the repository
root:

```powershell
dotnet run --project tests/Stellar.AdaptiveResearchView.ParityGenerator/Stellar.AdaptiveResearchView.ParityGenerator.csproj -c Release -- data/research/v1 native-tests/fixtures/adaptive-research-view.json
.tools/build-tools/Scripts/ctest.exe --test-dir build-native/testing -R adaptive_research_view_parity --output-on-failure
```
