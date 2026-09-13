# Adaptive Research campaign state validation (gate 064)

Source authority: `src/Game/Simulation/Research/Adaptive/AdaptiveResearchCampaignState.cs` under invariant culture. This gate owns campaign state, the factory, funding storage primitives, and schema-2 campaign capture/restore. Funding quotes, simulation advance, player wrapper17 / nested galaxy16 persistence, and package deployment remain later gates.

The retained fixture contains 23 actual-source rows. It covers empty/all-species creation, numeric factory ordering, duplicate and unknown civilizations/species, runtime identity, campaign schemas 1/2 and rejected schemas, catalog/cardinality/identity ordering, nested schema-5 state recovery, valid/invalid/duplicate/inactive funding, schema-1 funding omission, partial funding restoration, exact one-third/final consumption, missing consumption, two-slot removal with LIFO reinsertion, and duplicate reservation. Every managed call records the canonical data fingerprint before/after. Typed snapshot inputs are serialized before/after; native replay separately encodes the decoded DTO before/after.

Native-only ownership checks prove campaign move construction preserves civilization address/support identity, move-assignment replaces the target identity, aliased funding node views are owned before removal, and previously captured DTO/JSON projections remain unchanged after later removals and reinsertions.

An initial deferred harness comparison reported a step-2 projection containing the removed `second-slot` entry even though the event result was `found=true`, `consumed=6`, `remaining=0`; its artifact did not retain the event result, so it cannot establish a production cause. Linear-scan and unconditional-view experiments did not change that old harness failure and were removed. The final harness uses the original indexed free-slot store and cached view, compares every event result and full projection immediately, retains the complete vector of those owned pairs, and compares the entire vector again after all removals/reinsertions. It also retains an earlier full JSON projection and typed Capture DTO as encoded bytes and proves both unchanged. Both configurations pass, so the original failure is not reproducible in the final readable harness; it remains recorded as a harness defect without a stronger causal claim. The initial mismatch artifacts remain `actual-mismatch.json`, `expected-mismatch.json`, and the earlier failed Debug log; they are diagnostic scratch, not acceptance evidence.

Canonical fingerprint: `2E76D70C9E270E69DF4BFC66B4135419720A0BA5CCA0B2D61DD5906FA22975B1`.

Fixture SHA-256: `EEC9250156B8162F32D7927C6B4DA5C742979EC0D567E30B01642A3B08735AFF`. The earlier `9ED052D2...` fixture omitted each restore row's authoritative galaxy input; regeneration added those typed civilization/species inputs so native replay cannot reconstruct them from scenario names. Formatting did not change the fixture.

Generator SHA-256: `1B8962C90B87E10ED325B0A1BB3F7356E8C85C0550EFB56E631EE81B952824A2`.

Strict commands:

```text
dotnet run --project ../research-campaign-oracle-064/Stellar.AdaptiveResearchCampaign.ParityGenerator.csproj --configuration Release -- data/research/v1 work/064-research-campaign/actual-source-fixture.json
python work/064-research-campaign/build_strict.py debug
python work/064-research-campaign/build_strict.py release
```

Both final C++23 `/W4 /WX /permissive- /fp:precise` replays passed all 23 source rows. Debug and Release run logs have SHA-256 `B654DAF4EFE42A927118F737D539131FFEBE3D09F08B36D8C3D7C162476DE37C`.

The native entry point independently hashes the canonical JSON files before loading, checks the retained fingerprint, checks the same fingerprint after replay, and requires the invoked count to equal the fixture row count. Missing arguments, a missing research directory, and a missing fixture each return exit 1 through the top-level diagnostic boundary with exception type, message, current directory, research root, and fixture path. Their retained logs are `native-missing-args.log`, `native-missing-root.log`, and `native-missing-fixture.log`.

Maintained integration is engine0.1.30. The exported host remains the legacy
campaign; these focused gates do not establish full Adaptive Research campaign
advance, diplomacy composition, player save compatibility or graphical parity.

Maintained engine0.1.30 validation passed75/75 CTest and20/20 Python checks (`work/native-030-testing-embedded.log`). Exact committed package verification follows.
