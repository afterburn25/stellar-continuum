# Exploration planning and civilian recovery validation

Engine 0.1.13 integrates three source-parity gates. The clean Release export directly passes 33/33 CTest and 19/19 Python checks (`work/native-013-clean.log`). The earlier citation of `native-013-final-build.log` was incorrect: that file is historical and reports only 8/15. Each new gate also passes standalone Release and Debug compilation with `/W4 /WX` (Debug `/RTC1`). Retained C# generators reproduce the committed fixture hashes. Fixture decoding, result serialization, callback assertions and state comparisons occur outside the operation exception capture so harness failures cannot masquerade as expected gameplay failures.

Exact clean package: `Builds/Windows/StellarContinuum-windows-benchmark-33a0f639-20260913T050731985374Z`; source `33a0f639f2ba58807bbaccc57395c8d127c49174`; engine 0.1.13; `sourceDirty: false`; seven sealed runtime files; relocated fresh-campaign diagnostic passed. This runs on the development host with restricted runtime paths, not a separate clean-machine certification.

| Gate | Native executions | Fixture SHA-256 |
| --- | ---: | --- |
| Civilian recovery | 75 | `A255AE83FBEF4555AAB33D80C65B5BA974AF6A0AF6D2A8F821A4373359E87A2D` |
| Survey operations | 55 | `535BA3C8AB079C7998C186DF137E067DCF7F67262ABC9CCF651AEFB1E2F1CBF5` |
| Exploration planning | 60 | `F30D2091D88FACCD3DF23A51EFD7EF312CFA3307E4F7D346501292B98A1E48DE` |

Recovery covers first-match active civilian fleets, hold/resume, paid colony-mission abandonment confirmation, nearest reachable base selection, in-warp queued return, no-base failures, exact messages and partial mutation order. Its 75 executions include four explicit native revision-exhaustion boundary cases. Five additional source-only null observations are retained as boundary documentation, not native executions. Fuel, cargo and colonists are not silently refunded by recovery commands.

Survey profiles cover all source stellar/archetype categories, planet/moon counts, anomaly and rare-resource caps, duration bounds, radiation hazard thresholds, duplicate/order behavior and NaN/infinity handling. Four unrepresentable C# null observations are metadata rather than native-case counts. Two of the 55 actual executions validate source failures.

Planning includes 30 plan, 16 order assessment and 14 AI selection cases. It preserves knowledge-gated survey details, reach diagnostics, supported-target ordering, priority/distance/ID ties, candidate limits, occupied destination reservations and shared-target fallback. Extra coverage checks NaN sorting and invariant percentage formatting. The coordinator borrows a planner, and rvalue construction is rejected at compile time to prevent dangling references.

These gates provide planning and recovery commands. They do not themselves advance ships, refuel, reveal systems, run the full campaign scheduler, migrate player saves or render the game. Reviewed gate 025 will add actual exploration advancement separately. All source rules remain authoritative; no simplified replacement economy or exploration model is introduced.
