# Colonization Runtime Validation

The native colonization runtime is checked against 124 results produced by the actual C# implementation. The fixture contains 88 settlement-planning forwarding and dependency cases and 36 runtime command sequences. Every sequence records and compares the complete mutable world state after each successful command and each expected failure.

The runtime cases cover colony and outpost orders, player funding, retargeting, insufficient funds, AI candidate selection and distance ordering, timed work, occupied arrivals, bodyless arrivals, fallback habitats, partial funding, and source-visible partial mutations. Fleet snapshots retain nondefault combat and vessel history state to prove that unrelated tactical state survives every operation.

Four native-only boundaries cover integer and mission-revision exhaustion without C++ undefined behavior:

- route revision exhaustion before mutation;
- an accepted player order that deducts 120 credits before route revision exhaustion;
- colony identifier exhaustion before appending a colony, retaining establishment work already accumulated;
- arrival that appends the full colony and consumes the passengers before route-clear revision exhaustion.

The fixture also records eight source-only null-reference observations: seven null world arguments and one null fleet passed to `AbandonMissionForTransit`. The typed native API cannot represent those calls, so they are documented separately and are not counted as native parity passes. `TacticalLoadout` is null in these runtime fixtures because the colonization implementation never reads it; preservation of tactical state is instead demonstrated through nondefault `FleetCombatState` and complete `MassiveVesselState` history.

The canonical fixture SHA-256 is `3D16388CD4B8D094216ABE1C9323CE92616E7D8C8771285E11669D00564B9AF3`. Standalone Release (`/O2`) and Debug (`/Od`) builds both pass with `/W4 /WX`, reporting `124 actual C# cases and 4 native boundaries passed`. The retained C# oracle regenerates the canonical fixture byte for byte.

The maintained Windows validation passed all 43 CTest targets and all 19 Python export tests. `colonization_runtime_parity` passed as test 1 in 0.14 seconds. The combined validation log is `work/native-034-strategic-input-support-testing.log`.
