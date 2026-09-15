# Settlement planning migration contract

This planned gate follows the reviewed settlement-knowledge helpers. The source authority is `ColonizationOpportunityPlanner.cs`, `ResourceOutpostOpportunityPlanner.cs`, their record definitions and the existing species, reach, currency and resource-deposit helpers. It does not advance fleets, charge money, establish colonies or replace the source's current single-settlement-per-system rule.

## Boundary and dependencies

Use a borrowed const world view for systems, bodies, civilizations, colonies, fleets, economies and knowledge, plus the lane graph cache. Consume `SettlementKnowledgeWorldView` and its observer-safe suitability/reservation functions, the accepted `SpeciesColonizationViability` biology assessment, physical fleet distance, and the existing reach assessment callback signature with Colony mission kind. Return owned candidate/plan/assessment records. Do not return cached pointers into mutable vectors or persist views. Currency and resource-deposit calculations retain their authoritative native helpers.

The later settlement advancement view must hold a growable colony vector rather than a fixed span, since establishing a settlement appends a new colony. Planning can borrow a fresh span per call. Fleet iteration remains stable because colony creation does not append fleets. Do not hold colony iterators across a colony append.

## Colony opportunity rules

Find the first active populated Colony fleet with the requested ID, excluding the resource-outpost design. Validate its embarked species before lookup work. Clamp candidate counts to 1..64. BuildPlan creates unique-ID system and body dictionaries before suitability/reach work: duplicate IDs fail as in C#, rather than silently keeping one entry. Build observer-local fully surveyed suitability, cache reach once per system, and reconstruct friendly reservations from actual fleet state.

Eligibility preserves solid surface, absence of native pre-warp occupants, source biological viability, any-owner system occupancy, friendly reservation, supported reach and expedition affordability. Source candidate construction evaluates affordability before choosing the explanatory reason; a missing economy can therefore throw even for a biologically rejected candidate. A non-null destination bypasses this initial economy affordability lookup. Preserve this order rather than hiding missing state with an early rejection.

Sort orderable candidates first, then viability rank, natural habitability and operational capacity descending, followed by physical distance and stable system/body IDs ascending. Use .NET-compatible NaN ordering and stable ties. AssessOrder performs its own ordered checks for fleet, passengers, destination, detection, complete survey and exact system/body membership. It must not merely search a capped plan: a valid explicit target may lie outside the displayed candidate window. Preserve candidate payloads on rejected assessments where the source supplies them and keep exact currency/percentage messages.

## Resource-outpost differences

An outpost vessel requires the exact authored outpost design, active Colony role and positive personnel. BuildPlan creates unique system/body dictionaries and observer-safe suitability, preserves body insertion order during cached reach evaluation, and computes candidates before filtering to rare resources. Consequently candidate construction can expose missing economy data even for a body later filtered out; preserve actual operation order.

Require a solid surface, confirmed rare deposit, no native population, and source Unsuitable biological viability. A normally viable colony site is rejected for an outpost vessel. Occupancy blocks the whole system. Outpost reservations use the source's simpler rule: another active friendly Colony fleet with that destination; do not replace it with the more selective normal-colony reservation helper. Preserve the 90-unit affordability rule, 120-unit normal-colony rule and existing currency representation, without inventing a new exchange model.

Unlike normal colony AssessOrder, outpost AssessOrder searches the hard-capped 64-candidate plan. Preserve that behavior and cover a qualifying target beyond the cap. Orderable status, exact deposit metadata and all source messages are part of parity.

## Acceptance

Use actual C# methods for complete ordered outputs and exact errors. Cover private/partial/full survey states, foreign mission noninterference, occupied systems, explicit targets beyond display limits, duplicate dictionaries, missing economies, stable ties, nonfinite distances and callback order/count. Decode fixtures and validate callback observations outside operation catches. Verify immutable input preservation and keep unrepresentable null-world observations separate from executed native cases. Subsequent colony commands and advancement will have their own money/population and partial-mutation parity gate.
