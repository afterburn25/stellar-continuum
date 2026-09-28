using System.Runtime.CompilerServices;
using Game.Simulation.Economy;
using Game.Simulation.Generation;
using Game.Simulation.Species;
using Game.Simulation.Construction;

namespace Game.Simulation.Validation;

internal static class SpeciesDemographicEconomyValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidateEffectiveSpeciesPressureChangesPopulationWithoutDirectProductivityBonus();
        ValidateFoodAndWaterCarryingCapacity();
        Console.WriteLine("PASS: Species life history and exact natural environment drive population pace without direct economic bonuses");
    }

    private static void ValidateEffectiveSpeciesPressureChangesPopulationWithoutDirectProductivityBonus()
    {
        var terranGalaxy = CreateValidationGalaxy();
        var cryogenicGalaxy = CreateValidationGalaxy();

        var playerId = terranGalaxy.PlayerCivilizationId;
        Require(playerId == cryogenicGalaxy.PlayerCivilizationId,
            "deterministic validation galaxies disagreed on the player civilization");

        var terranColony = terranGalaxy.Colonies
            .Where(colony => colony.CivilizationId == playerId)
            .OrderBy(colony => colony.Id)
            .First();
        var cryogenicColony = cryogenicGalaxy.Colonies
            .Where(colony => colony.CivilizationId == playerId)
            .OrderBy(colony => colony.Id)
            .First(colony => colony.Id == terranColony.Id);

        const double initialPopulationMillions = 1000.0;
        const double simulationDays = 365.0;
        terranColony.PopulationMillions = initialPopulationMillions;
        cryogenicColony.PopulationMillions = initialPopulationMillions;
        terranColony.Infrastructure = cryogenicColony.Infrastructure = 1.0;
        terranColony.Stability = cryogenicColony.Stability = 1.0;
        terranColony.PopulationSpeciesId = SpeciesCatalog.TerranBaselineId;
        cryogenicColony.PopulationSpeciesId = SpeciesCatalog.CryogenicHydrocarbonId;

        var pressureView = new CurrentColonyPopulationTurnoverPressureView();
        var terranPressure = pressureView.Build(terranGalaxy, terranColony);
        var cryogenicPressure = pressureView.Build(cryogenicGalaxy, cryogenicColony);
        var terranFlow = EconomySimulation.GetCreditFlow(terranGalaxy, playerId);
        var cryogenicFlow = EconomySimulation.GetCreditFlow(cryogenicGalaxy, playerId);
        // Keep this test focused on intrinsic productivity. Habitat costs are intentionally
        // different, and an insolvent civilization now loses output through its explicit
        // operating-funding fraction rather than through a hidden species multiplier.
        terranGalaxy.Economies.First(economy => economy.CivilizationId == playerId).Credits = 1_000_000;
        cryogenicGalaxy.Economies.First(economy => economy.CivilizationId == playerId).Credits = 1_000_000;
        var terranSustenance = ColonySustenanceCapacity.GetSnapshot(terranGalaxy, terranColony);
        var cryogenicSustenance = ColonySustenanceCapacity.GetSnapshot(cryogenicGalaxy, cryogenicColony);

        RequireClose(
            terranPressure.EffectiveGrowthPaceFactor,
            terranPressure.IntrinsicGrowthPaceFactor * terranPressure.NaturalEnvironmentTurnoverFactor,
            "Terran effective demographic pace did not conserve its Species pressure inputs");
        RequireClose(
            cryogenicPressure.EffectiveGrowthPaceFactor,
            cryogenicPressure.IntrinsicGrowthPaceFactor * cryogenicPressure.NaturalEnvironmentTurnoverFactor,
            "cryogenic effective demographic pace did not conserve its Species pressure inputs");

        new EconomySimulation().Advance(terranGalaxy, simulationDays);
        new EconomySimulation().Advance(cryogenicGalaxy, simulationDays);

        var expectedTerranPopulation = ExpectedPopulation(initialPopulationMillions, terranColony.Stability,
            terranPressure.EffectiveGrowthPaceFactor, terranSustenance.SupportRatio, simulationDays);
        var expectedCryogenicPopulation = ExpectedPopulation(initialPopulationMillions, cryogenicColony.Stability,
            cryogenicPressure.EffectiveGrowthPaceFactor, cryogenicSustenance.SupportRatio, simulationDays);

        RequireClose(
            terranColony.PopulationMillions,
            expectedTerranPopulation,
            "Terran colony did not use the Species-owned effective population-turnover pace");
        RequireClose(
            cryogenicColony.PopulationMillions,
            expectedCryogenicPopulation,
            "cryogenic colony did not use the Species-owned effective population-turnover pace");

        var terranEconomy = terranGalaxy.Economies.First(economy => economy.CivilizationId == playerId);
        var cryogenicEconomy = cryogenicGalaxy.Economies.First(economy => economy.CivilizationId == playerId);

        // Population growth occurs after this tick's production inputs are computed. Species
        // identity still creates no hidden productivity modifier; its exact environmental
        // requirements may now create an explicit, separately reported habitat-support cost.
        RequireClose(
            terranEconomy.LastCreditsPerSecond + terranFlow.HabitatSupportPerDay,
            cryogenicEconomy.LastCreditsPerSecond + cryogenicFlow.HabitatSupportPerDay,
            "Species identity changed same-tick Credits beyond the explicit habitat-support cost");
        Require(Math.Abs(terranFlow.HabitatSupportPerDay - cryogenicFlow.HabitatSupportPerDay) > 0.000001,
            "different exact environmental requirements did not produce distinct visible habitat costs");
        RequireClose(
            terranEconomy.LastIndustryPerSecond,
            cryogenicEconomy.LastIndustryPerSecond,
            "Species identity/environmental turnover directly changed same-tick Industry productivity");
        RequireClose(
            terranEconomy.LastSciencePerSecond,
            cryogenicEconomy.LastSciencePerSecond,
            "Species identity/environmental turnover directly changed same-tick Science productivity");
    }

    private static void ValidateFoodAndWaterCarryingCapacity()
    {
        var galaxy = CreateValidationGalaxy();
        var colony = galaxy.Colonies.First(item => item.CivilizationId == galaxy.PlayerCivilizationId);
        var baseline = ColonySustenanceCapacity.GetSnapshot(galaxy, colony);
        Require(baseline.FoodCapacityMillions > 0.0 && baseline.WaterCapacityMillions > 0.0 && baseline.HousingCapacityMillions > 0.0,
            "founded colony had no represented food, potable-water or housing capacity");

        foreach (var (id, type, x) in new[]
                 {
                     (1001, "power_generator", -120f),
                     (1002, "controlled_agriculture", 120f),
                     (1003, "water_reclamation", 0f),
                 })
        {
            var definition = SurfaceBuildingCatalog.Find(type)!;
            colony.SurfaceBuildings.Add(new SurfaceBuildingState
            {
                Id = id, TypeId = type, X = x, Z = 160,
                IndustryProgress = definition.IndustryCost, IsComplete = true,
            });
        }
        var habitat = SurfaceBuildingCatalog.Find("habitat_complex")!;
        colony.SurfaceBuildings.Add(new SurfaceBuildingState
        {
            Id = 1004, TypeId = habitat.Id, X = 0, Z = -160,
            IndustryProgress = habitat.IndustryCost, IsComplete = true,
        });
        var expanded = ColonySustenanceCapacity.GetSnapshot(galaxy, colony);
        Require(expanded.BuiltFoodCapacityMillions == 2000.0 && expanded.BuiltWaterCapacityMillions == 2000.0 &&
            expanded.BuiltHousingCapacityMillions == 1000.0,
            "powered agriculture, water treatment and housing did not add their explicit support capacity");
        Require(expanded.SupportedPopulationMillions >= baseline.SupportedPopulationMillions + 999.999,
            "balanced food, water and housing construction did not raise sustainable population");

        colony.PopulationMillions = expanded.SupportedPopulationMillions * 1.20;
        var overCapacity = colony.PopulationMillions;
        new EconomySimulation().Advance(galaxy, 100.0);
        Require(colony.PopulationMillions < overCapacity,
            "population above available food/water/housing support continued growing without consequence");

        var reserveGalaxy = CreateValidationGalaxy();
        var reserveColony = reserveGalaxy.Colonies.First(item => item.CivilizationId == reserveGalaxy.PlayerCivilizationId);
        foreach (var (id, type, x) in new[]
                 {
                     (2001, "power_generator", -120f),
                     (2002, "controlled_agriculture", 120f),
                     (2003, "habitat_complex", 0f),
                 })
        {
            var definition = SurfaceBuildingCatalog.Find(type)!;
            reserveColony.SurfaceBuildings.Add(new SurfaceBuildingState
            {
                Id = id, TypeId = type, X = x, Z = 160,
                IndustryProgress = definition.IndustryCost, IsComplete = true,
            });
        }
        var reserveCapacity = ColonySustenanceCapacity.GetSnapshot(reserveGalaxy, reserveColony);
        reserveColony.PopulationMillions = reserveCapacity.WaterCapacityMillions + 100.0;
        reserveColony.StoredFoodPopulationDaysMillions = 0.0;
        reserveColony.StoredWaterPopulationDaysMillions = 500.0;
        var bufferedPopulation = reserveColony.PopulationMillions;
        var preview = ColonySustenanceReserves.Preview(reserveColony, reserveCapacity, 2.0);
        new EconomySimulation().Advance(reserveGalaxy, 2.0);
        Require(Math.Abs(reserveColony.PopulationMillions - bufferedPopulation) < .000001 &&
            reserveColony.StoredWaterPopulationDaysMillions < 500.0 &&
            Math.Abs(reserveColony.StoredWaterPopulationDaysMillions / reserveColony.PopulationMillions - preview.WaterReserveDays) < .000001,
            "potable-water reserve did not buffer a temporary production deficit");
        new EconomySimulation().Advance(reserveGalaxy, 10.0);
        Require(reserveColony.PopulationMillions < bufferedPopulation && reserveColony.StoredWaterPopulationDaysMillions == 0.0,
            "population did not decline after its potable-water reserve was exhausted");
        reserveColony.PopulationMillions = 1.0;
        reserveColony.StoredFoodPopulationDaysMillions = reserveColony.StoredWaterPopulationDaysMillions = 0.0;
        var refillCapacity = reserveCapacity with { FoodCapacityMillions = 10.0, WaterCapacityMillions = 10.0, HousingCapacityMillions = 10.0 };
        ColonySustenanceReserves.Advance(reserveColony, refillCapacity, 2.0);
        Require(reserveColony.StoredFoodPopulationDaysMillions > 0.0 && reserveColony.StoredWaterPopulationDaysMillions > 0.0,
            "empty reserves did not refill under repeated authoritative reserve advancement");
    }

    private static double ExpectedPopulation(double population, double stability, double demographicPace,
        double supportRatio, double simulationDays)
    {
        var rate = supportRatio >= 1.0
            ? EconomySimulation.BaselineDailyPopulationGrowthRate * stability * demographicPace *
              Math.Clamp(1.0 - (1.0 / supportRatio), 0.0, 1.0)
            : -EconomySimulation.UnsupportedPopulationDeclineRatePerDay * Math.Clamp(1.0 - supportRatio, 0.0, 1.0);
        return population * Math.Exp(rate * simulationDays);
    }

    private static Game.Simulation.Models.GalaxyState CreateValidationGalaxy() =>
        new GalaxyGenerator().Generate(
            0x4445_4D4F_4752_4F57L,
            new GalaxyGenerationSettings
            {
                SystemCount = 36,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 1,
                Radius = 420.0f,
            });

    private static void RequireClose(double actual, double expected, string message)
    {
        if (Math.Abs(actual - expected) > 0.000000001)
            throw new InvalidOperationException($"{message}: expected {expected}, got {actual}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
