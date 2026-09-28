using System;
using System.Linq;
using Game.Simulation.Construction;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Economy;

public sealed record ColonySustenanceCapacitySnapshot(
    double NaturalFoodCapacityMillions,
    double NaturalWaterCapacityMillions,
    double NaturalHousingCapacityMillions,
    double BuiltFoodCapacityMillions,
    double BuiltWaterCapacityMillions,
    double BuiltHousingCapacityMillions,
    double FoodCapacityMillions,
    double WaterCapacityMillions,
    double HousingCapacityMillions,
    double SupportedPopulationMillions,
    double SupportRatio,
    string LimitingSupply);

public static class ColonySustenanceCapacity
{
    public const double SealedBaselineCapacityPerInfrastructureMillions = 500.0;
    public const double NaturalBiosphereCapacityPerEarthAreaMillions = 12_000.0;

    public static ColonySustenanceCapacitySnapshot GetSnapshot(
        GalaxyState galaxy, ColonyState colony, SurfaceColonyOutput? surfaceOutput = null)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(colony);
        var infrastructure = Math.Clamp(colony.Infrastructure, 0.1, 5.0);
        var sealedCapacity = SealedBaselineCapacityPerInfrastructureMillions * infrastructure;
        var body = colony.PlanetaryBodyId is int bodyId
            ? galaxy.PlanetaryBodies.FirstOrDefault(candidate => candidate.Id == bodyId && candidate.SystemId == colony.SystemId)
            : null;
        double naturalFood;
        double naturalWater;
        double naturalHousing;
        if (body is null)
        {
            // Legacy orbital/bodyless settlements retain their current population as a fixed
            // supported baseline, but gain no automatic headroom for endless growth.
            naturalFood = naturalWater = naturalHousing = Math.Max(0.0, colony.PopulationMillions - sealedCapacity);
        }
        else
        {
            var assessment = new SpeciesPlanetaryHabitabilityEvaluator().Evaluate(body, colony.PopulationSpeciesId);
            var area = Math.Clamp(body.RadiusEarth * body.RadiusEarth, 0.02, 25.0);
            var biologicalFit = Math.Clamp(assessment.Environment.NaturalHabitability, 0.0, 1.0);
            var naturalBase = NaturalBiosphereCapacityPerEarthAreaMillions * area * infrastructure;
            naturalFood = naturalBase * biologicalFit;
            naturalWater = naturalBase * biologicalFit * Math.Clamp(assessment.Environment.SolventSuitability, 0.0, 1.0);
            naturalHousing = naturalBase * biologicalFit;
        }

        var surface = surfaceOutput ?? SurfaceConstruction.GetOutput(colony);
        var food = sealedCapacity + naturalFood + surface.FoodCapacityMillions;
        var water = sealedCapacity + naturalWater + surface.WaterCapacityMillions;
        var housing = sealedCapacity + naturalHousing + surface.HousingCapacityMillions;
        var supported = Math.Max(0.001, Math.Min(food, Math.Min(water, housing)));
        var ratio = colony.PopulationMillions <= 0.0 ? 1.0 : supported / colony.PopulationMillions;
        var minimum = Math.Min(food, Math.Min(water, housing));
        var limiting = new[] { (Name: "food", Value: food), (Name: "potable water", Value: water), (Name: "housing", Value: housing) }
            .Where(item => Math.Abs(item.Value - minimum) <= 0.001).Select(item => item.Name).ToArray();
        return new(naturalFood, naturalWater, naturalHousing, surface.FoodCapacityMillions, surface.WaterCapacityMillions,
            surface.HousingCapacityMillions, food, water, housing, supported, ratio, string.Join(" and ", limiting));
    }
}

public sealed record ColonySustenanceReserveSnapshot(
    double FoodReserveDays,
    double WaterReserveDays,
    double EffectiveSupportRatio,
    string LimitingSupply);

public static class ColonySustenanceReserves
{
    public const double MaximumFoodReserveDays = 30.0;
    public const double MaximumWaterReserveDays = 7.0;

    public static ColonySustenanceReserveSnapshot Advance(
        ColonyState colony,
        ColonySustenanceCapacitySnapshot capacity,
        double simulationDays)
    {
        var calculation = Calculate(colony, capacity, simulationDays);
        colony.StoredFoodPopulationDaysMillions = calculation.FoodReserve;
        colony.StoredWaterPopulationDaysMillions = calculation.WaterReserve;
        return calculation.Snapshot;
    }

    /// <summary>Uses the authoritative reserve interval math without changing colony state.</summary>
    public static ColonySustenanceReserveSnapshot Preview(ColonyState colony,
        ColonySustenanceCapacitySnapshot capacity, double simulationDays) =>
        Calculate(colony, capacity, simulationDays).Snapshot;

    private static ReserveCalculation Calculate(ColonyState colony,
        ColonySustenanceCapacitySnapshot capacity, double simulationDays)
    {
        var population = Math.Max(0.001, colony.PopulationMillions);
        var foodMaximum = Math.Max(population, capacity.FoodCapacityMillions) * MaximumFoodReserveDays;
        var waterMaximum = Math.Max(population, capacity.WaterCapacityMillions) * MaximumWaterReserveDays;
        var storedFood = Math.Clamp(colony.StoredFoodPopulationDaysMillions, 0.0, foodMaximum);
        var storedWater = Math.Clamp(colony.StoredWaterPopulationDaysMillions, 0.0, waterMaximum);

        var food = ApplyBalance(storedFood,
            capacity.FoodCapacityMillions, population, foodMaximum, simulationDays);
        var water = ApplyBalance(storedWater,
            capacity.WaterCapacityMillions, population, waterMaximum, simulationDays);
        var effectiveFood = food.EffectiveSupply;
        var effectiveWater = water.EffectiveSupply;
        var effective = Math.Min(effectiveFood, Math.Min(effectiveWater, capacity.HousingCapacityMillions));
        var minimum = effective;
        var limiting = new[]
            {
                (Name: "food", Value: effectiveFood),
                (Name: "potable water", Value: effectiveWater),
                (Name: "housing", Value: capacity.HousingCapacityMillions),
            }
            .Where(item => Math.Abs(item.Value - minimum) <= 0.001)
            .Select(item => item.Name);
        return new(food.Reserve, water.Reserve, new(food.Reserve / population,
            water.Reserve / population, Math.Max(0.0, effective / population), string.Join(" and ", limiting)));
    }

    private static (double EffectiveSupply, double Reserve) ApplyBalance(double reserve, double dailyProduction, double dailyDemand,
        double maximumReserve, double simulationDays)
    {
        if (dailyProduction >= dailyDemand)
        {
            reserve = Math.Min(maximumReserve, reserve + (dailyProduction - dailyDemand) * simulationDays);
            return (dailyDemand, reserve);
        }

        var deficit = dailyDemand - dailyProduction;
        var withdrawn = Math.Min(reserve, deficit * simulationDays);
        reserve -= withdrawn;
        return (dailyProduction + (simulationDays <= 0.0 ? 0.0 : withdrawn / simulationDays), reserve);
    }

    private sealed record ReserveCalculation(double FoodReserve, double WaterReserve,
        ColonySustenanceReserveSnapshot Snapshot);
}
