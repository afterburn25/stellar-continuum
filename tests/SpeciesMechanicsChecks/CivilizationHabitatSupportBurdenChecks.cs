using System.Runtime.CompilerServices;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;

internal static class CivilizationHabitatSupportBurdenChecks
{
    [ModuleInitializer]
    internal static void Initialize()
    {
        Run();
        Console.WriteLine("PASS: civilization/system habitat-support burden aggregation");
    }

    public static void Run()
    {
        ValidateGeneratedCivilizationBurdenConservesColonyTotals();
        ValidateUnknownFallbackAndMixedSpeciesStayDistinct();
        ValidateAggregationIsReadOnlyAndIgnoresZeroPopulation();
    }

    private static void ValidateGeneratedCivilizationBurdenConservesColonyTotals()
    {
        var galaxy = CreateGalaxy();
        var colonyView = new CurrentColonyHabitatSupportBurdenView();
        var civilizationView = new CurrentCivilizationHabitatSupportBurdenView(colonyView);

        foreach (var civilization in galaxy.Civilizations.OrderBy(civilization => civilization.Id))
        {
            var owned = galaxy.Colonies
                .Where(colony => colony.CivilizationId == civilization.Id && colony.PopulationMillions > 0.0)
                .OrderBy(colony => colony.Id)
                .Select(colony => colonyView.Build(galaxy, colony.Id))
                .ToArray();
            var aggregate = civilizationView.Build(galaxy, civilization.Id);

            Require(aggregate.PopulatedColonyCount == owned.Length,
                $"civilization {civilization.Id} populated colony count did not conserve colony burdens");
            Require(aggregate.ExactBodyColonyCount == owned.Count(burden => burden.UsesExactOccupiedBody),
                $"civilization {civilization.Id} exact-body count did not conserve colony burdens");
            Require(aggregate.LegacyUnknownEnvironmentColonyCount == owned.Count(burden => !burden.UsesExactOccupiedBody),
                $"civilization {civilization.Id} unknown-environment count did not conserve colony burdens");
            Require(aggregate.HabitatSupportedFallbackColonyCount ==
                    owned.Count(burden => burden.Environment?.UsesPrototypeHabitatSupportedFallback == true),
                $"civilization {civilization.Id} fallback count did not conserve colony burdens");
            Require(aggregate.DistinctSpeciesCount == owned.Select(burden => burden.SpeciesId).Distinct().Count(),
                $"civilization {civilization.Id} distinct Species count was incorrect");
            Require(aggregate.Systems.Select(system => system.SystemId)
                    .SequenceEqual(aggregate.Systems.Select(system => system.SystemId).OrderBy(id => id)),
                $"civilization {civilization.Id} system burdens were not deterministically ordered");

            var expected = owned.Aggregate(
                HabitatSupportBurdenTotals.Zero,
                (current, burden) => current.Plus(burden));
            RequireTotalsEqual(aggregate.Totals, expected,
                $"civilization {civilization.Id} aggregate totals did not conserve colony burdens");

            foreach (var system in aggregate.Systems)
            {
                var systemOwned = owned.Where(burden => burden.SystemId == system.SystemId).ToArray();
                Require(system.PopulatedColonyCount == systemOwned.Length,
                    $"system {system.SystemId} colony count did not conserve colony burdens");
                Require(system.ExactBodyColonyCount == systemOwned.Count(burden => burden.UsesExactOccupiedBody),
                    $"system {system.SystemId} exact-body count was incorrect");
                Require(system.LegacyUnknownEnvironmentColonyCount == systemOwned.Count(burden => !burden.UsesExactOccupiedBody),
                    $"system {system.SystemId} unknown-environment count was incorrect");
                Require(system.HabitatSupportedFallbackColonyCount ==
                        systemOwned.Count(burden => burden.Environment?.UsesPrototypeHabitatSupportedFallback == true),
                    $"system {system.SystemId} fallback count was incorrect");
                Require(system.DistinctSpeciesCount == systemOwned.Select(burden => burden.SpeciesId).Distinct().Count(),
                    $"system {system.SystemId} distinct Species count was incorrect");
                RequireTotalsEqual(
                    system.Totals,
                    systemOwned.Aggregate(HabitatSupportBurdenTotals.Zero, (current, burden) => current.Plus(burden)),
                    $"system {system.SystemId} totals did not conserve colony burdens");
            }
        }
    }

    private static void ValidateUnknownFallbackAndMixedSpeciesStayDistinct()
    {
        var galaxy = CreateGalaxy();
        var civilization = galaxy.Civilizations.OrderBy(civilization => civilization.Id).First();
        var baseline = new CurrentCivilizationHabitatSupportBurdenView().Build(galaxy, civilization.Id);

        var legacySpecies = SpeciesCatalog.All.First(species => species.Id != civilization.SpeciesId);
        var legacy = new ColonyState
        {
            Id = NextColonyId(galaxy),
            CivilizationId = civilization.Id,
            SystemId = civilization.HomeSystemId,
            PlanetaryBodyId = null,
            Name = "Aggregate Legacy Unknown Environment",
            PopulationSpeciesId = legacySpecies.Id,
            PopulationMillions = 333.0,
            Infrastructure = 1.0,
            Stability = 1.0,
        };
        galaxy.Colonies.Add(legacy);

        var habitability = new SpeciesPlanetaryHabitabilityEvaluator();
        var fallbackCandidate = (
            from species in SpeciesCatalog.All
            from body in galaxy.PlanetaryBodies
            let assessment = habitability.Evaluate(body, species.Id)
            where assessment.Viability == SpeciesColonizationViability.HabitatSupportedFallback
            orderby body.SystemId, body.Id, species.Id
            select new { Species = species, Body = body })
            .FirstOrDefault()
            ?? throw new InvalidOperationException("Validation galaxy contained no habitat-supported fallback pair.");

        var fallback = new ColonyState
        {
            Id = NextColonyId(galaxy),
            CivilizationId = civilization.Id,
            SystemId = fallbackCandidate.Body.SystemId,
            PlanetaryBodyId = fallbackCandidate.Body.Id,
            Name = "Aggregate Habitat Supported Fallback",
            PopulationSpeciesId = fallbackCandidate.Species.Id,
            PopulationMillions = 444.0,
            Infrastructure = 1.0,
            Stability = 1.0,
        };
        galaxy.Colonies.Add(fallback);

        var aggregate = new CurrentCivilizationHabitatSupportBurdenView().Build(galaxy, civilization.Id);
        Require(aggregate.PopulatedColonyCount == baseline.PopulatedColonyCount + 2,
            "aggregate did not include both additional populated colonies");
        Require(aggregate.LegacyUnknownEnvironmentColonyCount == baseline.LegacyUnknownEnvironmentColonyCount + 1,
            "legacy null-body colony was not preserved as unknown environment");
        Require(aggregate.HabitatSupportedFallbackColonyCount == baseline.HabitatSupportedFallbackColonyCount + 1,
            "exact fallback colony was not preserved as habitat-supported fallback");
        Require(aggregate.ExactBodyColonyCount == baseline.ExactBodyColonyCount + 1,
            "fallback exact-body colony did not increment exact occupancy count");

        var expectedDistinctSpecies = galaxy.Colonies
            .Where(colony => colony.CivilizationId == civilization.Id && colony.PopulationMillions > 0.0)
            .Select(colony => colony.PopulationSpeciesId)
            .Distinct()
            .Count();
        Require(aggregate.DistinctSpeciesCount == expectedDistinctSpecies,
            "aggregate collapsed mixed Species identity into the civilization founding Species");

        var legacySystem = aggregate.Systems.Single(system => system.SystemId == legacy.SystemId);
        Require(legacySystem.LegacyUnknownEnvironmentColonyCount >= 1,
            "system aggregation lost the legacy unknown-environment classification");
        var fallbackSystem = aggregate.Systems.Single(system => system.SystemId == fallback.SystemId);
        Require(fallbackSystem.HabitatSupportedFallbackColonyCount >= 1,
            "system aggregation lost the habitat-supported fallback classification");
    }

    private static void ValidateAggregationIsReadOnlyAndIgnoresZeroPopulation()
    {
        var galaxy = CreateGalaxy();
        var civilization = galaxy.Civilizations.OrderBy(civilization => civilization.Id).First();
        var before = galaxy.Colonies
            .Where(colony => colony.CivilizationId == civilization.Id)
            .OrderBy(colony => colony.Id)
            .Select(colony => new
            {
                colony.Id,
                colony.SystemId,
                colony.PlanetaryBodyId,
                colony.PopulationSpeciesId,
                colony.PopulationMillions,
                colony.Infrastructure,
                colony.Stability,
            })
            .ToArray();

        var zero = new ColonyState
        {
            Id = NextColonyId(galaxy),
            CivilizationId = civilization.Id,
            SystemId = civilization.HomeSystemId,
            PlanetaryBodyId = null,
            Name = "Zero Population Aggregate Control",
            PopulationSpeciesId = civilization.SpeciesId,
            PopulationMillions = 0.0,
            Infrastructure = 1.0,
            Stability = 1.0,
        };
        galaxy.Colonies.Add(zero);

        var aggregate = new CurrentCivilizationHabitatSupportBurdenView().Build(galaxy, civilization.Id);
        Require(aggregate.PopulatedColonyCount == before.Count(entry => entry.PopulationMillions > 0.0),
            "zero-population colony was incorrectly included in active habitat-support burden");

        var after = galaxy.Colonies
            .Where(colony => colony.CivilizationId == civilization.Id && colony.Id != zero.Id)
            .OrderBy(colony => colony.Id)
            .ToArray();
        Require(after.Length == before.Length,
            "habitat-support aggregation changed the owned colony collection");
        for (var i = 0; i < before.Length; i++)
        {
            Require(after[i].Id == before[i].Id && after[i].SystemId == before[i].SystemId,
                "habitat-support aggregation changed colony identity/location");
            Require(after[i].PlanetaryBodyId == before[i].PlanetaryBodyId,
                "habitat-support aggregation changed planetary occupancy");
            Require(after[i].PopulationSpeciesId == before[i].PopulationSpeciesId,
                "habitat-support aggregation changed colony Species identity");
            RequireClose(after[i].PopulationMillions, before[i].PopulationMillions,
                "habitat-support aggregation changed colony population");
            RequireClose(after[i].Infrastructure, before[i].Infrastructure,
                "habitat-support aggregation changed infrastructure");
            RequireClose(after[i].Stability, before[i].Stability,
                "habitat-support aggregation changed stability");
        }
    }

    private static GalaxyState CreateGalaxy() =>
        new GalaxyGenerator().Generate(
            0x4147_4752_4547_4154L,
            new GalaxyGenerationSettings
            {
                SystemCount = 72,
                PreWarpCivilizationCount = 6,
                AncientCivilizationCount = 1,
                Radius = 650.0f,
            });

    private static int NextColonyId(GalaxyState galaxy) =>
        galaxy.Colonies.Count == 0 ? 0 : galaxy.Colonies.Max(colony => colony.Id) + 1;

    private static void RequireTotalsEqual(
        HabitatSupportBurdenTotals actual,
        HabitatSupportBurdenTotals expected,
        string message)
    {
        RequireClose(actual.PopulationMillions, expected.PopulationMillions, message + " (population)");
        RequireClose(actual.TypicalDayMetabolicDemandMillions, expected.TypicalDayMetabolicDemandMillions, message + " (metabolism)");
        RequireClose(actual.AdultBiomassMillionKg, expected.AdultBiomassMillionKg, message + " (biomass)");
        RequireClose(actual.GravityMitigationPopulationMillions, expected.GravityMitigationPopulationMillions, message + " (gravity)");
        RequireClose(actual.ThermalControlPopulationMillions, expected.ThermalControlPopulationMillions, message + " (thermal)");
        RequireClose(actual.PressureControlPopulationMillions, expected.PressureControlPopulationMillions, message + " (pressure)");
        RequireClose(actual.SealedHabitatPopulationMillions, expected.SealedHabitatPopulationMillions, message + " (sealed)");
        RequireClose(actual.ArtificialBiospherePopulationMillions, expected.ArtificialBiospherePopulationMillions, message + " (biosphere)");
        RequireClose(actual.RadiationShieldingPopulationMillions, expected.RadiationShieldingPopulationMillions, message + " (radiation)");
    }

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
