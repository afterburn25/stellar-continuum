using System.Runtime.CompilerServices;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Species;

namespace Game.Simulation.Validation;

internal static class SpeciesPlanetaryReadModelValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidateSurveyGatedSpeciesSuitability();
        Console.WriteLine("PASS: survey-gated species planetary suitability");
    }

    private static void ValidateSurveyGatedSpeciesSuitability()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x5350_4543_504C_414EL,
            new GalaxyGenerationSettings
            {
                SystemCount = 44,
                PreWarpCivilizationCount = 5,
                AncientCivilizationCount = 1,
                Radius = 500.0f,
            });

        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var target = galaxy.Systems.First(system =>
            system.Id != player.HomeSystemId &&
            galaxy.PlanetaryBodies.Any(body => body.SystemId == system.Id) &&
            !galaxy.Knowledge.IsSystemFullySurveyed(player.Id, system.Id));
        var targetBodyIds = galaxy.PlanetaryBodies
            .Where(body => body.SystemId == target.Id)
            .Select(body => body.Id)
            .OrderBy(id => id)
            .ToArray();

        var readModel = new SpeciesPlanetaryReadModel();
        galaxy.Knowledge.RevealSystem(player.Id, target.Id);
        var detected = readModel.BuildForSpecies(galaxy, player.Id, player.SpeciesId);
        Require(
            detected.All(view => view.SystemId != target.Id),
            "species suitability leaked detailed body facts at detection level");

        galaxy.Knowledge.RecordReconnaissance(player.Id, target.Id);
        Require(
            galaxy.Knowledge.GetSystemSurveyLevel(player.Id, target.Id) == SystemSurveyLevel.PartiallySurveyed,
            "validation system did not enter reconnaissance survey state");
        var reconnaissance = readModel.BuildForSpecies(galaxy, player.Id, player.SpeciesId);
        Require(
            reconnaissance.All(view => view.SystemId != target.Id),
            "species suitability leaked detailed body facts during reconnaissance");

        galaxy.Knowledge.MarkSystemFullySurveyed(player.Id, target.Id);
        var detailed = readModel.BuildForSpecies(galaxy, player.Id, player.SpeciesId)
            .Where(view => view.SystemId == target.Id)
            .OrderBy(view => view.PlanetaryBodyId)
            .ToArray();
        Require(
            detailed.Select(view => view.PlanetaryBodyId).SequenceEqual(targetBodyIds),
            "full survey did not expose one species-suitability result per known body");

        var evaluator = new SpeciesPlanetaryHabitabilityEvaluator();
        foreach (var view in detailed)
        {
            var body = galaxy.PlanetaryBodies.First(candidate => candidate.Id == view.PlanetaryBodyId);
            var authoritative = evaluator.Evaluate(body, player.SpeciesId);
            Require(
                Math.Abs(view.NaturalHabitability - authoritative.Environment.NaturalHabitability) < 0.0000001,
                "planetary read model changed natural habitability");
            Require(
                Math.Abs(view.UnprotectedOperationalCapacity - authoritative.Environment.UnprotectedOperationalCapacity) < 0.0000001,
                "planetary read model changed operational capacity");
            Require(view.LimitingFactor == authoritative.Environment.LimitingFactor,
                "planetary read model changed limiting factor");
            Require(view.ColonizationViability == authoritative.Viability,
                "planetary read model changed colonization viability");
        }

        var available = readModel.BuildForAvailablePopulations(galaxy, player.Id);
        Require(available.Any(view => view.SpeciesId == player.SpeciesId && view.SystemId == target.Id),
            "available-population view omitted the founding species on a fully surveyed system");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
