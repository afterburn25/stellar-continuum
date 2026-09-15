# Gate 047 Adaptive Research eligibility validation

The actual C# oracle loads the canonical research, applicability and facility catalogs and invokes the three public `AdaptiveResearchEligibilityEvaluator` methods under invariant culture. It constructs typed states through pre-resolved internal mutators outside the operation catch. Each case freezes its typed setup recipe and complete gate-044 state before invocation, then records the complete result or exact error and the complete state afterward. Method selection, projection, enumeration and JSON encoding occur outside the production-operation catch.

The retained fixture contains 415 native cases: 392 scientific, 17 project-start and six stage-facility evaluations. It queries all 370 canonical nodes with a fully satisfied scientific state. Every `ResearchBlockerCode` ordinal from 0 through 18 occurs with exact ordered blocker fields, optional subject and numeric values, and source message text. Six C# null-only calls are recorded separately: scientific and project-start reject null state or node ID, while stage-facility succeeds when the selected node and stage have no facility requirement. Native references and string views cannot represent those calls.

The canonical assets currently contain no nonpublic nodes, alternative capability requirements or alternative facility requirements. Those three dormant branches and both alternative fallbacks use an exclusively owned temporary copy of the actual catalog directory. The oracle and native consumer apply the same explicit fixture-recorded node IDs, capabilities, facility requirement and stage, load the production catalogs from that copy, and delete it afterward. Canonical data is never changed.

Coverage includes all-of and any-of prerequisite ordering, archived established knowledge, civilization and population applicability, absent versus present-empty context, global and mismatched contextual evidence, pressure tolerance and NaN behavior, exact and mismatched scoped capabilities, absent and paused active projects, directed-program capacity, minimum and free-lab thresholds, requested NaN and both infinities, required and alternative facilities, successful fallbacks, unknown stage-facility inputs, and exact unknown-directed-stage failure. The same invalid-stage state remains scientifically eligible, while an unknown-node project start returns its blocker before consulting the stage. A dedicated allowed case proves scientific eligibility excludes labs, program capacity and facilities; project-start cases prove those operational checks are included.

The native consumer decodes every argument and setup operation before its invocation-only catch, reconstructs the typed state, compares the full decoded state to the source before image, compares the owned result, and proves the full state is unchanged afterward. Integer and enum values compare exactly, finite floating values use relative tolerance `1e-12`, and named nonfinite values compare exactly.

Both harnesses normalize and verify an exclusively claimed absolute scratch root under a dedicated temporary parent, reject a preexisting claim, and use checked nonthrowing cleanup. Two independent source generations are byte-identical at SHA-256 `328012A625E4750AF1753A63366D2FB6FE81101B8B0AD568B0DA620F0DEE6250`. Strict MSVC C++23 Release and Debug builds use `/permissive- /fp:precise /utf-8 /W4 /WX` with explicit `/Fo` and `/Fd` outputs under ignored draft directories. Both replay all 415 native cases successfully and return nonzero from caught terminal failures.

The maintained Windows build passed all 55 CTest cases, including this parity test and the earlier Adaptive Research gates, plus all 20 Python recovery and package-integrity tests.

Regenerate the actual-source fixture and run the focused native replay with:

```powershell
dotnet run --project tests/Stellar.AdaptiveResearchEligibility.ParityGenerator/Stellar.AdaptiveResearchEligibility.ParityGenerator.csproj --configuration Release -- data/research/v1 native-tests/fixtures/adaptive-research-eligibility.json
.tools/build-tools/Scripts/ctest.exe --test-dir build-native/testing -R adaptive_research_eligibility_parity --output-on-failure
```
