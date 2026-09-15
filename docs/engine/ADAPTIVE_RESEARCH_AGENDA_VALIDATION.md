# Adaptive Research Agenda validation

Gate 056 ports the Agenda catalog, per-civilization state, perceived-adequacy review, and visible-project shortlist from the three `AdaptiveResearchAgenda*.cs` source files. It does not add strategic snapshots, capacity-request fulfillment, campaign persistence, or hidden-node selection.

The retained actual-source fixture contains 60 unique rows: 46 non-malformed rows, including the canonical catalog projection, and 14 malformed catalog/file/JSON-shape rows. The non-malformed rows exercise sparse state and two-free-slot dictionary reinsertion order, all recommendation branches, full candidate component/blocker projections, active and paused project exclusion, the exact three-revision cache key, no-op invalidation behavior, typed public errors, invalid numeric diagnostic formatting thresholds, fresh-state error ordering, and separate civilization/runtime identity. Malformed rows retain the exact changed UTF-8 bytes and the source before/after directory fingerprints.

- Fixture SHA-256: `B1BE47F565B651E53D5A7AAB623A681B22E857A52571C166140F7525E6E51965`
- Retained generator SHA-256: `D21D441A870B29EB11C92FA57AC868EBC95C95421E9E5FDC960DE41EC16F4EFA`
- Canonical research fingerprint: `2E76D70C9E270E69DF4BFC66B4135419720A0BA5CCA0B2D61DD5906FA22975B1`

Generate the actual-source fixture from the repository root:

```powershell
dotnet run --project tests/Stellar.AdaptiveResearchAgenda.ParityGenerator/Stellar.AdaptiveResearchAgenda.ParityGenerator.csproj -- data\research\v1 native-tests/fixtures/adaptive-research-agenda.json
```

Run the maintained replay:

```powershell
cmake --build build-native/testing --target stellar_research_agenda_tests
ctest --test-dir build-native/testing -R adaptive_research_agenda_parity --output-on-failure
```

The native replay uses Windows CNG SHA-256 to prove retained inputs are byte-identical before and after production calls. A maintained native test target therefore needs a whole-target `WIN32` guard and `bcrypt` link. The production source has no Windows dependency.

Both strict configurations pass all 60 rows against the canonical Gate055 identity implementation. Managed no-argument and missing-root runs exit 1 with full exception text, stack, working directory, research-root, and fixture diagnostics.

The native JSON library reports missing-file, JSON syntax, and JSON value-kind failures through `AdaptiveResearchAgendaCatalogError`; source .NET reports platform-specific file, `JsonReaderException`, or `InvalidOperationException` categories and diagnostics. The fixture records those source results, while the replay treats these as an explicit parser boundary. Authored semantic catalog messages and public runtime error messages are compared exactly after typed category mapping. No claim is made about universal precedence when one payload combines a parser-shape fault with an earlier or later authored semantic fault.

Candidate and explanation strings are byte-compared for the canonical fixture. Numeric text uses the shared precision-15 legacy custom formatter for the source `0.#` messages. This evidence does not claim equivalence for every non-finite or arbitrary malformed numeric payload.

Review also corrected sparse priority removal/reinsertion to reuse free slots in
source Dictionary order, lazy state creation before bad-priority lookup, catalog
argument evaluation order, typed exception boundaries, and ownership of aliased
provenance. The shared general numeric formatter retains the source fixed/exponent
boundary; exact Agenda diagnostics cover 1e6, 1e16, 1e17, -1e-4 and -1e-5.
The final harness separates production calls from JSON projections, so projection
failures cannot masquerade as catalog failures.

Maintained integration passed all 65 CTest and 20 Python checks in
`work/native-026-testing.log`. The final source generator was also run from its
maintained project (`work/native-026-agenda-source.log`). Its raw output is retained
at `work/agenda-026-regenerated.json`: every field matches the committed source
fixture except the owned temporary-directory GUID embedded in the missing-file
exception. Normalizing only that GUID makes both JSON documents identical. Raw
byte-for-byte regeneration is therefore not claimed for this fixture; the exact
original exception and regenerated exception are retained, and input contents and
all semantic messages remain unchanged.

Exact committed engine 0.1.26 export `84d7370225a6b6b234707890ad8e1cb3c078d047` passed 65/65 CTest and 20/20 Python checks. Package `Builds/Windows/StellarContinuum-windows-benchmark-84d73702-20260913T125937489845Z` has `sourceDirty=false`, seven hashed runtime files, and all nine relocation/recovery validation flags true. Evidence: `work/native-026-clean.log`. All six GitHub workflows for this commit passed, including native CI 34758146962 and screenshot CI 34758146958. This remains a headless foundation, not a native playable release or clean-machine certification.
