# Adaptive Research Authority validation

Gate 053 ports `AdaptiveResearchAuthority` and every `AdaptiveResearchAuthorityInputs` forwarder as the authoritative composition surface over the existing research kernel, expertise catalog, readiness calculator, expertise service, and starting-profile composer.

## Ownership and scope

`AdaptiveResearchAuthority` is an owning, move-only stable Pimpl. `Storage` constructs the kernel, expertise catalog, readiness calculator, expertise service, and starting-profile composer directly in their final addresses and in C# source order. Const getters borrow that storage; a getter retained across authority move construction remains valid because the allocation does not move. Destruction or move assignment of the owning authority invalidates previously borrowed getters. Civilization states own their primary and expertise sidecar data independently.

This gate does not add pressure, agenda, foreign-research, outcome, or snapshot-v2 support. Those runtime-owned sidecars remain separate migration work.

## Actual-source fixture

The retained generator at `tests/Stellar.AdaptiveResearchAuthority.ParityGenerator` references `Game.csproj` and invokes the real C# authority. Its fixture records complete state, expertise state, results, blockers, events, and errors before and after each command. Setup and serialization occur outside the operation catch. The fixture contains:

- all 8 canonical starting reference profiles through the authority, including seeded expertise and institutions;
- 29 sequential commands covering the complete view and readiness reads; start, pause, resume, reallocate, resolve, and segmented advancement; institutions, tacit assets, atrophy, pressure, evidence, traits, contextual traits, capabilities, and bounded candidate review;
- epsilon and 80-year advancement, invalid elapsed time, invalid labs before missing-node behavior, and capacity loss that pauses the live project;
- duplicate evidence with no kernel event but mandatory facade readiness recalculation, plus a partial failure where the evidence mutation remains committed before readiness lookup fails;
- nonpositive and NaN atrophy where expertise atrophy returns early but the authority still recalculates active readiness;
- the source behavior where a paused `hypothesis_resolution_required` project receives practice again while another project crosses boundaries.

The generator fingerprints every canonical JSON input before and after replay. The native test also compares all canonical JSON bytes before and after its replay.

## Reproduction

From the repository root:

```powershell
dotnet run --project tests/Stellar.AdaptiveResearchAuthority.ParityGenerator/Stellar.AdaptiveResearchAuthority.ParityGenerator.csproj -- data/research/v1 reproduced-adaptive-research-authority.json
cmake --build build-native --config Debug --target stellar_research_authority_tests
ctest --test-dir build-native -C Debug -R adaptive_research_authority_parity --output-on-failure
```

The isolated Debug and Release acceptance builds used MSVC C++23 with `/W4 /WX /permissive- /fp:precise /utf-8` and explicit ignored `/Fo` and `/Fd` outputs. Both report `8 profiles and 29 commands passed`. The fixture SHA-256 is `8C0FDDC6AE2F81F79B3FC2705E68CA6DFBB623A4CDAB7BBB9BD4F095F3E4EAC4`; byte regeneration matches it. The canonical research JSON fingerprint embedded before and after source replay is `2E76D70C9E270E69DF4BFC66B4135419720A0BA5CCA0B2D61DD5906FA22975B1`.

The native executable exits 1 with exception type, message, current directory, fixture path, and research root when arguments are missing. The managed generator exits 1 with `Exception.ToString`, current directory, research root, and fixture path for an invalid root, without creating output.

## Native boundary probes

In addition to source parity, the native test proves move construction and move assignment preserve the Pimpl allocation used by borrowed helpers, then composes a profile, calculates readiness, starts research, and advances through the moved authority. State-owned `string_view`, optional context, and span aliases are copied before replacement/removal mutations.


The promoted production target and retained native replay passed the focused maintained CTest check, `adaptive_research_authority_parity`, from the repository root. Build and execution output are retained in `work/native-053-authority-focused.log`. This check follows the clean 0.1.24 export and is not part of that older package.

The combined maintained engine 0.1.25 build passed 63/63 CTest and 20/20 Python checks in `work/native-054-recovery-testing.log`. The snapshot replay target is Windows-only because its retained fixture hashing uses BCrypt; production snapshot code has no Windows dependency. Exact committed package verification follows separately.

The exact clean 0.1.25 export of `aa3eaa834bd9c8570bde4d70b4265f47da1a5d2a` passed the combined 63/20 checks and all retained relocated checks. Package `StellarContinuum-windows-benchmark-aa3eaa83-20260913T120628476660Z` records `sourceDirty: false` and seven sealed runtime files; see `work/native-025-clean.log`.
