using System;
using Game.Simulation.Construction;
using Game.Simulation.Models;

namespace Game.Simulation.Economy;

/// <summary>Read-only surface explanations. These projections never advance simulation state.</summary>
public sealed record ColonySustenanceFeedbackSnapshot(double EffectiveSupportRatio, string LimitingSupply,
    bool IsBuffered, bool IsDeclining, double FoodDaysUntilDepletion, double WaterDaysUntilDepletion,
    string Status, string RecoveryAction);
public sealed record SurfaceConstructionFeedbackContext(double StoredMaterials, double SharedSiteDemand,
    bool HasRecentProduction);
public sealed record SurfaceConstructionFeedbackSnapshot(double StoredMaterials, double SharedSiteDemand,
    double MinimumDaysRemaining, string Status, string RecoveryAction);

public static class ColonySurfaceFeedbackReadModel
{
    public static ColonySustenanceFeedbackSnapshot GetSustenance(GalaxyState galaxy, ColonyState colony,
        ColonySustenanceCapacitySnapshot? capacity = null)
    {
        var support = capacity ?? ColonySustenanceCapacity.GetSnapshot(galaxy, colony);
        var population = Math.Max(.001, colony.PopulationMillions);
        var foodDeficit = Math.Max(0, population - support.FoodCapacityMillions);
        var waterDeficit = Math.Max(0, population - support.WaterCapacityMillions);
        var foodDays = foodDeficit <= .0000001 ? double.PositiveInfinity : Math.Max(0, colony.StoredFoodPopulationDaysMillions) / foodDeficit;
        var waterDays = waterDeficit <= .0000001 ? double.PositiveInfinity : Math.Max(0, colony.StoredWaterPopulationDaysMillions) / waterDeficit;
        if (colony.Kind == SettlementKind.ResourceOutpost)
            return new(1, support.LimitingSupply, false, false, foodDays, waterDays,
                $"Outpost support limit: {support.LimitingSupply}.", "Keep essential food, water and habitat services powered and staffed.");

        // A partial reserve can run out during the next simulation day. Preview uses the exact
        // reserve interval math but does not mutate the colony queried by the surface UI.
        var nextDay = ColonySustenanceReserves.Preview(colony, support, 1);
        var declining = nextDay.EffectiveSupportRatio < 1.0 - .0000001;
        var buffered = !declining && (foodDeficit > .0000001 || waterDeficit > .0000001);
        var capacityOrEarliest = foodDays < waterDays - .0000001 ? "food" :
            waterDays < foodDays - .0000001 ? "potable water" : support.LimitingSupply;
        var limiter = declining ? nextDay.LimitingSupply : buffered ? capacityOrEarliest : support.LimitingSupply;
        var status = declining
            ? $"Next-day projection: population declines because {nextDay.LimitingSupply} cannot cover current need."
            : buffered ? $"Next-day projection: capacity deficit is covered by reserves; {capacityOrEarliest} is the first limit."
            : $"Next-day projection: support remains stable; {support.LimitingSupply} is the capacity limit.";
        var recovery = declining || buffered ? RecoveryFor(limiter) :
            "Keep essential food, water and habitat services powered and staffed.";
        return new(nextDay.EffectiveSupportRatio, limiter, buffered, declining, foodDays, waterDays, status, recovery);
    }

    public static SurfaceConstructionFeedbackContext GetConstructionContext(GalaxyState galaxy, int civilizationId,
        double storedMaterials, bool hasRecentProduction) => new(Math.Max(0, storedMaterials),
            SurfaceConstruction.GetIndustryDemand(galaxy, civilizationId, 1), hasRecentProduction);

    public static SurfaceConstructionFeedbackSnapshot GetConstruction(SurfaceBuildingState building,
        SurfaceConstructionFeedbackContext context)
    {
        var definition = SurfaceBuildingCatalog.Find(building.TypeId) ?? throw new ArgumentException("Unknown surface building.");
        var remaining = Math.Max(0, definition.IndustryCost - building.IndustryProgress);
        var minimumDays = remaining / SurfaceConstruction.IndustryPerSitePerDay;
        var status = context.StoredMaterials <= .0000001
            ? context.HasRecentProduction ? "No material is stored; construction awaits newly produced material and shares it with other sites."
                : "No material is stored; construction awaits material availability."
            : context.SharedSiteDemand > SurfaceConstruction.IndustryPerSitePerDay + .0000001
                ? "Stored material is shared with other surface sites and also serves infrastructure projects and shipbuilding."
                : "Stored material is available for this authorized site.";
        var action = context.StoredMaterials <= .0000001
            ? "Restore material production or wait for production to reach storage."
            : context.SharedSiteDemand > SurfaceConstruction.IndustryPerSitePerDay + .0000001
                ? "Increase material availability or reduce competing active sites."
                : "Keep material availability steady to meet the minimum time.";
        return new(context.StoredMaterials, context.SharedSiteDemand, minimumDays, status, action);
    }

    private static string RecoveryFor(string limiter) => limiter.Contains("water", StringComparison.Ordinal)
        ? "Build Water reclamation, then prioritize its workers and power."
        : limiter.Contains("food", StringComparison.Ordinal) ? "Build Controlled agriculture, then prioritize its workers and power."
        : "Build Habitat complex to raise supported housing.";
}
