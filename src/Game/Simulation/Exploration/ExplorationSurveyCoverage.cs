using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;

namespace Game.Simulation.Exploration;

public enum ExplorationSurveyWorkKind
{
    DetailedSurvey = 0,
    Reconnaissance = 1,
    CatalogExploration = 2,
}

public sealed record ExplorationSurveyWorkItem(
    int SystemId,
    string CatalogName,
    ExplorationSurveyWorkKind WorkKind,
    SystemSurveyLevel SurveyLevel,
    double SurveyProgress,
    double? EstimatedScienceSurveyDays,
    SurveyOperationalHazard? SurveyOperationalHazard,
    int PositiveSignatureBodyCount);

public sealed record CivilizationExplorationCoverageSnapshot(
    int CivilizationId,
    int TotalCatalogSystemCount,
    int UnknownSystemCount,
    int DetectedSystemCount,
    int PartiallySurveyedSystemCount,
    int FullySurveyedSystemCount,
    int ReconnaissanceBacklogCount,
    int DetailedSurveyBacklogCount,
    double FullySurveyedFraction,
    int PositiveSignatureBodyCount,
    int ConfirmedAnomalyBodyCount,
    int ConfirmedRareResourceBodyCount,
    int ConfirmedNativeCivilizationBodyCount,
    int ActiveScoutCount,
    int AssignedScoutCount,
    int IdleScoutCount,
    int ActiveScienceCount,
    int AssignedScienceCount,
    int IdleScienceCount,
    IReadOnlyList<ExplorationSurveyWorkItem> PriorityWorkItems);

/// <summary>
/// Builds a bounded civilization-level summary of exploration workload from observer-local
/// knowledge. It deliberately does not rank by hidden world value, invent operational reach,
/// or prescribe strategic policy. Unknown catalog targets expose only public catalog identity;
/// body signatures and confirmed discoveries come exclusively from <see cref="ExplorationReadModel"/>.
/// </summary>
public sealed class ExplorationSurveyCoverageView
{
    public const int DefaultMaximumWorkItems = 12;
    public const int HardMaximumWorkItems = 32;

    private readonly ExplorationReadModel _readModel;

    public ExplorationSurveyCoverageView(ExplorationReadModel? readModel = null)
    {
        _readModel = readModel ?? new ExplorationReadModel();
    }

    public CivilizationExplorationCoverageSnapshot Build(
        GalaxyState galaxy,
        int civilizationId,
        int maximumWorkItems = DefaultMaximumWorkItems)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        if (!galaxy.Civilizations.Any(civilization => civilization.Id == civilizationId))
            throw new InvalidOperationException($"Unknown civilization {civilizationId}.");

        var boundedMaximum = Math.Clamp(maximumWorkItems, 0, HardMaximumWorkItems);
        var exploration = _readModel.Build(galaxy, civilizationId);
        var knownById = exploration.KnownSystems.ToDictionary(system => system.SystemId);

        var detected = exploration.KnownSystems.Count(system => system.SurveyLevel == SystemSurveyLevel.Detected);
        var partial = exploration.KnownSystems.Count(system => system.SurveyLevel == SystemSurveyLevel.PartiallySurveyed);
        var full = exploration.KnownSystems.Count(system => system.SurveyLevel == SystemSurveyLevel.FullySurveyed);
        var unknown = Math.Max(0, galaxy.Systems.Count - exploration.KnownSystems.Count);

        var positiveSignatureBodies = exploration.KnownSystems
            .SelectMany(system => system.PlanetaryBodies)
            .Count(HasPositiveSignature);
        var confirmedAnomalies = exploration.KnownSystems
            .Where(system => system.HasDetailedSurvey)
            .SelectMany(system => system.PlanetaryBodies)
            .Count(body => body.HasAnomaly == true);
        var confirmedResources = exploration.KnownSystems
            .Where(system => system.HasDetailedSurvey)
            .SelectMany(system => system.PlanetaryBodies)
            .Count(body => body.HasRareResource == true);
        var confirmedNativeCivilizations = exploration.KnownSystems
            .Where(system => system.HasDetailedSurvey)
            .SelectMany(system => system.PlanetaryBodies)
            .Count(body => body.HasPreWarpCivilization == true);

        var scoutMissions = exploration.ActiveMissions.Where(mission => mission.Role == FleetRole.Scout).ToArray();
        var scienceMissions = exploration.ActiveMissions.Where(mission => mission.Role == FleetRole.Science).ToArray();
        var assignedScouts = scoutMissions.Count(IsAssignedSurveyMission);
        var assignedScience = scienceMissions.Count(IsAssignedSurveyMission);

        var work = BuildWorkItems(galaxy, knownById)
            .Take(boundedMaximum)
            .ToArray();

        return new CivilizationExplorationCoverageSnapshot(
            civilizationId,
            galaxy.Systems.Count,
            unknown,
            detected,
            partial,
            full,
            unknown + detected,
            partial,
            galaxy.Systems.Count == 0 ? 1.0 : (double)full / galaxy.Systems.Count,
            positiveSignatureBodies,
            confirmedAnomalies,
            confirmedResources,
            confirmedNativeCivilizations,
            scoutMissions.Length,
            assignedScouts,
            scoutMissions.Length - assignedScouts,
            scienceMissions.Length,
            assignedScience,
            scienceMissions.Length - assignedScience,
            work);
    }

    private static IEnumerable<ExplorationSurveyWorkItem> BuildWorkItems(
        GalaxyState galaxy,
        IReadOnlyDictionary<int, KnownSystemExplorationView> knownById)
    {
        // Finish already-invested detailed survey work first. This is a neutral workload ordering,
        // not strategic valuation: no resource/anomaly/native value affects the ordering.
        foreach (var known in knownById.Values
                     .Where(system => system.SurveyLevel == SystemSurveyLevel.PartiallySurveyed)
                     .OrderBy(system => system.SystemId))
        {
            yield return BuildKnownWorkItem(known, ExplorationSurveyWorkKind.DetailedSurvey);
        }

        // Detected systems have a known astronomical target but still need reconnaissance-grade
        // knowledge before body complexity/signature absence can be interpreted.
        foreach (var known in knownById.Values
                     .Where(system => system.SurveyLevel == SystemSurveyLevel.Detected)
                     .OrderBy(system => system.SystemId))
        {
            yield return BuildKnownWorkItem(known, ExplorationSurveyWorkKind.Reconnaissance);
        }

        // The star catalog/coordinates are common astronomical data in the current prototype.
        // Do not attach any world facts to these untouched targets.
        foreach (var system in galaxy.Systems
                     .Where(system => !knownById.ContainsKey(system.Id))
                     .OrderBy(system => system.Id))
        {
            yield return new ExplorationSurveyWorkItem(
                system.Id,
                system.Name,
                ExplorationSurveyWorkKind.CatalogExploration,
                SystemSurveyLevel.Unknown,
                0.0,
                null,
                null,
                0);
        }
    }

    private static ExplorationSurveyWorkItem BuildKnownWorkItem(
        KnownSystemExplorationView system,
        ExplorationSurveyWorkKind workKind) =>
        new(
            system.SystemId,
            system.CatalogName,
            workKind,
            system.SurveyLevel,
            system.SurveyProgress,
            workKind == ExplorationSurveyWorkKind.DetailedSurvey
                ? RemainingSurveyDays(system)
                : null,
            workKind == ExplorationSurveyWorkKind.DetailedSurvey
                ? system.SurveyOperationalHazard
                : null,
            system.PlanetaryBodies.Count(HasPositiveSignature));

    private static double? RemainingSurveyDays(KnownSystemExplorationView system) =>
        system.EstimatedScienceSurveyDays is double totalDays
            ? Math.Max(0.0, totalDays * (1.0 - Math.Clamp(system.SurveyProgress, 0.0, 1.0)))
            : null;

    private static bool HasPositiveSignature(PlanetaryBodyExplorationView body) =>
        body.HasRareResourceSignature == true ||
        body.HasAnomalySignature == true ||
        body.HasActivitySignature == true;

    private static bool IsAssignedSurveyMission(ExplorationMissionView mission) =>
        mission.Status.Phase is
            ExplorationMissionPhase.Traveling or
            ExplorationMissionPhase.ReconnaissanceReady or
            ExplorationMissionPhase.ScienceSurveying;
}
