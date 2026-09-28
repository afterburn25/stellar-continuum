# Player industry priority

`CivilizationEconomyState.IndustryPriority` is an optional persisted override. An unset value
preserves the existing strategic provider, which keeps AI behavior intact; the player UI presents
that normal player fallback as Balanced. Explicit choices are Balanced (1:1), Infrastructure First
(3:1), and Shipbuilding First (1:3).

The weighted allocator still controls all spending. A priority only matters under competing demand;
unused share reflows immediately. Infrastructure includes both surface construction sites and empire
projects. The Economy page exposes `IndustryPriority_Balanced`,
`IndustryPriority_InfrastructureFirst`, and `IndustryPriority_ShipbuildingFirst`.

The ordinary `GalaxySimulationStepCoordinator` binds the provider to each advanced galaxy, so a
coordinator reused for a replacement campaign cannot retain an earlier override. Explicitly
injected allocation policies still take precedence. Save and load both reject unknown enum values.
