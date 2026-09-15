# Combat simulation migration contract

This gate ports `CombatSimulation` and its direct command, event, and own-force
records. It uses the existing combat profile/state registry. The world view borrows
immutable systems and mutable fleets; political hostility is a callback whose
default is peaceful. A simulation instance owns its active engagement keys across
calls, so engagement-start and engagement-end events depend on earlier advances.

Fleet lookup and every `EnsureState` mutation follow source order. Rejected orders
therefore may initialize or normalize combat state. Active fleets are sorted by ID
and converted to a unique-ID dictionary before target planning. Explicit attacks
are the sole aggression source; armed targets return fire, and eligible defenders
select the lowest-ID current attacker. Fire actions are planned before damage is
applied and execute in target-ID then source-ID order, preserving simultaneous
volleys. Damage crosses shields, armor, then hull. Destruction clears represented
mission fields and embarked population as the source does.

Retreat progress advances only while actively threatened; an unthreatened retreat
completes immediately. Cooldown and retreat comparisons retain the source epsilon,
NaN propagation, ordering, and event order. Force summaries lazily ensure all active
owned fleet states and include only armed profiles.

C# permits NaN and positive infinity past `simulationDeltaDays <= 0`, then relies on
runtime floating-to-integer conversion in volley calculation. Native conversion of
those values would be undefined, so the native boundary rejects nonfinite positive
or NaN deltas before mutation. Finite deltas whose computed volley count exceeds
the native integer range are also rejected after source-ordered lazy state setup;
the full resulting fleet state is verified before a retry. Boundary tests are
reported separately from actual C# parity cases. This gate does not implement
massive combat, diplomacy ownership, intelligence estimates, movement, or a combat
event archive.

The JSON bridge represents `TacticalLoadout` only as null because the scoped source
simulation does not read or mutate that field. Every fixture fleet carries a
nondefault `MassiveVesselState`, and the native consumer compares every represented
vessel-history field before and after every command.
