# Gate 082: colony, economy, legacy technology, and construction persistence

This isolated adapter ports only `CampaignSaveService` conversion boundaries `ToColonies`, `ToEconomies`, `ToTechnologies`, `ToConstructionStates`, and their capture counterparts. It does not decode a galaxy payload or validate cross-object references.

## Source boundary

The fixture generator invokes the actual private C# methods by reflection under invariant culture and pins `src/Game/Persistence/CampaignSaveService.cs` before and after generation. The 57 replay rows cover current and historical colony versions 7/8/11/12, population species resolution, nullable DTO boundaries, complete surface-building fields, economy validation asymmetry, named floating-point values, legacy technology insertion behavior, UTF-16 ordinal capture ordering, discarded partial results, and every construction validation class in source first-error order.

DTO collections remain nullable only where JSON can supply null and the source observes it. Runtime collections retain their existing non-null native types. Restore does not normalize values beyond the source rules.

## Ownership

Native capture returns an owned value snapshot. The C# `ToColonyDtos` helper assigns the live `SurfaceBuildings` list by reference, as demonstrated by the fixture metadata alias probe; the enclosing `CapturePayload` serializes that helper output synchronously. The native DTO therefore matches the durable payload value at capture time while deliberately offering a detached lifetime. Dedicated native probes mutate live surface buildings, technology IDs, and construction IDs/orders after capture and verify retained DTOs are unchanged.

The adapter excludes shipyards, knowledge, a general galaxy codec, and all subsequent cross-reference validation.
