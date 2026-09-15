# Gate 051 Adaptive Research readiness and expertise service validation

This gate ports `AdaptiveResearchReadinessCalculator`, `AdaptiveResearchExpertiseService`, and the internal facility-capability replacement used during institution synchronization. It attaches one owned expertise sidecar to civilization state through a public const query and an internal writer. Core state, expertise, and materialized-view revisions remain separate. Authority, starting profiles, snapshot schema 2, funding, and campaign integration remain outside this gate.

The calculator borrows the base, facility, expertise, and progress catalogs. The service borrows the base and expertise catalogs and the calculator. These dependencies must outlive their consumers and remain unmoved; individual rvalue construction is rejected. Returned readiness breakdowns own all IDs, context, numbers, and explanations.

The retained C# generator sets current and UI culture to invariant and invokes the actual source. Its fixture contains 23 read-only readiness cases, 36 sequential public service commands, and 6 focused service cases. Every record retains typed inputs, the full core and expertise state before and after, all three revision counters, exact errors, and complete returned breakdowns. The focused service matrix records synchronization partial mutation, atrophy floors and grace behavior, NaN current-year behavior, and stage/node validation order.

Native JSON decoding, operation selection, and typed argument construction occur before the invocation-only catch. Result projection and full-state comparison occur afterward. Integers, enums, IDs, revisions, and collection order compare exactly. Only named floating-point DTO/state fields use a relative tolerance of `1e-10`; named nonfinite values compare exactly. Player-facing explanations use the shared `detail::legacy_custom_fixed` formatter.

Native-only checks cover every individual borrowed-rvalue constructor guard, owned sidecar copy independence, state-cache `string_view` aliases, bulk facility span aliasing, and checked core/expertise revision exhaustion. The attached sidecar is an owned native clone boundary; this does not claim C# object identity semantics.

Two source generations are byte-identical at SHA-256 `F8A1EA26DC22F646C517A3E112DCD8183EE64FA2A0DC00C15167613C9C8B6E2F`. Strict isolated Debug and Release builds use MSVC C++23 `/W4 /WX /permissive- /fp:precise /utf-8` with explicit ignored `/Fo` and `/Fd` outputs and replay the full fixture. A combined translation unit includes every current Adaptive Research public header. Direct invocation without arguments exits 1 and prints exception type, message, current directory, fixture, and research root.
The retained managed generator also catches catalog, setup, and output failures at entry; a missing research root exits 1 with the full managed exception, current directory, research-root argument, and fixture path, and creates no output file.

```powershell
dotnet run --project tests/Stellar.AdaptiveResearchReadiness.ParityGenerator/Stellar.AdaptiveResearchReadiness.ParityGenerator.csproj -c Release -- data/research/v1 native-tests/fixtures/adaptive-research-readiness.json
python tools/stellar-export/stellar.py build windows-testing
ctest --test-dir build-native/testing -R "^adaptive_research_readiness_parity$" --output-on-failure
```


The maintained Windows testing build passed 60/60 CTest and 20/20 Python checks after integration. The retained transcript is `work/native-051-readiness-testing.log`. This is development integration evidence; the latest separately verified clean package is still engine 0.1.23.

The exact committed engine 0.1.24 export (`febf1d7cb42ecd52b017c861376737ed9c0ea183`) subsequently passed the combined 61/61 CTest and 20/20 Python suite with `sourceDirty: false`, seven sealed runtime files and relocated launch/campaign validation. See `work/native-024-clean.log` and HANDOFF.md for the exact package.
