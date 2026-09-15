# Canonical exploration travel and survey orders

The preserved public command is
`ExplorationSimulation.IssueTravelOrder(GalaxyState, int, int)`. It delegates
to the same `ExplorationMissionPlanner.AssessOrder` used by presentation and AI,
with `requireSurveyWork: false`. A rejection returns the assessment without
mutating any fleet. An accepted local order calls `FleetRouteOrders.Clear`; an
accepted nonlocal order calls `FleetRouteOrders.Assign` with the candidate's
already-authoritative reach assessment. `IssueSurveyOrder` is the same command
with `requireSurveyWork: true`.

The maintained native tree already ports every component of that sequence:

- `ExplorationMissionPlanner::assess_order`
- `clear_fleet_route`
- `assign_fleet_route`
- `ExplorationSimulation::assess_operational_reach` and `advance`

This closes the previously missing public exploration mutation methods. The maintained header and source add
`ExplorationOrderWorldView`, `issue_travel_order`, and `issue_survey_order` to
`ExplorationSimulation`. The implementation only composes those existing
planner and route helpers. It adds no reach, lane, fuel, drive, survey, or
transit formula.

The maintained actual-source oracle is `tests/Stellar.ExplorationOrders.ParityGenerator`, with a direct ProjectReference to the preserved Game.csproj. The original isolated validation used an external sibling directory so its source would not enter the game compile glob. Root promotion adds only working-directory/output diagnostics to its terminal exception handler; fixture content is unchanged. Its project references the real
`Game.csproj`, and its top-level exception handler returns a failing process for
terminal errors. Twelve bounded rows cover missing, inactive and wrong-role
fleets; unknown and blocked targets; scout/science assignment; travel versus
survey coverage policy; local route clearing; and local-transit rerouting. Each
row records the complete before and after fleet objects plus the complete
source assessment. The native replay compares those results and every retained
fleet field.

The source generator produced the same fixture hash on two runs. The staged
translation unit and replay compiled under MSVC C++ latest with
`/W4 /WX /permissive-`, UTF-8 and precise floating point in strict Debug
(`/MTd /RTC1`) and Release (`/MT /O2 /DNDEBUG`). Both configurations replayed
all `12/12` actual-source rows. A negative fixture with the accepted scout
order's resulting mission revision changed was rejected at the exact fleet
comparison.

The reused native route helpers deliberately reject `INT_MAX` mission-revision
exhaustion before mutation, as documented on `fleet_reach.hpp`; the preserved
C# increment is unchecked in the normal project configuration. That established
native hardening boundary is outside the source-equivalent rows.

Frozen SHA-256 evidence:

- candidate header: `6c3d87f8153453548a243564ab71c4123c6b0793e732593eceade516aade3671`
- candidate source: `47332d1e8ab586e85aa2f3eec241803caa10b33d5b831cb5b9152569debbc466`
- native replay: `f4d03e30ae59b8f031e17f0690b31587e48309504536954e1ca82f311541c362`
- deterministic fixture: `3f1ad4ab82ef260527b339a59ccbc57338d8dc2b3c085c3cf467f9237bc1d622`
- external source generator: `968f564c733dba107c00304718258fdd7dc71d29976f7436378ba63424c91214`
- external generator project: `1fce33263d63d00f0917821adef23701fa444a9308ecb3bf5b60d8233b492225`
