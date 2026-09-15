# Gate 040 coordinator command validation

The retained actual C# `GalaxySimulationStepCoordinator` oracle produced eight stateful scenarios containing 42 typed commands, plus two raw null-campaign ordering observations. `CurrentCulture` and `CurrentUICulture` are set to invariant at process entry. The independently regenerated fixture is byte-identical at SHA-256 `603761F5D4A55A70C883AFAEA39779D12B9392BBA0995B0A5ABCB9433BB77A69`.

Each fixture command freezes the complete input campaign before and after the production call. Typed result capture alone occurs inside the operation catch; result enumeration, normalization, and freezing occur afterward. Scenario controls and the actual ordered hostility callback pairs are serialized into the fixture.

The native consumer reuses gate 038's complete campaign decoder and encoder. It validates and decodes the command name and every typed argument before constructing an invocation-only callback. Before each invocation it compares native state to the source `Before`; afterward it compares the complete result or exact exception and the native state to source `After`. Integer JSON values compare exactly. `SimulationDays` values compare exactly, the enumerated float32 fields use relative tolerance `1e-6`, and remaining floating fields use relative tolerance `1e-10`.

Both opportunity-plan encoders serialize every source candidate field, including the nested reach assessment. The fixture contains one nonempty colony plan, two nonempty outpost plans, one accepted colony order, and one accepted outpost order. It also contains the source duplicate-ID deployment re-resolution, finite unsupported-reach rejection, four ordered hostility callback observations, and stateful retry after a partial raw batch failure.

The separate native mission-revision safety probe verifies the exact exhaustion exception and the retained Hold mutation at the partial-failure boundary. It is not counted as an actual-source command.

Strict MSVC C++23 Release and Debug builds use `/permissive- /fp:precise /utf-8 /W4 /WX`, explicit `/Fo` and `/Fd` paths under ignored draft build folders, and the full linked dependency set. Both logs report `campaign command parity: 8 scenarios, 42 commands` followed by their strict parity success line.

After promotion, `python tools/stellar-export/stellar.py build windows-testing` completed successfully with 50 of 50 CTest cases and 20 of 20 Python validation cases. The maintained run includes the 60-case combat-command runtime regression, the 28-step/8-combat-outcome campaign coordinator regression, and this 8-scenario/42-command coordinator suite.
