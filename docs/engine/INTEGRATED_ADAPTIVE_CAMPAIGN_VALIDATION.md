<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> Godot/C#/.NET references below are legacy implementation or fixture provenance,
> not the current runtime or instructions to restore it.
> Start with [the current handoff](../AGENT_HANDOFF.md) and
> [verified project state](../PROJECT_STATE.md).

# Gate 075 validation

The managed oracle is a plain C# reconstruction of the composition in
`Main.CoreIntegration.cs`. It does not invoke Godot `Main`, use presentation
clock/UI/autosave policy, or claim player-save17 compatibility. The fixture
pins nine source files before and after generation and freezes the matched
Adaptive Research construction/shipbuilding authorities, observer-filtered
Diplomacy AI knowledge, matched Diplomacy combat callback, disabled legacy
research/science, and phase order.

The 15-row replay compares complete retained world, schema-2 Adaptive Research,
and Diplomacy snapshots before and after every call. It also compares core,
sensor, Adaptive Research, and Diplomacy outputs, partial phase traces after
errors, maintenance cadence, AI intent/industry projection, and all combat
intelligence records. Coverage includes fresh and restored owners, zero and
positive time, sensor capability/no-capability, active research spending and a
long completion advance, blocked and granted construction/shipbuilding gates,
observer-private AI knowledge, and failures in each reachable phase.
The two-argument default no-trace path is separately replayed for positive,
zero, and failing calls and must produce the same final state and error.

Two independent managed generations were byte-identical at SHA-256
`DB84FF5A90F6BEFDFEB1E6D55C2B7636A6EA8630EC8025C0019C0D6807AD3677`.
Strict MSVC C++23 Debug and Release builds passed all 15 rows with
`/W4 /WX /permissive- /fp:precise /utf-8 /Z7`. Both managed and native
no-argument runs exit 1 and print the missing research root, source root, and
fixture path diagnostics.

Run the isolated strict checks with:

```text
python work/075-integrated-adaptive-campaign/build_strict.py debug
python work/075-integrated-adaptive-campaign/build_strict.py release
```

## Maintained integration

Gate 075 is registered as `integrated_adaptive_campaign_parity`. The retained generator project is `tests/Stellar.IntegratedAdaptiveCampaign.ParityGenerator`; only its project-reference path changed. Its source reconstructs the plain C# composition used by Main.CoreIntegration; it does not invoke Godot or provide general save compatibility. Root review made tracing caller-owned and opt-in, pinned fixture bytes, and records scanner capability at the sensor phase before later research completion.

- `core/include/stellar/core/integrated_adaptive_campaign.hpp` SHA-256 `1C1901EFAEF35BE9F440E6184317628AB1C9DB425FD871947D5B029EEDD3BF69`
- `core/src/integrated_adaptive_campaign.cpp` SHA-256 `6FA52A581001E57F9A9443EC6DBD992D1D3BA7E5215CB7D590E1CAE81DF8300B`
- `native-tests/integrated_adaptive_campaign_tests.cpp` SHA-256 `9605F5B65A1E2A2AC0EFEBB4E6BAD9D8A807425DAC2B8BCF858CDD6187034F2D`
- `native-tests/fixtures/integrated-adaptive-campaign.json` SHA-256 `DB84FF5A90F6BEFDFEB1E6D55C2B7636A6EA8630EC8025C0019C0D6807AD3677`
- `tests/Stellar.IntegratedAdaptiveCampaign.ParityGenerator/Program.cs` SHA-256 `C042BD4C25C1333AB4A50B395880F0BEB11E271C16A9EB52AEB8669E7D9D1998`

Maintained 0.1.35 validation passed 84/84 CTest and 28/28 Python. The three retained NativeRecovery tests cover deterministic full Adaptive campaign output, real research state, asset-root resolution, missing/corrupt research, output/pending preservation and argument boundaries. The maintained `dotnet run` source generator reproduced the 15-row fixture exactly (`work/075-maintained-generator.log`). Clean committed export is the subsequent gate.
