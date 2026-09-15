# Strategic intent migration boundary

This gate ports `CivilizationStrategicIntentBuilder`, `StrategicIndustryPriorityProvider`, and `StrategicShipbuildingPreferenceProvider`. It owns copied plans, priorities, strings, intent maps, and shipbuilding preference records. It does not invoke the strategic planner, evaluator, director, runtime coordinator, industry allocator, or simulation mutation.

The zero-based source priority enum is retained. Builder output initializes all declared priority weights to zero, stores unknown numeric priority values, and lets the last duplicate priority overwrite earlier weights. The first input priority remains the summary primary. Score clamping, `Math.Max`, NaN behavior, role thresholds, defer thresholds, metadata, and custom `0.00` summary formatting follow the source.

Providers overwrite by civilization ID. Industry weights use the source formulas and clamp the final values to `[0.25, 4.0]`; raw published intent weights are not pre-clamped. Missing and removed IDs return `{1,1}` and a native preference record carrying the requested civilization ID, no role, and no deferral. Review publication keys both providers from `review.Intent.CivilizationId`, independently of the plan ID. Clear and remove affect only provider state.

The retained source oracle sets `CurrentCulture` and `CurrentUICulture` to invariant culture before any formatting. Its ordered fixture records typed builder inputs and provider operations, complete outputs and provider state after each operation, and source input-preservation evidence. Null source arguments are recorded with explicit source-only metadata because typed native references cannot represent them.

Native provider ownership checks mutate caller-owned values after publication and verify the provider retained its own copy. This is a native value/lifetime boundary check, separate from source parity: the C# industry provider retains the intent record and its `IReadOnlyDictionary` reference.
