# Gate 084: authoritative galaxy cross-reference validation

This isolated gate ports only `CampaignSaveService.ValidatePlanetaryReferences` (source lines 1145–1281). The public view borrows existing typed systems, bodies, civilizations, colonies, economies, fleets, and combat-intelligence observations for one call. Its optional active encounter pointer is the sole mutable input because source battle validation materializes legacy formation `InitialShipCount` before success or later failure.

Validation order is source order: active encounter; combat intelligence; conditional surface economies; body dictionary and system set; colonies including surface and resource-outpost state; then fleets including tactical, design, endurance, freight, route, civilian order, local work, and body-target rules. The adapter does not add general civilization/system validation, knowledge validation, planetary-catalog validation, or a payload codec.

Known nested failures are translated to five explicit source categories: invalid data, invalid operation, argument, argument out of range, and arithmetic overflow. Unexpected exceptions escape. Duplicate body IDs retain the source `ToDictionary` argument failure after conditional surface-economy checks. Resource-outpost lookup retains the existing first-matching economy rule and the source tolerances.

The actual-source fixture invokes the private C# method by reflection. It freezes the relevant world before and after, pins six directly invoked source files before and after, and includes early/later multi-invalid rows plus encounter materialization followed by a combat-intelligence failure.
