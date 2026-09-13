# Survey operations profiling boundary

Authority is `Exploration/SurveyOperationsProfile.cs`. This gate ports only the
deterministic profiler, its result, and `SurveyOperationalHazard`. Mission
selection, reconnaissance, survey advancement, discoveries, rewards, and
knowledge mutation remain separate work.

The profiler resolves the first system with the requested ID in source order.
It then considers every body whose `SystemId` matches that requested ID, also in
source order. Moons are bodies whose kind is exactly `Moon`; planets include
both planets, dwarf planets, and unrecognized imported enum values. Missing
systems fail with `InvalidOperationException("Unknown system {id}.")`. Duplicate
system IDs are not rejected because C# `FirstOrDefault` selects the first.

Work starts at eight days, adds 0.85 per planet and 0.35 per moon, then the
larger of the archetype and primary-star workload. It adds at most 2.4 anomaly
days and 1.5 rare-resource days, and finally clamps to [9, 28]. Secondary and
tertiary stars, habitability flags, names, coordinates, and bodies belonging to
other systems do not affect the profile. `ProgressPerDay` is exactly one divided
by the bounded estimate.

Hazard is severe for the source severe star/archetype set or maximum matching
body radiation at least 0.72. It is elevated for the source elevated set or
radiation at least 0.40, and routine otherwise. The native implementation
preserves the source `Enumerable.Max(double)` behavior for imported NaN values:
NaN values are ignored when a numeric value exists, while an all-NaN nonempty
set produces NaN and therefore does not cross either radiation threshold.
Infinity is compared normally. Physical-state validators normally exclude
these nonfinite values, but the profiler itself does not call those validators.

Native callers pass typed spans rather than a nullable `GalaxyState`. A null
galaxy reference and null required records are not representable at this API
boundary. The C# `ArgumentNullException` behavior is documented here and is not
reported as a native parity case with a fabricated result.
