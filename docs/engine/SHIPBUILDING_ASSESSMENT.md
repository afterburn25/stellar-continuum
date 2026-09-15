# Canonical shipbuilding start assessment

The maintained implementation extracts the existing `start_ship_build` validation into one
internal, non-mutating preparation path and exposes its owned result through
`assess_start_ship_build`. The committing command calls that same preparation
once and consumes its values. The UI therefore does not reproduce queue,
research, construction, treasury, stable-identity, population, source-colony,
or species rules.

## Preserved validation and mutation sequence

The order remains the preserved C# `ShipbuildingSimulation.TryStartBuild`
sequence and the already maintained C++ behavior:

1. Find the civilization, then the first matching shipyard.
2. Reject the eight-order pending cap before design lookup.
3. Resolve the design and its authoritative capability/construction lock.
4. Resolve the first matching economy and sovereign currency, then apply the
   finite-credit check with the existing `0.0001` tolerance.
5. Validate and prepare the stable order identity, preserving every current
   identity diagnostic.
6. For a population-carrying design, select the largest owned colony with the
   current stable first-on-tie and NaN behavior. Require the design reservation
   plus the existing 500 million retained population, then validate its species.
7. Only after all checks pass, debit credits, reserve population from the exact
   prepared colony, write active or queued state, and increment the sequence.

The public `minimum_retained_colony_population_millions` constant gives the
existing literal a canonical name; its value and behavior remain 500 million.
No gameplay formula changed.

## Assessment ownership

`ShipbuildingStartAssessment` owns strings and scalar values. It exposes the
exact first blocker, design costs, pending/max queue counts, whether the order
would queue, the prepared stable ID, the exact population reservation and
minimum source population, and the chosen source colony's ID, species, and
current population when selection reaches that phase. It retains no pointer or
span. The result is a preview only: `start_ship_build` always prepares again
from current state, so a caller cannot commit a stale assessment.

An earlier blocker intentionally prevents later-phase details from being
computed. For example, insufficient funding remains the first failure before
population source selection. This preserves source diagnostic order while the
design's population reservation and minimum requirement remain available as
soon as the design and lock checks pass.

## Evidence

`shipbuilding_assessment_tests.cpp` checks non-mutation, exact design and
population requirements, chosen colony provenance, active versus queued
preparation, single debit/reservation on commit, population and funding
failure order, exact assessment/command blocker equality, stale preview
revalidation, queue-before-design order, and stable-identity diagnostics.

The unchanged maintained `native-tests/shipbuilding_tests.cpp` also replayed
the full actual-source `native-tests/fixtures/shipbuilding.json` suite against
the candidate implementation: 89/89 cases passed in strict Debug and 89/89 in
strict Release. The focused assessment tests passed in both configurations.
Together these runs cover existing commands, failures, mutation order,
automatic orders, advancement, cancellation, fleet completion, and the new
non-mutating assessment/shared-prepare contract.

Strict Debug and Release both passed the focused assessment and unchanged actual-source shipbuilding replay. The combined engine0.1.47 build passed121 CTest.
