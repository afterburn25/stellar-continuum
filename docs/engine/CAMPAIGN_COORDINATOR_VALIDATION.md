# Campaign coordinator validation

Gate 038 ports the ordered `GalaxySimulationStepCoordinator.Advance` boundary
and the combat-outcome projection from the retained C# source. It does not claim
the coordinator command/read-model surface, a campaign save schema, a playable
campaign loop, or tick-throughput/FPS evidence.

`CampaignSimulationState` owns the complete native `FreshCampaignState` plus a
lazy retained lane graph. The graph owns its astronomy and is rebuilt after an
actual system-geometry change. Each phase rebuilds its borrowed spans and local
economic projections after vector-appending phases. Strategic and combat runtime
objects remain on the coordinator across calls and moves.

The retained `stellar-campaign-step-oracle-v1` fixture contains 28 actual C#
multi-step cases, eight standalone actual combat-outcome cases, and four
source-only observations. It covers invalid and zero time, duplicate/missing
state, partial mutation on late failure and retry, capability and hostility call
order, construction/ship/research/exploration/freight/combat/colonization order,
duplicate civilization allocation order, strategic and combat history, and the
source configuration that omits strategic shipbuilding preferences. Its SHA-256
is `F491991643BDE25FCC66402AC9B7090CD2E6334970784D22A89130244DC320CA`.

The native consumer compares integer fields and returned simulation days exactly.
Finite double economic, progress, and result fields use a relative tolerance of
`1e-10 * max(1, abs(actual), abs(expected))`. A `1e-6` relative tolerance is
limited to source `float` fields: vector X/Y, surface Z and rotation, sensor
range, the six tactical-vessel fractions, and the core exclusion radius. Named
nonfinite values compare by category. The consumer compares the complete decoded
campaign projection after every command, including failed calls, as well as
ordered result records, computed combat outcomes, and one unified callback
timeline. The native fallback flag is initialized from fixture metadata and
checked separately because the C# `GalaxyState` does not expose it.

Additional native probes verify a value-captured mutable capability callback
across multiple phases and a coordinator move, null input, lazy time validation,
lane ownership after freeing a replaced system vector, cache retention for
identical geometry, state movement, and invalidation after geometry changes.

The maintained Windows testing preset passed all 48 CTest tests and all 19
Python recovery and package-integrity tests. The successful transcript is
`work/native-038-campaign-testing.log`, with SHA-256
`469B60322471A88C44F33420A0648E07040803999CC8392C41FB0E01E225E5D1`.
Its campaign target passed as test 45. The initial integration attempt stopped
during dependency scanning because the promoted coordinator source and two
promoted tests still used draft-local header names. Those includes now use the
public `stellar/core` paths; the original failed transcript is retained as
`work/native-038-campaign-testing-integration-failure.log`, with SHA-256
`33CAD09B60FBF741D3E72510F32FE71261E65CAB9C848A0E21F0398AE464C4C8`.

After tightening the numeric comparison rules, the focused linked Release and
Debug `/W4 /WX` builds and fixture replays both passed all 28 step cases and all
eight combat-outcome cases. Their compile-log SHA-256 values are
`5CD4E12342D7E8E36CB02A2AA5ECC61F7D11E2C2929D0E3CF8EC9B6530B43387`
and `415F8E6D23DAC6D8985ABD9318D025A9551C08487363361331356D350A6C7868`;
their replay-log values are
`50C805FA4DB149D06D21E42AE93691DF2B41BBD26D492D2B42A4FB3224B0FA84`
and `B2EB7E427D1A49B89A2414019AB767DCC25E251F9B0D78BE7E0ED6082DE4C7B6`.
