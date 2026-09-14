# Campaign massive combat lifecycle parity

Gate 095 extends the maintained `CampaignMassiveCombat` service with observer
snapshots, authoritative advancement, engagement evidence, non-player doctrine
and reconciliation. The owned tactical encounter remains canonical. The internal
fleet-intelligence adapter reuses the existing rules with a retained fleet index;
it does not create a second intelligence policy or duplicate world state.

The maintained generator is `tests/Stellar.CampaignMassiveLifecycle.ParityGenerator`,
using the actual `Game.csproj`. The retained fixture is
`native-tests/fixtures/campaign-massive-lifecycle.json`. The CTest entry is
`campaign_massive_lifecycle_parity`; Gate 094 and the existing fleet-intelligence
regressions remain separately runnable. Strict build evidence is retained in
`work/095-campaign-massive-lifecycle/build/{debug,release}`.

The external `actual` source oracle produced 37 rows twice with identical bytes.
Both fixtures have SHA-256
`3BB574E8C47CB9F89F653E525B9D731D677030BD00FBCE17E1DBC9257B30AA6F`.

The fixture fingerprints all directly composed source contracts, including
`MassiveCombatOutcome.cs` and `FleetCombatPower.cs`. The native runner checks
every fingerprint before replay, checks the fixture and sources again after
replay, and compares the complete resulting world, return value, or exact
exception type/message for every row.

Coverage includes begin/reconcile/observe/advance lifecycle behavior, clone and
signed-zero paths, active-only observation, hidden 0.2-confidence attacker and
salvo redaction, filtering visible events before the last-128 cap, scanner
callback receipts, engagement intelligence across a saved clone and a new
runtime, and doctrine emergency-retreat threshold, escort, distance tie, and
inexact-own behavior. Move construction/assignment and the owned hostility
callback are also exercised outside the fixture rows.

Observation results retain their actual coordinate fields: formation
`Vector2` positions and velocities serialize as `X/Y`, while event and salvo
`MassivePoint` values additionally retain nested `Vector.X/Y` and `IsFinite`.
The scanned salvo case checks non-axis formation positions, nonzero non-axis
velocities, and fractional interpolation (`Progress01 = 0.5`, current position
`(190, 70.5)`). Binary32 comparisons cover formation coordinates, strengths,
headings, warp progress, and all active-salvo floating-point values.

Strict MSVC Debug and Release builds pass with `/W4 /WX /permissive-
/fp:precise /utf-8 /Z7`, isolated object/PDB directories, and the respective
debug/release runtime and optimization flags. Each build reports:

    Campaign massive combat lifecycle parity: 37/37 rows passed

Both configurations also reject an empty argument list, a missing fixture, and
a missing source root with contextual terminal diagnostics.

Maintained integration passed all three focused campaign-begin/reconcile,
campaign-lifecycle and fleet-intelligence checks. The maintained actual-source
generator reproduced the fixture bytes (`work/native-095-integration.log`).
Integration review tightened retained lookup invalidation to include ordered
bindings. Native equivalence probes cover relocated fleet storage, reordered
bindings and duplicate bindings after the lookup is warm; the retained service
must produce the same world or failure as a freshly constructed service.

This is headless native lifecycle evidence. Player/developer frame routing,
session save/recovery, presentation, graphical frame rate and full game parity
are outside this gate. Source timing diagnostics are not durable state and are
not reproduced as a performance claim.
