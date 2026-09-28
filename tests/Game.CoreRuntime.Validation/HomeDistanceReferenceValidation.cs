using Game.Campaign;
using Game.Presentation;
using Game.Simulation.Exploration;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.CoreRuntime.Validation;

internal static class HomeDistanceReferenceValidation
{
    internal static void Run()
    {
        var sessions = new CampaignSessionService();
        var humanGalaxy = sessions.CreateNew("HOME-DISTANCE-HUMAN").Galaxy;
        var human = humanGalaxy.Civilizations.Single(civilization => civilization.IsPlayer);
        var sol = humanGalaxy.Systems.Single(system => system.Id == human.HomeSystemId);
        var sirius = humanGalaxy.Systems.Single(system => system.Name == "Sirius");

        Require(InterstellarDistance.Between(sol, sol) == 0.0 &&
                MetricFormat.InterstellarDistance(0.0, 0.0).Contains("0 ly", StringComparison.Ordinal),
            "home-system distance must be a visible zero reference");
        var siriusDistance = InterstellarDistance.Between(sol, sirius);
        var siriusDisplay = MetricFormat.InterstellarDistance(siriusDistance, siriusDistance / 3.26156);
        Require(Math.Abs(siriusDistance - 8.60) <= .02 && siriusDisplay.Contains("8.6 ly", StringComparison.Ordinal) &&
                siriusDisplay.Contains("2.6 pc", StringComparison.Ordinal),
            "Sirius home reference did not retain measured three-dimensional distance and metric/ly formatting");

        var unknown = humanGalaxy.Systems.First(system =>
            humanGalaxy.Knowledge.GetSystemSurveyLevel(human.Id, system.Id) == SystemSurveyLevel.Unknown);
        Require(!new ExplorationReadModel().Build(humanGalaxy, human.Id).KnownSystems.Any(system => system.SystemId == unknown.Id) &&
                !MetricFormat.InterstellarDistance(InterstellarDistance.Between(sol, unknown)).Contains(unknown.Name, StringComparison.Ordinal),
            "unknown-system distance reference must not depend on a private name or survey facts");

        var nonhumanGalaxy = sessions.CreateNew("HOME-DISTANCE-NONHUMAN", SpeciesCatalog.PelagicHighPressureId).Galaxy;
        var nonhuman = nonhumanGalaxy.Civilizations.Single(civilization => civilization.IsPlayer);
        var nonhumanHome = nonhumanGalaxy.Systems.Single(system => system.Id == nonhuman.HomeSystemId);
        Require(nonhuman.SpeciesId == SpeciesCatalog.PelagicHighPressureId &&
                InterstellarDistance.Between(nonhumanHome, nonhumanHome) == 0.0,
            "nonhuman distance reference did not use the player's own founding home system");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
