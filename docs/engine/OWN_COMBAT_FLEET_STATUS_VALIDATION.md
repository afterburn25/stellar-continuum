# Own combat fleet status validation

The retained actual C# oracle contains 46 `OwnCombatFleetStatusBuilder.Build`
cases and one separately recorded null-world observation. The canonical fixture
SHA-256 is
`D6FB05B50D5D134B4E63F5792C8987DD64E432BAA72EE1F0B8CB0A108B84B0BF`.

The cases cover every fleet role and known military order, unknown orders,
missing/blank/unknown combat profiles, stable ordering with duplicate fleet IDs,
inactive and foreign fleet privacy, negative and nonfinite persisted values,
firing and hull-damage epsilon boundaries, retreat progress, nullable
disengagement, unarmed vessels, and a mixed aggregate. Each case compares every
summary field, every stored and computed per-fleet field, and complete decoded
civilization and fleet state before and after the read-only operation. Integer
values are compared exactly before floating-point tolerance is considered.

The native consumer parses input and expected output before entering the
production exception capture. Serialization and all comparisons occur after the
capture. A null C# `GalaxyState` cannot be expressed by the native typed view and
is documented separately. `TacticalLoadout` remains outside the native import
boundary; all cases require it to be null while retaining nondefault tactical
vessel history.

Standalone C++23 MSVC Release and Debug builds pass all 46 source cases with
`/W4 /WX /fp:precise`. The shared readiness extraction also passes the existing
56 combat-readiness cases, 22 prototype-capability cases, and three source-only
null observations. Maintained Windows validation was combined with the campaign
coordinator integration gate. The successful maintained run passed 48/48 CTest
tests and 19/19 Python recovery/package tests. Its transcript is
`work/native-038-campaign-testing.log` with SHA-256
`469B60322471A88C44F33420A0648E07040803999CC8392C41FB0E01E225E5D1`.
