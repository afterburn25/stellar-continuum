# Adaptive Research strategic owner validation

Gate 060 validates the stable owning facade around the already source-matched
research components. The actual C# fixture loads the complete runtime and uses
one civilization state through the facade's Authority, Pressure, Agenda,
ForeignDiscovery, and Outcomes properties. It records catalog cardinalities,
initial and final core/support state, the delegated foreign assessment, and a
deterministic outcome plan. This is a composition check rather than a repeat of
the standalone component matrices.

Native-only probes retain every getter and weak-support address through move
construction and move assignment, verify that move assignment discards the
destination owner's former support graph, and verify that copying a
civilization creates a fresh support identity. Static assertions preserve the
facade's move-only contract.

The fixture SHA-256 is
`9DF23BE6FB32F90AF2D14ED867103FAD69D6DE0C2C4F27A5AA29A3501E2C470C`.
Strict C++23 Debug and Release builds both passed with `/W4 /WX`. The managed
generator returned exit 1 for missing arguments and for a missing research
root, with type, message, working directory, root, and fixture diagnostics.

```text
dotnet run --project ../research-strategic-owner-oracle-060/Stellar.AdaptiveResearchStrategicOwner.ParityGenerator.csproj -c Release -- data/research/v1 work/060-research-strategic-owner/actual-source-fixture.json
python work/060-research-strategic-owner/build_strict.py debug
python work/060-research-strategic-owner/build_strict.py release
```

## Maintained integration

Promoted into the root CMake build for Engine 0.1.28. The combined maintained build passed 70/70 CTest and 20/20 Python checks (`work/native-028-testing.log`). Exact committed export `a9610c7426d8fdf5c6f493a2b28b154fe7081b6f` passed all 70/70 CTest and 20/20 Python checks. Package `Builds/Windows/StellarContinuum-windows-benchmark-a9610c74-20260913T144010986128Z` has `sourceDirty=false`, seven hashed runtime files and all nine relocation/recovery flags true (`work/native-028-clean.log`). Separate-machine certification remains open.

The maintained oracle is `tests/Stellar.AdaptiveResearchStrategicOwner.ParityGenerator`. Final native copy-isolation checks save the original revision before mutating the copied state.
