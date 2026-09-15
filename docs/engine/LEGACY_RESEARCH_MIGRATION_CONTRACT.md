# Legacy research migration contract

## Source and scope

The authority is `src/Game/Simulation/Research/ResearchSimulation.cs` together
with the accepted legacy `TechnologyRegistry` conversion. This gate converts
the two advance entry points, `StartResearch`, legacy AI selection, completion
events, and the prototype-warp development-stage replacement. It does not add
adaptive research, currency costs, construction, ship spawning, or scheduling.

The frozen native surface is `LegacyResearchSimulation`,
`LegacyResearchWorldView`, `LegacyResearchEvent`, and
`LegacyResearchOrderResult` in `legacy_research.hpp`. The borrowed world contains
mutable civilization, technology, and economy spans and a const construction
span. Public methods are `advance`, `advance_for_civilization`, and
`start_research`.

## Advance order

Advance snapshots civilization values before iteration. Global advance visits
that snapshot in list order; selected advance applies the ID filter before the
Ancient check. Ancient civilizations skip every state lookup. Every other visit
performs first-match technology, construction, then economy lookups, even when
the player has no active research. Missing data therefore fails in that order.
Duplicate civilization IDs remain separate snapshot visits while both visits
mutate the first matching state/economy and stage replacement mutates the first
matching live civilization.

An idle non-player selects from the registry's available definitions. Ordering
is descending source score, then ascending research cost, with stable source
registry order for a complete tie. Source `double` comparison puts NaN scores
below numeric scores. Assigning the AI research ID happens before its definition
lookup and spending.

Active research resolves by exact ordinal ID. Remaining work is
`Math.Max(0, cost-progress)`; spend is `Math.Min(remaining,
Math.Max(0, science))`. Native helpers preserve source NaN behavior. Science is
debited and progress credited before the completion threshold test. Completion
occurs unless `progress + 0.0001 < cost`, inserts the technology into the
ordered unique completed-ID set, clears active research and progress, and emits
the completion event. Completing `prototype_warp_drive` from a PreWarp snapshot
replaces the first matching live civilization with WarpCapable and emits the
stage event second. Existing completed duplicates do not get appended, but the
rest of completion still occurs.

Returned events are method-local. On an exception, mutations from earlier
visits and mutations before the failing operation persist, but no event list is
returned.

## StartResearch order

StartResearch first finds the first civilization. Unknown and Ancient results
return without state lookup. It then first-matches technology state; an active
ID returns without construction lookup. It then first-matches construction and
filters the accepted registry availability helper. Unknown, completed, unmet
technology/project, null, and ordinally mismatched IDs share the unavailable
result. Native represents the nullable source string as an optional string view.
Acceptance sets the canonical definition ID and resets progress to zero;
economy is never read or charged.

## Verification and boundaries

The invariant-culture actual-C# oracle freezes arguments, before state,
immediate results or typed errors, and after state outside operation catches.
The native consumer parses all inputs and expectations before invoking only the
simulation operation inside its catch. It compares ordered civilizations,
including preserved leadership office order and character metadata, technology
insertion order, construction, economies, results/events, exact messages,
errors, and partial mutations.

C# null-galaxy calls are retained as source-only observations because native
spans cannot represent a null aggregate. Native views accept empty spans and
otherwise have no additional semantic boundary in this gate. Standalone Release
and Debug builds use MSVC `/W4 /WX`.
