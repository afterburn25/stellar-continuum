# Combat simulation validation

The retained actual C# oracle contains 73 cases covering military orders, own-force
summaries, combat advances, multi-command engagement history, duplicate fleet IDs,
hostility callback order and failure, simultaneous fire, destruction, retreat, and
source-ordered partial mutation. Each command records a complete post-command world
snapshot, including commands that throw, and the native consumer compares that
snapshot outside the operation catch.

The promoted fixture SHA-256 is
`2E5987EF5B0B37B9660002E1F77B52DF39F766B3727CF29E037C8D80495BCB50`.
Before promotion, standalone Release and Debug builds compiled with `/W4 /WX` and
passed all 73 actual C# cases plus three separately reported native boundaries.

The native boundaries reject NaN and positive-infinity simulation deltas before
mutation because converting those values to a volley count is undefined in C++.
A finite `1e100` delta is rejected when its computed volley count exceeds the native
integer range. That case verifies the complete post-failure world, including the
source-ordered lazy combat-state initialization, and then retries on the same
simulation instance to verify engagement persistence.

`TacticalLoadout` is null throughout this bridge because this scoped simulation does
not access it. Every fixture fleet has a nondefault `MassiveVesselState`; all of its
represented history and condition fields are included in every world comparison.
This validation does not cover massive-combat resolution, diplomacy ownership,
intelligence estimates, travel, or a combat-event archive.

The maintained `windows-testing` build passed 38 of 38 CTest targets and 19 of 19
Python recovery/package-integrity tests. Its complete log is
`work/native-030-combat-testing.log`.
