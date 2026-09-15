# Matched combat command runtime

Port the command surfaces in `CombatOrderPreview.cs`, `CombatCommandBatchPreviewService.cs`, `CombatCommandBatchService.cs` and `CombatCommandRuntime.cs` over the accepted native `CombatSimulation`. This is required before the campaign coordinator exposes previews and issuance together. It does not introduce tactical movement, diplomacy ownership or foreign-intelligence disclosure.

## One political permission source

The source runtime constructs simulation and previews from the same hostility object. The native runtime must retain one owned policy instance, shared by its preview and simulation calls. Copying a stateful `std::function` independently into these services can split its state and violate this rule. A shared owned callable with adapters that capture its shared handle is suitable; no callback may capture a movable runtime's raw `this`. The default policy remains peaceful. Test ordered calls through preview, issuance, engage-hostiles and advancement with a stateful and a throwing policy.

## Preview and batch behavior

Preview is read-only: it must not call the mutating combat-state initializer. Select the first active owned fleet with the requested ID. Resolve a known combat profile or the role's pristine fallback without rewriting the fleet. Preserve source validation order and exact messages for Hold, Defend, Attack, Retreat and unknown enum values. Defend requires an armed fleet already present in an existing target system. Attack checks weapon, requested active foreign target, co-location, valid-profile disengagement in this system, then political permission. Missing/invalid target combat profiles do not establish disengagement. Preview carries an owned copy of the requested order; it does not normalize it to the eventual issued order.

Both batch surfaces distinct their input IDs and visit them in ascending order. Empty input has AllAccepted=false and AnyAccepted=false. Return every per-fleet result, counts and aggregate flags in source order. Issuance may partially accept a heterogeneous selection. If a later callback throws, earlier fleet mutations remain and the overall operation returns no result. Preview never changes world state, including on failure.

## Engage hostiles

Find the first active owned actor and require a current system. Enumerate active foreign fleets in that system by stable ascending ID. For each candidate, use the matched preview; issue the first accepted attack through the retained simulation. A change or exception in the shared policy between preview and issuance must retain the source result and partial state. If none qualify, retain the source's generic failure text rather than disclosing peaceful, disengaged or absent rival identities. This method is the privacy-preserving command bridge, not a new detection rule.

## Composition and acceptance

Expose the runtime's retained simulation for the coordinator's combat step so engagement history survives commands and ticks. Preserve the source constructor rule against supplying both a raw simulation and a matched runtime to the later coordinator. Raw legacy simulation injection must leave previews unavailable rather than inventing a second peaceful policy.

The native value wrapper is movable and deliberately noncopyable. Moving it transfers the single retained simulation and engagement cache while the simulation adapter and preview surface continue to share the same owned hostility callable. A native-only lifetime probe previews before the move, then issues and advances after the move. This checks C++ ownership safety rather than claiming an additional C# behavior.

Use actual C# multi-command fixtures. Cover preview immutability with nondefault legacy combat and vessel state; all order outcomes; unknown profiles and enums; duplicate/empty batch IDs; mixed acceptance; first-match duplicates; stable engage candidate selection; default peace; policy changes and throws after earlier commands; and subsequent advancement through the same retained simulation. Decode input, resolve fixture references and validate command kinds before the production exception capture; serialize and compare all results, callback observations and complete world snapshots afterward. Compare integer state exactly. Source-only null arguments and explicit native overflow boundaries remain separately identified.
