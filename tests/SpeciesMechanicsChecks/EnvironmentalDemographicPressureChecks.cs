using System.Runtime.CompilerServices;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;

internal static class EnvironmentalDemographicPressureChecks
{
    [ModuleInitializer]
    internal static void Initialize()
    {
        Run();
        Console.WriteLine("PASS: exact-body environmental demographic pressure boundaries");
    }

    public static void Run()
    {
        ValidateNaturallyStressedExactWorldReducesTurnover();
        ValidateHabitatSupportedFallbackRemainsNeutral();
        ValidateLegacyNullBodyRemainsNeutral();
    }

    private static void ValidateNaturallyStressedExactWorldReducesTurnover()
    {
        var galaxy = CreateGalaxy();
        var habitability = new SpeciesPlanetaryHabitabilityEvaluator();
        var candidate = (
            from species in SpeciesCatalog.All
            from body in galaxy.PlanetaryBodies
            let assessment = habitability.Evaluate(body, species.Id)
            where assessment.Viability == SpeciesColonizationViability.NaturallyViable
            where assessment.Environment.NaturalHabitability >= PlanetarySpeciesHabitabilityEvaluator.MinimumNaturalSettlementHabitability
            where assessment.Environment.NaturalHabitability < 0.95
            orderby assessment.Environment.NaturalHabitability ascending, body.Id ascending, species.Id ascending
            select new { Species = species, Body = body, Assessment = assessment })
            .FirstOrDefault()
            ?? throw new InvalidOperationException("Validation galaxy contained no naturally viable but environmentally stressed Species/world pair.");

        var colony = AddValidationColony(galaxy, candidate.Body, candidate.Species.Id);
        var pressure = new CurrentColonyPopulationTurnoverPressureView().Build(galaxy, colony);
        var expectedEnvironmentFactor = Math.Sqrt(candidate.Assessment.Environment.NaturalHabitability);
        var expectedIntrinsic = SpeciesDemographicPressureEvaluator.Evaluate(candidate.Species).IntrinsicGrowthPaceFactor;

        Require(pressure.UsesExactOccupiedBody,
            "naturally stressed exact-world test did not use authoritative body identity");
        Require(pressure.ColonizationViability == SpeciesColonizationViability.NaturallyViable,
            "naturally stressed world was not reported as naturally viable");
        Require(pressure.EnvironmentalPressureApplied,
            "naturally stressed exact world did not apply environmental demographic pressure");
        Require(pressure.NaturalEnvironmentTurnoverFactor < 1.0,
            "naturally stressed exact world did not reduce turnover below neutral");
        RequireClose(pressure.NaturalEnvironmentTurnoverFactor, expectedEnvironmentFactor,
            "environmental demographic pressure did not use sqrt(NaturalHabitability)");
        RequireClose(pressure.EffectiveGrowthPaceFactor, expectedIntrinsic * expectedEnvironmentFactor,
            "effective demographic pace did not conserve intrinsic × environmental pressure");
    }

    private static void ValidateHabitatSupportedFallbackRemainsNeutral()
    {
        var galaxy = CreateGalaxy();
        var habitability = new SpeciesPlanetaryHabitabilityEvaluator();
        var candidate = (
            from species in SpeciesCatalog.All
            from body in galaxy.PlanetaryBodies
            let assessment = habitability.Evaluate(body, species.Id)
            where assessment.Viability == SpeciesColonizationViability.HabitatSupportedFallback
            orderby body.Id ascending, species.Id ascending
            select new { Species = species, Body = body, Assessment = assessment })
            .FirstOrDefault()
            ?? throw new InvalidOperationException("Validation galaxy contained no habitat-supported fallback Species/world pair.");

        var colony = AddValidationColony(galaxy, candidate.Body, candidate.Species.Id);
        var pressure = new CurrentColonyPopulationTurnoverPressureView().Build(galaxy, colony);
        var intrinsic = SpeciesDemographicPressureEvaluator.Evaluate(candidate.Species).IntrinsicGrowthPaceFactor;

        Require(pressure.UsesExactOccupiedBody,
            "habitat-supported fallback test lost exact body identity");
        Require(pressure.ColonizationViability == SpeciesColonizationViability.HabitatSupportedFallback,
            "fallback world did not preserve its authoritative colonization viability");
        Require(!pressure.EnvironmentalPressureApplied,
            "prototype habitat-supported fallback incorrectly applied raw natural environmental pressure");
        RequireClose(pressure.NaturalEnvironmentTurnoverFactor, 1.0,
            "habitat-supported fallback must remain environmentally neutral until support is authoritative");
        RequireClose(pressure.EffectiveGrowthPaceFactor, intrinsic,
            "habitat-supported fallback changed intrinsic population turnover before support modeling exists");
    }

    private static void ValidateLegacyNullBodyRemainsNeutral()
    {
        var galaxy = CreateGalaxy();
        var civilization = galaxy.Civilizations.OrderBy(c => c.Id).First();
        var colony = new ColonyState
        {
            Id = NextColonyId(galaxy),
            CivilizationId = civilization.Id,
            SystemId = civilization.HomeSystemId,
            PlanetaryBodyId = null,
            Name = "Legacy Null-Body Demographic Test",
            PopulationSpeciesId = civilization.SpeciesId,
            PopulationMillions = 1000.0,
            Infrastructure = 1.0,
            Stability = 1.0,
        };
        galaxy.Colonies.Add(colony);

        var pressure = new CurrentColonyPopulationTurnoverPressureView().Build(galaxy, colony);
        var intrinsic = SpeciesDemographicPressureEvaluator.Evaluate(civilization.SpeciesId).IntrinsicGrowthPaceFactor;

        Require(!pressure.UsesExactOccupiedBody,
            "legacy null-body colony was incorrectly treated as exact occupancy");
        Require(pressure.PlanetaryBodyId is null && pressure.ColonizationViability is null,
            "legacy null-body colony claimed an evaluated body or colonization viability");
        Require(!pressure.EnvironmentalPressureApplied,
            "legacy null-body colony received retroactive environmental demographic pressure");
        RequireClose(pressure.NaturalHabitability, 1.0,
            "legacy null-body colony did not use neutral environmental habitability");
        RequireClose(pressure.NaturalEnvironmentTurnoverFactor, 1.0,
            "legacy null-body colony did not use neutral environmental turnover");
        RequireClose(pressure.EffectiveGrowthPaceFactor, intrinsic,
            "legacy null-body colony changed intrinsic turnover through a compatibility-world guess");
    }

    private static ColonyState AddValidationColony(
        GalaxyState galaxy,
        PlanetaryBodyState body,
        string speciesId)
    {
        var civilization = galaxy.Civilizations.OrderBy(c => c.Id).First();
        var colony = new ColonyState
        {
            Id = NextColonyId(galaxy),
            CivilizationId = civilization.Id,
            SystemId = body.SystemId,
            PlanetaryBodyId = body.Id,
            Name = $"Environmental Demographic Test {body.Id}",
            PopulationSpeciesId = speciesId,
            PopulationMillions = 1000.0,
            Infrastructure = 1.0,
            Stability = 1.0,
        };
        galaxy.Colonies.Add(colony);
        return colony;
    }

    private static int NextColonyId(GalaxyState galaxy) =>
        galaxy.Colonies.Count == 0 ? 0 : galaxy.Colonies.Max(colony => colony.Id) + 1;

    private static GalaxyState CreateGalaxy() =>
        new GalaxyGenerator().Generate(
            0x454E_5644_454D_4F47L,
            new GalaxyGenerationSettings
            {
                SystemCount = 72,
                PreWarpCivilizationCount = 6,
                AncientCivilizationCount = 1,
                Radius = 650.0f,
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
