# Gate 080 boundary

This draft ports current `FleetSaveDto` and `FleetCombatSaveDto`, including all
route, local-work, cargo, population, combat, tactical-loadout, and named-vessel
fields. Restore follows `CampaignSaveService.ToFleets` ordering exactly through
`CombatProfileRegistry.EnsureState`; capture follows `ToFleetDtos`, including
its mutation of live fleet combat state before population-species validation.

Null and empty persisted route lists remain distinguishable in `FleetSaveDto`.
Both restore to an owned empty runtime vector, as in the source. Legacy format
and population switches are explicit call inputs. The implementation adds no
role, design, tactical-loadout, vessel, cargo, route, or reference validation
beyond these source methods; those belong to the later galaxy reference pass.

C# `ToFleetDtos` calls `CloneLoadout` and `CloneVessel`; it does not retain the
live tactical object references. Native DTOs likewise own those nested values.
Tests prove value snapshots and mutation/failure ordering. A later general
capture owner must preserve this detached lifetime without loosening source
validation order.

This is not a galaxy JSON decoder, a historical envelope migration, or a player
save compatibility claim.
