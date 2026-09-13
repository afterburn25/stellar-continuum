using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Construction;

public static partial class SurfaceConstruction
{
    // Reading old worlds assigns virtual slots without mutating the campaign. A successful
    // construction/removal order persists the mapping before changing the building list.
    public static IReadOnlyDictionary<int, int> GetBuildingSlots(ColonyState colony)
    {
        var result = colony.SurfaceBuildings.Where(building => building.SlotIndex.HasValue)
            .ToDictionary(building => building.Id, building => building.SlotIndex!.Value);
        var occupied = result.Values.ToHashSet();
        foreach (var building in colony.SurfaceBuildings.Where(building => !building.SlotIndex.HasValue).OrderBy(building => building.Id))
        {
            var slot = Enumerable.Range(0, MaximumBuildings).First(index => !occupied.Contains(index));
            result[building.Id] = slot; occupied.Add(slot);
        }
        return result;
    }

    private static void AssignLegacySlots(ColonyState colony)
    {
        var mapping = GetBuildingSlots(colony);
        foreach (var building in colony.SurfaceBuildings) building.SlotIndex = mapping[building.Id];
    }

    public static ConstructionOrderResult BuildInSlot(GalaxyState galaxy, int civilizationId, int colonyId, int slot, string typeId)
    {
        var colony = galaxy.Colonies.FirstOrDefault(item => item.Id == colonyId && item.CivilizationId == civilizationId);
        if (colony is null) return new(false, "You can build only on a planet you own.");
        if (colony.SurfaceHubLevel <= 0) return new(false, "Complete the Command Center to unlock planetary building slots.");
        if (slot < 0 || slot >= GetBuildingCapacity(colony)) return new(false, "Upgrade the Command Center to unlock this building slot.");
        if (GetBuildingSlots(colony).Values.Contains(slot)) return new(false, "This building slot is occupied or reserved by construction.");
        // Coordinates are retained solely for old save/terrain compatibility. The player
        // chooses a slot, never a terrain location. Preserve every legacy building in place.
        for (var ring = 1; ring <= 7; ring++)
            for (var n = 0; n < ring * 12; n++)
            {
                var angle = n * Math.Tau / (ring * 12);
                var x = (float)(Math.Cos(angle) * ring * 60);
                var z = (float)(Math.Sin(angle) * ring * 60);
                if (PlacementError(colony.SurfaceBuildings, typeId, x, z, 0) is not null) continue;
                var result = Place(galaxy, civilizationId, colonyId, typeId, x, z, 0);
                if (result.Accepted) colony.SurfaceBuildings.MaxBy(building => building.Id)!.SlotIndex = slot;
                return result;
            }
        return new(false, "No compatible surface site is available for this building.");
    }
}
