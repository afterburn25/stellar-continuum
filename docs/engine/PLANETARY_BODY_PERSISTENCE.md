# Gate 083B planetary-body persistence boundary

This gate ports only the private `CampaignSaveService` planetary-body restore, validation, and capture adapters. It does not implement a galaxy envelope, historical catalog upgrades, system generation, colonies, knowledge, fleets, or player-save compatibility.

Restore retains the parser-facing distinctions that affect the source result: a missing/null body list, an empty list, null list elements, nullable names, and nullable environments. It materializes all non-null DTOs before validation. Validation then follows the source order: nonempty and distinct IDs globally; per body kind, environment, physical values with the original inner exception, system membership, cyclic ancestry, primary/moon parent shape, and parent existence/kind/system.

Capture intentionally represents the surrounding save-service behavior by validating the source bodies before projecting detached DTOs. The returned collection is always present and contains no null elements. Inputs and outputs are owned across the API boundary; system spans are borrowed only for each call.

The C# runtime supplies valid Unicode strings. Native invalid UTF-8 text and general JSON parser diagnostics remain parser-layer boundaries. The gate does not claim that every malformed serialized object is representable through the typed DTO API.
