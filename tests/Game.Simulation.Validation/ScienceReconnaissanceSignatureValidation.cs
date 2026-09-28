using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class ScienceReconnaissanceSignatureValidation
{
    public static void ValidateScienceTransitionEmitsPositiveSignaturesOnce()
    {
        var galaxy = CreateValidationGalaxy();
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        DisableAllFleets(galaxy);

        var target = FindPositiveUnknownTarget(galaxy, player.Id);
        galaxy.Knowledge.RevealSystem(player.Id, target.Id);
        var science = AddScienceFleet(galaxy, player.Id, target);
        var simulation = new ExplorationSimulation();
        var profile = simulation.GetSurveyOperationsProfile(galaxy, target.Id);
        var bodies = galaxy.PlanetaryBodies.Where(body => body.SystemId == target.Id).ToArray();

        var first = simulation.Advance(galaxy, profile.EstimatedScienceSurveyDays * 0.40)
            .Where(@event => @event.FleetId == science.Id && @event.SystemId == target.Id)
            .ToArray();

        Require(galaxy.Knowledge.GetSystemSurveyLevel(player.Id, target.Id) == SystemSurveyLevel.PartiallySurveyed,
            "science transition did not establish reconnaissance-grade knowledge");
        Require(galaxy.Knowledge.GetSystemSurveyProgress(player.Id, target.Id) > 0.0 &&
                galaxy.Knowledge.GetSystemSurveyProgress(player.Id, target.Id) < 1.0,
            "science transition did not remain an incomplete survey");
        Require(first.Count(@event => @event.Type == ExplorationEventType.SystemSurveyStarted) == 1,
            "science reconnaissance transition did not emit exactly one survey-start event");
        Require(first.All(@event => @event.Type != ExplorationEventType.SystemSurveyed),
            "incomplete science transition incorrectly emitted survey completion");
        AssertPositiveSignatureEvents(first, bodies, "initial science reconnaissance transition");

        var second = simulation.Advance(galaxy, profile.EstimatedScienceSurveyDays * 0.10)
            .Where(@event => @event.FleetId == science.Id && @event.SystemId == target.Id)
            .ToArray();
        Require(second.All(@event => @event.Type is not (
                ExplorationEventType.ResourceSignatureDetected or
                ExplorationEventType.AnomalySignatureDetected or
                ExplorationEventType.ActivitySignatureDetected)),
            "later partial science tick duplicated reconnaissance signature events");
        Require(second.All(@event => @event.Type != ExplorationEventType.SystemSurveyStarted),
            "later partial science tick duplicated survey-start event");

        var completion = simulation.Advance(galaxy, profile.EstimatedScienceSurveyDays)
            .Where(@event => @event.FleetId == science.Id && @event.SystemId == target.Id)
            .ToArray();
        Require(galaxy.Knowledge.IsSystemFullySurveyed(player.Id, target.Id),
            "science survey did not complete after sufficient deterministic effort");
        Require(completion.Count(@event => @event.Type == ExplorationEventType.SystemSurveyed) == 1,
            "science completion did not emit exactly one system-surveyed event");
        AssertConfirmedDiscoveryEvents(completion, bodies, "science completion");
        Require(completion.All(@event => @event.Type is not (
                ExplorationEventType.ResourceSignatureDetected or
                ExplorationEventType.AnomalySignatureDetected or
                ExplorationEventType.ActivitySignatureDetected)),
            "science completion re-emitted already observed unconfirmed signatures");
    }

    public static void ValidateDirectCompletionSkipsTransientSignatures()
    {
        var galaxy = CreateValidationGalaxy();
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        DisableAllFleets(galaxy);

        var target = FindPositiveUnknownTarget(galaxy, player.Id);
        galaxy.Knowledge.RevealSystem(player.Id, target.Id);
        var science = AddScienceFleet(galaxy, player.Id, target);
        var simulation = new ExplorationSimulation();
        var profile = simulation.GetSurveyOperationsProfile(galaxy, target.Id);
        var bodies = galaxy.PlanetaryBodies.Where(body => body.SystemId == target.Id).ToArray();

        var events = simulation.Advance(galaxy, profile.EstimatedScienceSurveyDays * 1.10)
            .Where(@event => @event.FleetId == science.Id && @event.SystemId == target.Id)
            .ToArray();

        Require(galaxy.Knowledge.IsSystemFullySurveyed(player.Id, target.Id),
            "one-tick science survey did not reach full survey state");
        Require(events.Count(@event => @event.Type == ExplorationEventType.SystemSurveyStarted) == 1,
            "direct completion did not retain the survey-start transition event");
        Require(events.Count(@event => @event.Type == ExplorationEventType.SystemSurveyed) == 1,
            "direct completion did not emit exactly one completion event");
        Require(events.All(@event => @event.Type is not (
                ExplorationEventType.ResourceSignatureDetected or
                ExplorationEventType.AnomalySignatureDetected or
                ExplorationEventType.ActivitySignatureDetected)),
            "direct-to-full science survey emitted transient unconfirmed signatures alongside confirmed findings");
        AssertConfirmedDiscoveryEvents(events, bodies, "direct science completion");
    }

    private static void AssertPositiveSignatureEvents(
        IReadOnlyCollection<ExplorationEvent> events,
        IReadOnlyCollection<PlanetaryBodyState> bodies,
        string context)
    {
        var expectedResources = bodies.Count(body => body.HasRareResource);
        var expectedAnomalies = bodies.Count(body => body.HasAnomaly);
        var expectedActivity = bodies.Count(body => body.HasPreWarpCivilization);

        Require(events.Count(@event => @event.Type == ExplorationEventType.ResourceSignatureDetected) == expectedResources,
            $"{context} emitted incorrect rare-resource signature count");
        Require(events.Count(@event => @event.Type == ExplorationEventType.AnomalySignatureDetected) == expectedAnomalies,
            $"{context} emitted incorrect anomaly signature count");
        Require(events.Count(@event => @event.Type == ExplorationEventType.ActivitySignatureDetected) == expectedActivity,
            $"{context} emitted incorrect activity signature count");

        foreach (var @event in events.Where(@event => @event.Type is
                     ExplorationEventType.ResourceSignatureDetected or
                     ExplorationEventType.AnomalySignatureDetected or
                     ExplorationEventType.ActivitySignatureDetected))
        {
            Require(@event.PlanetaryBodyId is int bodyId && bodies.Any(body => body.Id == bodyId),
                $"{context} emitted a signature event without a valid body ID");
        }
    }

    private static void AssertConfirmedDiscoveryEvents(
        IReadOnlyCollection<ExplorationEvent> events,
        IReadOnlyCollection<PlanetaryBodyState> bodies,
        string context)
    {
        Require(events.Count(@event => @event.Type == ExplorationEventType.ResourceSurveyed) == bodies.Count(body => body.HasRareResource),
            $"{context} emitted incorrect confirmed rare-resource count");
        Require(events.Count(@event => @event.Type == ExplorationEventType.AnomalySurveyed) == bodies.Count(body => body.HasAnomaly),
            $"{context} emitted incorrect confirmed anomaly count");
        Require(events.Count(@event => @event.Type == ExplorationEventType.NativeCivilizationSurveyed) == bodies.Count(body => body.HasPreWarpCivilization),
            $"{context} emitted incorrect confirmed native-civilization count");
    }

    private static StarSystemState FindPositiveUnknownTarget(GalaxyState galaxy, int civilizationId) =>
        galaxy.Systems
            .Where(system => galaxy.Knowledge.GetSystemSurveyLevel(civilizationId, system.Id) == SystemSurveyLevel.Unknown)
            .OrderBy(system => system.Id)
            .First(system => galaxy.PlanetaryBodies.Any(body =>
                body.SystemId == system.Id &&
                (body.HasRareResource || body.HasAnomaly || body.HasPreWarpCivilization)));

    private static void DisableAllFleets(GalaxyState galaxy)
    {
        foreach (var fleet in galaxy.Fleets)
            fleet.IsActive = false;
    }

    private static FleetState AddScienceFleet(GalaxyState galaxy, int civilizationId, StarSystemState system)
    {
        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 70000 : galaxy.Fleets.Max(existing => existing.Id) + 70000,
            CivilizationId = civilizationId,
            Name = "Science Signature Validation Vessel",
            Role = FleetRole.Science,
            Position = system.Position,
            CurrentSystemId = system.Id,
            StrategicSpeed = 18.0,
            SensorRange = 185.0f,
            IsActive = true,
        };
        galaxy.Fleets.Add(fleet);
        return fleet;
    }

    private static GalaxyState CreateValidationGalaxy() =>
        new GalaxyGenerator().Generate(
            0x5343_4945_4E43_4553L,
            new GalaxyGenerationSettings
            {
                SystemCount = 72,
                PreWarpCivilizationCount = 6,
                AncientCivilizationCount = 1,
                Radius = 620.0f,
            });

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
