using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class SurveyOperationsValidation
{
    public static void ValidateDeterministicBoundedSurveyEffortAndTickInvariance()
    {
        const long seed = 0x5355_5256_4559_4F50L;
        var settings = CreateSettings();
        var first = new GalaxyGenerator().Generate(seed, settings);
        var second = new GalaxyGenerator().Generate(seed, settings);
        var profiler = new SurveyOperationsProfiler();

        var firstProfiles = first.Systems
            .OrderBy(system => system.Id)
            .Select(system => profiler.Build(first, system.Id))
            .ToArray();
        var secondProfiles = second.Systems
            .OrderBy(system => system.Id)
            .Select(system => profiler.Build(second, system.Id))
            .ToArray();

        Require(firstProfiles.SequenceEqual(secondProfiles), "identical physical galaxies produced different survey-operation profiles");
        Require(firstProfiles.Select(profile => Math.Round(profile.EstimatedScienceSurveyDays, 4)).Distinct().Count() > 1, "survey effort did not vary across physically different systems");
        foreach (var profile in firstProfiles)
        {
            Require(profile.EstimatedScienceSurveyDays >= SurveyOperationsProfiler.MinimumSurveyDays, "survey effort fell below the bounded minimum");
            Require(profile.EstimatedScienceSurveyDays <= SurveyOperationsProfiler.MaximumSurveyDays, "survey effort exceeded the bounded maximum");
            Require(double.IsFinite(profile.ProgressPerDay) && profile.ProgressPerDay > 0.0, "survey profile produced invalid progress rate");
            Require(profile.PlanetCount + profile.MoonCount == first.PlanetaryBodies.Count(body => body.SystemId == profile.SystemId), "survey profile body counts diverged from the physical catalog");
        }

        DisableAllFleets(first);
        DisableAllFleets(second);
        var playerA = first.Civilizations.First(civilization => civilization.Id == first.PlayerCivilizationId);
        var playerB = second.Civilizations.First(civilization => civilization.Id == second.PlayerCivilizationId);
        var targetId = firstProfiles.OrderByDescending(profile => profile.EstimatedScienceSurveyDays).First().SystemId;
        var targetA = first.Systems.First(system => system.Id == targetId);
        var targetB = second.Systems.First(system => system.Id == targetId);

        PreparePartialSurvey(first, playerA.Id, targetId);
        PreparePartialSurvey(second, playerB.Id, targetId);
        AddScienceFleet(first, playerA.Id, targetA, 8001);
        AddScienceFleet(second, playerB.Id, targetB, 8001);

        var explorationA = new ExplorationSimulation();
        var explorationB = new ExplorationSimulation();
        const double totalDays = 4.0;
        explorationA.Advance(first, totalDays);
        for (var i = 0; i < 8; i++)
            explorationB.Advance(second, totalDays / 8.0);

        var progressA = first.Knowledge.GetSystemSurveyProgress(playerA.Id, targetId);
        var progressB = second.Knowledge.GetSystemSurveyProgress(playerB.Id, targetId);
        Require(progressA < 1.0 && progressB < 1.0, "tick-invariance scenario completed too early to compare intermediate progress");
        Require(Math.Abs(progressA - progressB) < 0.0000001, "science survey progress changed when identical simulated time was split across ticks");

        var profileForTarget = profiler.Build(first, targetId);
        var expected = Math.Min(1.0, ExplorationSimulation.ScoutReconnaissanceProgress + totalDays / profileForTarget.EstimatedScienceSurveyDays);
        Require(Math.Abs(progressA - expected) < 0.0000001, "science survey progress did not follow the target's deterministic survey-effort profile");
    }

    public static void ValidateReconnaissanceSignalsRemainPositiveOnly()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x5349_474E_414C_4E4FL,
            new GalaxyGenerationSettings
            {
                SystemCount = 56,
                PreWarpCivilizationCount = 5,
                AncientCivilizationCount = 1,
                Radius = 560.0f,
                HabitableChance = 0.0,
                AnomalyChance = 0.0,
                RareResourceChance = 0.0,
                IndependentPreWarpChance = 0.0,
            });
        DisableAllFleets(galaxy);
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var target = galaxy.Systems.First(system =>
            system.Id != player.HomeSystemId &&
            system.Archetype == StarArchetype.Standard &&
            galaxy.PlanetaryBodies.Where(body => body.SystemId == system.Id).All(body => !body.HasRareResource && !body.HasAnomaly && !body.HasPreWarpCivilization));

        galaxy.Knowledge.RevealSystem(player.Id, target.Id);
        var scout = AddScoutFleet(galaxy, player.Id, target, 8101);
        var exploration = new ExplorationSimulation();
        var events = exploration.Advance(galaxy, ExplorationSimulation.ScoutReconnaissanceDays);

        Require(galaxy.Knowledge.GetSystemSurveyLevel(player.Id, target.Id) == SystemSurveyLevel.PartiallySurveyed, "scout reconnaissance did not establish partial survey state");
        Require(!events.Any(evt => evt.Type is ExplorationEventType.ResourceSignatureDetected or ExplorationEventType.AnomalySignatureDetected or ExplorationEventType.ActivitySignatureDetected), "scout invented a positive discovery signature where authoritative bodies had none");

        var view = new ExplorationReadModel().Build(galaxy, player.Id).KnownSystems.First(system => system.SystemId == target.Id);
        Require(view.EstimatedScienceSurveyDays is >= SurveyOperationsProfiler.MinimumSurveyDays and <= SurveyOperationsProfiler.MaximumSurveyDays, "reconnaissance did not expose bounded estimated science effort");
        Require(view.SurveyOperationalHazard is not null, "reconnaissance did not expose operational survey hazard");
        Require(view.PlanetaryBodies.Count > 0, "reconnaissance did not expose the orbital catalog");
        foreach (var body in view.PlanetaryBodies)
        {
            Require(body.HasRareResourceSignature is null, "absence of a scout resource signature was incorrectly represented as confirmed false");
            Require(body.HasAnomalySignature is null, "absence of a scout anomaly signature was incorrectly represented as confirmed false");
            Require(body.HasActivitySignature is null, "absence of a scout activity signature was incorrectly represented as confirmed false");
            Require(body.HasRareResource is null && body.HasAnomaly is null && body.HasPreWarpCivilization is null, "scout reconnaissance leaked confirmed body findings");
        }

        scout.IsActive = false;
    }

    public static void ValidatePositiveSignaturesAndConfirmedBodyDiscoveries()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x4449_5343_4F56_4552L,
            new GalaxyGenerationSettings
            {
                SystemCount = 52,
                PreWarpCivilizationCount = 5,
                AncientCivilizationCount = 1,
                Radius = 540.0f,
                HabitableChance = 1.0,
                AnomalyChance = 1.0,
                RareResourceChance = 1.0,
                IndependentPreWarpChance = 1.0,
            });
        DisableAllFleets(galaxy);
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var target = galaxy.Systems.First(system => system.Id != player.HomeSystemId && system.Archetype == StarArchetype.Standard);
        var bodies = galaxy.PlanetaryBodies.Where(body => body.SystemId == target.Id).OrderBy(body => body.Id).ToArray();
        Require(bodies.Any(body => body.HasRareResource), "discovery scenario has no rare-resource body");
        Require(bodies.Any(body => body.HasAnomaly), "discovery scenario has no anomaly body");
        Require(bodies.Any(body => body.HasPreWarpCivilization), "discovery scenario has no native pre-warp body");

        galaxy.Knowledge.RevealSystem(player.Id, target.Id);
        var scout = AddScoutFleet(galaxy, player.Id, target, 8201);
        var exploration = new ExplorationSimulation();
        var scoutEvents = exploration.Advance(galaxy, ExplorationSimulation.ScoutReconnaissanceDays);

        foreach (var body in bodies)
        {
            Require(
                scoutEvents.Any(evt => evt.Type == ExplorationEventType.ResourceSignatureDetected && evt.PlanetaryBodyId == body.Id) == body.HasRareResource,
                $"resource reconnaissance signature mismatch for body {body.Id}");
            Require(
                scoutEvents.Any(evt => evt.Type == ExplorationEventType.AnomalySignatureDetected && evt.PlanetaryBodyId == body.Id) == body.HasAnomaly,
                $"anomaly reconnaissance signature mismatch for body {body.Id}");
            Require(
                scoutEvents.Any(evt => evt.Type == ExplorationEventType.ActivitySignatureDetected && evt.PlanetaryBodyId == body.Id) == body.HasPreWarpCivilization,
                $"activity reconnaissance signature mismatch for body {body.Id}");
        }

        var partialView = new ExplorationReadModel().Build(galaxy, player.Id).KnownSystems.First(system => system.SystemId == target.Id);
        foreach (var bodyView in partialView.PlanetaryBodies)
        {
            var authoritative = bodies.First(body => body.Id == bodyView.BodyId);
            Require(bodyView.HasRareResourceSignature == (authoritative.HasRareResource ? true : null), "partial resource signature did not preserve positive-only semantics");
            Require(bodyView.HasAnomalySignature == (authoritative.HasAnomaly ? true : null), "partial anomaly signature did not preserve positive-only semantics");
            Require(bodyView.HasActivitySignature == (authoritative.HasPreWarpCivilization ? true : null), "partial activity signature did not preserve positive-only semantics");
            Require(bodyView.HasRareResource is null && bodyView.HasAnomaly is null && bodyView.HasPreWarpCivilization is null, "partial survey exposed confirmed finding booleans");
        }

        scout.IsActive = false;
        AddScienceFleet(galaxy, player.Id, target, 8202);
        var profile = exploration.GetSurveyOperationsProfile(galaxy, target.Id);
        var scienceEvents = exploration.Advance(galaxy, profile.EstimatedScienceSurveyDays);
        Require(galaxy.Knowledge.IsSystemFullySurveyed(player.Id, target.Id), "science survey did not complete after its bounded estimated effort");
        Require(scienceEvents.Any(evt => evt.Type == ExplorationEventType.SystemSurveyed && evt.SystemId == target.Id), "science completion event was missing");

        var expectedConfirmedCount = bodies.Sum(body => (body.HasRareResource ? 1 : 0) + (body.HasAnomaly ? 1 : 0) + (body.HasPreWarpCivilization ? 1 : 0));
        var confirmed = scienceEvents.Where(evt => evt.Type is ExplorationEventType.ResourceSurveyed or ExplorationEventType.AnomalySurveyed or ExplorationEventType.NativeCivilizationSurveyed).ToArray();
        Require(confirmed.Length == expectedConfirmedCount, "confirmed discovery event count did not match represented body findings");
        Require(confirmed.All(evt => evt.PlanetaryBodyId is not null), "a body-specific confirmed discovery omitted its physical body ID");

        foreach (var body in bodies)
        {
            Require(
                confirmed.Any(evt => evt.Type == ExplorationEventType.ResourceSurveyed && evt.PlanetaryBodyId == body.Id) == body.HasRareResource,
                $"confirmed rare-resource discovery mismatch for body {body.Id}");
            Require(
                confirmed.Any(evt => evt.Type == ExplorationEventType.AnomalySurveyed && evt.PlanetaryBodyId == body.Id) == body.HasAnomaly,
                $"confirmed anomaly discovery mismatch for body {body.Id}");
            Require(
                confirmed.Any(evt => evt.Type == ExplorationEventType.NativeCivilizationSurveyed && evt.PlanetaryBodyId == body.Id) == body.HasPreWarpCivilization,
                $"confirmed native-civilization discovery mismatch for body {body.Id}");
        }

        var fullView = new ExplorationReadModel().Build(galaxy, player.Id).KnownSystems.First(system => system.SystemId == target.Id);
        foreach (var bodyView in fullView.PlanetaryBodies)
        {
            var authoritative = bodies.First(body => body.Id == bodyView.BodyId);
            Require(bodyView.HasRareResource == authoritative.HasRareResource, "full survey resource fact mismatch");
            Require(bodyView.HasAnomaly == authoritative.HasAnomaly, "full survey anomaly fact mismatch");
            Require(bodyView.HasPreWarpCivilization == authoritative.HasPreWarpCivilization, "full survey native-civilization fact mismatch");
            Require(bodyView.HasRareResourceSignature == authoritative.HasRareResource, "full survey did not resolve resource signature to confirmed state");
            Require(bodyView.HasAnomalySignature == authoritative.HasAnomaly, "full survey did not resolve anomaly signature to confirmed state");
            Require(bodyView.HasActivitySignature == authoritative.HasPreWarpCivilization, "full survey did not resolve activity signature to confirmed state");
        }
    }

    private static GalaxyGenerationSettings CreateSettings() => new()
    {
        SystemCount = 60,
        PreWarpCivilizationCount = 5,
        AncientCivilizationCount = 1,
        Radius = 580.0f,
        HabitableChance = 0.40,
        AnomalyChance = 0.32,
        RareResourceChance = 0.25,
        IndependentPreWarpChance = 0.10,
    };

    private static void PreparePartialSurvey(GalaxyState galaxy, int civilizationId, int systemId)
    {
        galaxy.Knowledge.RevealSystem(civilizationId, systemId);
        galaxy.Knowledge.RecordReconnaissance(civilizationId, systemId, ExplorationSimulation.ScoutReconnaissanceProgress);
    }

    private static FleetState AddScoutFleet(GalaxyState galaxy, int civilizationId, StarSystemState system, int id)
    {
        var fleet = new FleetState
        {
            Id = id,
            CivilizationId = civilizationId,
            Name = $"Survey Validation Scout {id}",
            Role = FleetRole.Scout,
            Position = system.Position,
            CurrentSystemId = system.Id,
            StrategicSpeed = 24.0,
            SensorRange = 145.0f,
            IsActive = true,
        };
        galaxy.Fleets.Add(fleet);
        return fleet;
    }

    private static FleetState AddScienceFleet(GalaxyState galaxy, int civilizationId, StarSystemState system, int id)
    {
        var fleet = new FleetState
        {
            Id = id,
            CivilizationId = civilizationId,
            Name = $"Survey Validation Science {id}",
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

    private static void DisableAllFleets(GalaxyState galaxy)
    {
        foreach (var fleet in galaxy.Fleets)
            fleet.IsActive = false;
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
