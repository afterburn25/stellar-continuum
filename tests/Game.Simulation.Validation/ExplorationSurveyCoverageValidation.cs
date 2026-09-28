using System.Numerics;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class ExplorationSurveyCoverageValidation
{
    public static void ValidateObserverSafeCoverageTransitions()
    {
        var galaxy = CreateValidationGalaxy();
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var target = galaxy.Systems
            .Where(system => galaxy.Knowledge.GetSystemSurveyLevel(player.Id, system.Id) == SystemSurveyLevel.Unknown)
            .OrderBy(system => system.Id)
            .First(system => galaxy.PlanetaryBodies.Any(body =>
                body.SystemId == system.Id &&
                (body.HasRareResource || body.HasAnomaly || body.HasPreWarpCivilization)));
        var targetBodies = galaxy.PlanetaryBodies.Where(body => body.SystemId == target.Id).ToArray();
        var expectedPositiveBodies = targetBodies.Count(body =>
            body.HasRareResource || body.HasAnomaly || body.HasPreWarpCivilization);
        var expectedAnomalies = targetBodies.Count(body => body.HasAnomaly);
        var expectedResources = targetBodies.Count(body => body.HasRareResource);
        var expectedNativeCivilizations = targetBodies.Count(body => body.HasPreWarpCivilization);

        var view = new ExplorationSurveyCoverageView();
        var baseline = view.Build(galaxy, player.Id, ExplorationSurveyCoverageView.HardMaximumWorkItems);

        galaxy.Knowledge.RevealSystem(player.Id, target.Id);
        var detected = view.Build(galaxy, player.Id, ExplorationSurveyCoverageView.HardMaximumWorkItems);
        Require(detected.UnknownSystemCount == baseline.UnknownSystemCount - 1,
            "detecting a catalog target did not reduce unknown-system coverage by one");
        Require(detected.DetectedSystemCount == baseline.DetectedSystemCount + 1,
            "detecting a catalog target did not increase detected-only coverage by one");
        Require(detected.PositiveSignatureBodyCount == baseline.PositiveSignatureBodyCount,
            "detected-only target leaked hidden positive body signatures into coverage");
        Require(detected.ConfirmedAnomalyBodyCount == baseline.ConfirmedAnomalyBodyCount &&
                detected.ConfirmedRareResourceBodyCount == baseline.ConfirmedRareResourceBodyCount &&
                detected.ConfirmedNativeCivilizationBodyCount == baseline.ConfirmedNativeCivilizationBodyCount,
            "detected-only target leaked confirmed body discoveries");

        var detectedWork = detected.PriorityWorkItems.First(item => item.SystemId == target.Id);
        Require(detectedWork.WorkKind == ExplorationSurveyWorkKind.Reconnaissance,
            "detected-only target was not represented as reconnaissance work");
        Require(detectedWork.EstimatedScienceSurveyDays is null &&
                detectedWork.SurveyOperationalHazard is null &&
                detectedWork.PositiveSignatureBodyCount == 0,
            "reconnaissance backlog item leaked hidden survey complexity or signatures");

        Require(galaxy.Knowledge.RecordReconnaissance(player.Id, target.Id, 0.40),
            "validation reconnaissance did not advance target knowledge");
        var partial = view.Build(galaxy, player.Id, ExplorationSurveyCoverageView.HardMaximumWorkItems);
        Require(partial.DetectedSystemCount == detected.DetectedSystemCount - 1 &&
                partial.PartiallySurveyedSystemCount == baseline.PartiallySurveyedSystemCount + 1,
            "reconnaissance did not transition detected coverage into partial survey coverage");
        Require(partial.ReconnaissanceBacklogCount == detected.ReconnaissanceBacklogCount - 1,
            "reconnaissance completion did not reduce reconnaissance backlog");
        Require(partial.DetailedSurveyBacklogCount == baseline.DetailedSurveyBacklogCount + 1,
            "reconnaissance completion did not create detailed-survey backlog");
        Require(partial.PositiveSignatureBodyCount == baseline.PositiveSignatureBodyCount + expectedPositiveBodies,
            "partial survey did not expose exactly the observed positive body signatures");
        Require(partial.ConfirmedAnomalyBodyCount == baseline.ConfirmedAnomalyBodyCount &&
                partial.ConfirmedRareResourceBodyCount == baseline.ConfirmedRareResourceBodyCount &&
                partial.ConfirmedNativeCivilizationBodyCount == baseline.ConfirmedNativeCivilizationBodyCount,
            "partial survey incorrectly promoted positive signatures to confirmed discoveries");

        var partialWork = partial.PriorityWorkItems.First(item => item.SystemId == target.Id);
        Require(partialWork.WorkKind == ExplorationSurveyWorkKind.DetailedSurvey,
            "partial target was not represented as detailed-survey work");
        Require(partialWork.EstimatedScienceSurveyDays is > 0.0 && partialWork.SurveyOperationalHazard is not null,
            "partial target did not expose legitimate known detailed-survey effort");
        Require(partialWork.PositiveSignatureBodyCount == expectedPositiveBodies,
            "partial work item did not carry exactly the observer-visible positive signatures");

        galaxy.Knowledge.MarkSystemFullySurveyed(player.Id, target.Id);
        var full = view.Build(galaxy, player.Id, ExplorationSurveyCoverageView.HardMaximumWorkItems);
        Require(full.PartiallySurveyedSystemCount == baseline.PartiallySurveyedSystemCount &&
                full.FullySurveyedSystemCount == baseline.FullySurveyedSystemCount + 1,
            "full science survey did not transition coverage into fully surveyed state");
        Require(full.DetailedSurveyBacklogCount == baseline.DetailedSurveyBacklogCount,
            "fully surveyed target remained in detailed-survey backlog");
        Require(full.PriorityWorkItems.All(item => item.SystemId != target.Id),
            "fully surveyed target remained in bounded outstanding work list");
        Require(full.ConfirmedAnomalyBodyCount == baseline.ConfirmedAnomalyBodyCount + expectedAnomalies,
            "full survey did not expose exact confirmed anomaly count");
        Require(full.ConfirmedRareResourceBodyCount == baseline.ConfirmedRareResourceBodyCount + expectedResources,
            "full survey did not expose exact confirmed rare-resource count");
        Require(full.ConfirmedNativeCivilizationBodyCount == baseline.ConfirmedNativeCivilizationBodyCount + expectedNativeCivilizations,
            "full survey did not expose exact confirmed native-civilization count");
    }

    public static void ValidateBoundedWorkOrderingAndOwnedFleetCounts()
    {
        var galaxy = CreateValidationGalaxy();
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var foreign = galaxy.Civilizations.First(civilization => civilization.Id != player.Id);
        DisableSurveyFleets(galaxy, player.Id);

        var home = galaxy.Systems.First(system => system.Id == player.HomeSystemId);
        galaxy.Knowledge.MarkSystemFullySurveyed(player.Id, home.Id);
        var partialTarget = galaxy.Systems.First(system => system.Id != home.Id);
        galaxy.Knowledge.RecordReconnaissance(player.Id, partialTarget.Id, 0.40);
        var unknownTarget = galaxy.Systems.First(system =>
            system.Id != home.Id &&
            system.Id != partialTarget.Id &&
            galaxy.Knowledge.GetSystemSurveyLevel(player.Id, system.Id) == SystemSurveyLevel.Unknown);

        _ = AddSurveyFleet(galaxy, player.Id, FleetRole.Scout, unknownTarget, "Assigned Coverage Scout");
        _ = AddSurveyFleet(galaxy, player.Id, FleetRole.Science, partialTarget, "Assigned Coverage Science");
        _ = AddSurveyFleet(galaxy, player.Id, FleetRole.Scout, home, "Idle Coverage Scout");
        _ = AddSurveyFleet(galaxy, foreign.Id, FleetRole.Science, partialTarget, "Foreign Coverage Science");

        var view = new ExplorationSurveyCoverageView();
        var first = view.Build(galaxy, player.Id, maximumWorkItems: 2);
        var second = view.Build(galaxy, player.Id, maximumWorkItems: 2);

        Require(first.PriorityWorkItems.Count == 2,
            "coverage work list did not honor requested bound");
        Require(first.PriorityWorkItems.Select(item => (item.SystemId, item.WorkKind))
                .SequenceEqual(second.PriorityWorkItems.Select(item => (item.SystemId, item.WorkKind))),
            "identical coverage inputs produced non-deterministic bounded work ordering");
        Require(first.PriorityWorkItems[0].SystemId == partialTarget.Id &&
                first.PriorityWorkItems[0].WorkKind == ExplorationSurveyWorkKind.DetailedSurvey,
            "coverage did not prioritize already-invested partial survey work before untouched catalog targets");

        Require(first.ActiveScoutCount == 2 && first.AssignedScoutCount == 1 && first.IdleScoutCount == 1,
            "coverage reported incorrect owned scout assignment saturation");
        Require(first.ActiveScienceCount == 1 && first.AssignedScienceCount == 1 && first.IdleScienceCount == 0,
            "coverage reported incorrect owned science assignment saturation");
        Require(first.ActiveScienceCount != 2,
            "coverage leaked a foreign science fleet into the observer's capacity totals");
        Require(first.ReconnaissanceBacklogCount == first.UnknownSystemCount + first.DetectedSystemCount,
            "coverage reconnaissance backlog is inconsistent with unknown + detected workload");
        Require(first.DetailedSurveyBacklogCount == first.PartiallySurveyedSystemCount,
            "coverage detailed-survey backlog is inconsistent with partial survey workload");
        Require(first.FullySurveyedFraction >= 0.0 && first.FullySurveyedFraction <= 1.0,
            "coverage produced an invalid fully-surveyed fraction");

        var empty = view.Build(galaxy, player.Id, maximumWorkItems: 0);
        Require(empty.PriorityWorkItems.Count == 0,
            "coverage did not honor a zero work-item bound");
        var hardBounded = view.Build(galaxy, player.Id, maximumWorkItems: int.MaxValue);
        Require(hardBounded.PriorityWorkItems.Count <= ExplorationSurveyCoverageView.HardMaximumWorkItems,
            "coverage exceeded its hard work-item bound");
    }

    private static void DisableSurveyFleets(GalaxyState galaxy, int civilizationId)
    {
        foreach (var fleet in galaxy.Fleets.Where(fleet =>
                     fleet.CivilizationId == civilizationId &&
                     fleet.Role is FleetRole.Scout or FleetRole.Science))
        {
            fleet.IsActive = false;
        }
    }

    private static FleetState AddSurveyFleet(
        GalaxyState galaxy,
        int civilizationId,
        FleetRole role,
        StarSystemState system,
        string name)
    {
        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 50000 : galaxy.Fleets.Max(existing => existing.Id) + 50000,
            CivilizationId = civilizationId,
            Name = name,
            Role = role,
            Position = system.Position,
            CurrentSystemId = system.Id,
            StrategicSpeed = role == FleetRole.Scout ? 22.0 : 18.0,
            SensorRange = role == FleetRole.Scout ? 135.0f : 185.0f,
            IsActive = true,
        };
        galaxy.Fleets.Add(fleet);
        return fleet;
    }

    private static GalaxyState CreateValidationGalaxy() =>
        new GalaxyGenerator().Generate(
            0x434F_5645_5241_4745L,
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
