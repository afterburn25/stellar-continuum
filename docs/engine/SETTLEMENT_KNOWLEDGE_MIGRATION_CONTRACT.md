# Settlement knowledge migration contract

## Scope and representation

The source authority is `SpeciesPlanetaryReadModel.cs`,
`ColonySettlementBodyResolver.cs`, and
`FriendlyColonyMissionReservations.cs`. The native view borrows const systems,
bodies, civilizations, colonies, fleets, and knowledge. Results own all strings
and IDs; no result retains a pointer into a caller vector. Biology comes only
from the accepted `species_colonization_assessment` implementation and its
`SpeciesColonizationViability`, rather than environmental settlement scoring.

## Observer read models

Build-for-species validates the species before checking observer existence.
Only bodies in fully surveyed systems are visible. Output sorts by system ID and
then body ID and contains the canonical environment assessment and colonization
viability.

Build-for-available-populations first resolves the first observer civilization.
It gathers species from positive-population owned colonies in colony order,
appends the civilization home species, applies ordinal distinctness, and sorts
species IDs ordinally. Each species build repeats species then observer
validation. The flattened result sorts by system ID, body ID, then species ID.
Zero, negative, and NaN populations fail the positive test; positive infinity
passes it. Foreign colony populations never become available.

## Settlement body resolution

Resolution validates species, then civilization. A missing system, incomplete
survey, or any existing colony in the system returns no body in that order.
Every body in the requested system is assessed. Foundable bodies sort by
viability descending, natural habitability descending, unprotected operational
capacity descending, then body ID ascending. Native comparison explicitly puts
NaN below numeric values as the source `double` comparer does. The returned
optional owns only the selected body ID.

## Friendly reservations

Candidate filtering excludes the requesting fleet ID, inactive fleets, foreign
fleets, non-colony roles, and embarked populations that are not greater than
zero. A remote destination reserves immediately, before occupancy, survey,
passenger species, or body checks. A local fleet requires a current unoccupied
system, full owner-local survey, a nonblank known passenger species, and either
a viable explicit body in that system or any viable body there.

Hold and prevent-automatic-settlement flags are intentionally irrelevant.
Resolved candidates sort stably by fleet ID. The first candidate for each system
wins, and reservation output retains group insertion order. Try-get rebuilds the
same map and returns source out-parameter zero when absent.

## Verification and boundaries

The invariant-culture actual-C# oracle freezes explicit arguments, immediate
results or typed errors, and complete before/after input state outside operation
catches. The native consumer parses required source fields and every expectation
before catching only the helper call. Because the native view is const, it also
requires the source after state to equal its before state. Result records,
ordering, messages, errors, and IDs are compared exhaustively.

Nine source-only null observations cover five null-world public calls, two
null-requesting-fleet calls and two null species-key calls. Native references,
spans and `string_view` cannot represent these null arguments; non-null unknown
species behavior remains executable and exact. Release and Debug standalone
consumers build with MSVC `/W4 /WX`.
