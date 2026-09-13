# Gate 066 validation

This draft ports the public `AdaptiveResearchCampaignSimulation.Advance` and
`AdaptiveResearchCampaignProgression.SynchronizeDevelopmentStages` behavior.
It intentionally excludes the outer campaign coordinator, diplomacy, save
envelopes, and automatic host scheduling.

The retained source generator executes under invariant culture and loads the
canonical `data/research/v1` files. Each row owns its complete typed world and
schema-2 campaign state before and after the call, ordered events, transient
research revisions, error type/message, and canonical input fingerprints. The
native harness reconstructs those inputs, verifies its pre-call projection,
invokes only the typed production operation inside the catch boundary, then
compares the full post-call projection and proves the caller snapshot and
canonical inputs remain unchanged.

Current source evidence contains 31 operations. It includes finite and
nonfinite time validation, a true zero-day no-op, missing-state/lookup partial
failures, numeric civilization ordering, seeded-ancient handling, network and
surface facility add/remove/no-op behavior, duplicate surface-key replacement,
retained warp capabilities, AI selection, funding shortfall/restoration and P0
banker's rounding, nonfinite and overflowing funding arithmetic, milestone and
outcome ordering, and empty-world behavior.

The fixture SHA-256 is
`BA0F004A7FCE79D9DD54947E5049A01A938E792BC60430CA8C021052ADC6E50A`.
The generator SHA-256 is
`213A432B52CD1C417C36F35EE8BB900D88F5A9E373B5CF7FD0EC32C4BD846E56`.
The canonical research fingerprint is
`2E76D70C9E270E69DF4BFC66B4135419720A0BA5CCA0B2D61DD5906FA22975B1`.
Fixture regeneration is byte deterministic.

One retained row starts from the source factory's live state. The remaining
ordinary rows pass through the source schema-2 capture/restore boundary before
their recorded call, so native replay can reconstruct the same transient
revision inputs. The intentional missing-campaign row also uses fresh setup.
Integer JSON values are compared without conversion to double, with an
explicit `2^53 + 1` regression. Floating fields use an absolute `1e-10`
tolerance; all text, enum, boolean, array order, and object structure are exact.

Regeneration:

```powershell
dotnet run --project ..\research-campaign-simulation-oracle-066\Stellar.AdaptiveResearchCampaignSimulation.ParityGenerator.csproj -- data\research\v1 work\066-research-campaign-simulation\actual-source-fixture.json
```

Strict native Debug and Release both passed all 31 source rows:

```powershell
python work/066-research-campaign-simulation/build_strict.py debug
python work/066-research-campaign-simulation/build_strict.py release
```

Both use C++23, `/W4 /WX /permissive- /fp:precise /Z7`, and explicit ignored
`/Fo` and `/Fd` output paths. The two run logs have SHA-256
`761B1CFFBF6892E4D1C856124B361F3075A5CFE4039E8AF23235B820759ACAC2`.
The native missing-argument, missing-root, and missing-fixture probes each exit
1 through the top-level diagnostic boundary. The managed missing-argument and
missing-root probes likewise exit 1 with full exception, stack, current
directory, research root, and fixture path.


Promoted to the maintained engine0.1.31 build. Combined maintained validation passed77/77 CTest and20/20 Python (`work/native-031-testing.log`); exact committed export follows.
