# Observer-local strategic planning migration boundary

The next strategic gate ports `KnowledgeSnapshot.cs`, `StrategicDecisionEvaluator.cs` and `CivilizationStrategicPlanner.cs`. Reuse the plan, priority, own-state and intent records introduced by gate 032, and the native civilization traits. This gate has no authoritative foreign fleet or economy view. It accepts only the source observer-local knowledge records and self-knowledge. The director, own-state builder, review clock and diplomacy adapter remain separate composition work.

## Knowledge and evaluation

Retain the original knowledge dictionary's insertion order and keys as well as each record's CivilizationId. The source iterates values; dictionary key/record ID differences and equal-scoring first-observed threats must not silently become sorted-ID behavior. Records include explicit HasMilitaryEstimate, defaulting true only when constructed that way by the source. A known diplomatic contact is not automatically a military estimate. Do not infer missing force strength from the galaxy.

KnownCivilization midpoint and freshness keep source arithmetic, clamps and stale-tick behavior. C# unchecked long subtraction can wrap at extreme tick inputs; implement defined native behavior or label an explicit boundary, never signed-overflow undefined behavior. The empty knowledge provider, if included, validates nonnegative nowTick and returns no foreign observations. The actual diplomacy adapter is not part of this gate.

EvaluateWar returns the source defensive, unknown-strength assessment immediately when no military estimate exists. Otherwise preserve confidence/freshness, conservative estimated threat, strength ratio, survival floor, honor override and ordered score contributions. NaN and infinity follow source Math semantics; do not normalize malformed estimates to plausible information. Exact honor behavior can override the survival/score gates only where the source does.

## Cached planner

Clamp review interval to at least one tick. Source required-argument validation happens before consulting a cached plan. An unforced request strictly before cached ReviewAfterTick returns the previous owned plan even if new inputs differ. At equality, or when forced, compute a new plan. Checked nowTick plus interval must throw before overwriting the old cache on overflow. Invalidate, RemoveCivilization and Clear affect only the specified caches. Returned plans cannot dangle after a later review or cache removal.

Build priorities in the original source order. Preserve supply and research predicates, industrial scarcity, exploration, colony logistics restraint and fleet capability checks. Strongest estimated threat iterates observed records in insertion order, skipping those without estimates; equal threat scores keep the first record. The source applies its existing known-war multiplier as written, even if a future balancing change might choose a different policy. Unknown-strength known wars still produce defensive urgency without fabricated force estimates; choose the lowest record CivilizationId for this branch. Equal relationship opportunity scores retain observed insertion order. Final priorities sort by descending .NET Double.CompareTo score (NaN after finite) and then enum value. Do not replace the intent builder's first-priority summary with a different winner rule.

The public planner itself does not validate future ObservedAtTick; the source director does so later. Preserve that boundary. Similarly, the planner accepts its supplied traits and self-state rather than reading the world. Human and ancient scheduling exclusions belong to the runtime coordinator, not this planner.

## Acceptance and composition

Use actual C# source calls for war assessments, freshness and multistep planner/cache scenarios, not a second mathematical implementation as the oracle. Compare every priority, order, reason, generated/review tick, cache survival after errors, and source input immutability. Include no-estimate contacts, known war without strength, honor, low/fresh/stale confidence, equal-score insertion ties, unknown numeric values, threshold boundaries, negative/overflow ticks, forced reviews, changed inputs before/at due time, removal and clearing. Distinguish unrepresentable source-null observations and explicit native arithmetic boundaries from parity cases.

After this gate, the own-state builder must combine accepted logistics, exploration, habitability, legacy capabilities, technology and combat readiness without reading hidden rivals. The director then joins that builder, planner and intent builder. The runtime must retain the source clock, campaign reset and ordered publication into industry/shipbuilding preferences. None of those stages may be called integrated AI before their real source behavior is connected and tested.
