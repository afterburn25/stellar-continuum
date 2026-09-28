using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;

namespace Game.Simulation.Exploration;

/// <summary>
/// Builds a bounded observer-safe set of survey destinations for one physical exploration fleet.
/// Player UI and AI should consume this contract rather than reimplementing target priorities or
/// operational-reach rules. The planner uses only common catalog coordinates, the acting
/// civilization's legitimate survey knowledge, and the injected Logistics-owned reach view.
/// </summary>
public sealed class ExplorationMissionPlanner
{
    public const int DefaultMaximumCandidates = 32;
    public const int HardMaximumCandidates = 64;

    private readonly IInterstellarOperationalReachView _operationalReach;
    private readonly SurveyOperationsProfiler _surveyProfiler;

    public ExplorationMissionPlanner(
        IInterstellarOperationalReachView? operationalReach = null,
        SurveyOperationsProfiler? surveyProfiler = null)
    {
        _operationalReach = operationalReach ?? new LaneInterstellarOperationalReachView();
        _surveyProfiler = surveyProfiler ?? new SurveyOperationsProfiler();
    }

    public ExplorationMissionPlan BuildPlan(
        GalaxyState galaxy,
        int fleetId,
        int maximumCandidates = DefaultMaximumCandidates)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        maximumCandidates = Math.Clamp(maximumCandidates, 1, HardMaximumCandidates);

        var fleet = galaxy.Fleets.FirstOrDefault(candidate => candidate.Id == fleetId && candidate.IsActive);
        if (fleet is null)
        {
            return ExplorationMissionPlan.Unavailable(
                fleetId,
                "No active exploration vessel with that fleet ID is available.");
        }

        if (fleet.Role is not (FleetRole.Scout or FleetRole.Science))
        {
            return ExplorationMissionPlan.Unavailable(
                fleetId,
                $"{fleet.Name} is not a scout or science survey vessel.");
        }

        var candidates = galaxy.Systems
            .Where(system => NeedsSurveyWork(galaxy, fleet, system.Id))
            .Select(system => BuildCandidate(galaxy, fleet, system))
            // Supported work stays ahead of blocked work, but blocked targets remain visible so
            // UI/AI diagnostics preserve the authoritative Logistics-owned rejection reason.
            .OrderBy(candidate => candidate.Reach.IsSupported ? 0 : 1)
            .ThenBy(candidate => candidate.PriorityBand)
            .ThenBy(candidate => candidate.DistanceFromFleet)
            .ThenBy(candidate => candidate.SystemId)
            .Take(maximumCandidates)
            .ToArray();

        return new ExplorationMissionPlan(
            fleet.Id,
            fleet.Name,
            fleet.Role,
            true,
            candidates.Length == 0
                ? "No remaining survey work is available to this vessel."
                : $"{candidates.Length} survey target{(candidates.Length == 1 ? string.Empty : "s")} available in the current planning window.",
            candidates);
    }

    public ExplorationMissionOrderAssessment AssessOrder(
        GalaxyState galaxy,
        int fleetId,
        int destinationSystemId, bool requireSurveyWork = true)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var fleet = galaxy.Fleets.FirstOrDefault(candidate => candidate.Id == fleetId && candidate.IsActive);
        if (fleet is null)
            return ExplorationMissionOrderAssessment.Rejected("No active exploration vessel with that fleet ID is available.");
        if (fleet.Role is not (FleetRole.Scout or FleetRole.Science))
            return ExplorationMissionOrderAssessment.Rejected($"{fleet.Name} is not a scout or science survey vessel.");

        var system = galaxy.Systems.FirstOrDefault(candidate => candidate.Id == destinationSystemId);
        if (system is null)
            return ExplorationMissionOrderAssessment.Rejected("Unknown astronomical target.");
        if (requireSurveyWork && !NeedsSurveyWork(galaxy, fleet, destinationSystemId))
        {
            return ExplorationMissionOrderAssessment.Rejected(
                fleet.Role == FleetRole.Scout
                    ? $"{system.Name} already has reconnaissance-grade survey coverage."
                    : $"{system.Name} already has a completed detailed science survey.");
        }

        var candidate = BuildCandidate(galaxy, fleet, system);
        if (!candidate.Reach.IsSupported)
            return ExplorationMissionOrderAssessment.Rejected(candidate.Reach.Reason, candidate);

        var local = fleet.CurrentSystemId == destinationSystemId && fleet.DestinationSystemId is null;
        if (!NeedsSurveyWork(galaxy, fleet, destinationSystemId))
            return ExplorationMissionOrderAssessment.Approve(local ? $"{fleet.Name} is already on station in {system.Name}." :
                $"{fleet.Name}: course set for {system.Name}. {candidate.Reach.Reason}", candidate, local);
        var action = local
            ? fleet.Role == FleetRole.Scout ? "reconnaissance pass" : "detailed science survey"
            : fleet.Role == FleetRole.Scout ? "reconnaissance mission" : "science-survey mission";
        return ExplorationMissionOrderAssessment.Approve(
            local
                ? $"{fleet.Name} is ready to begin the {action} in {system.Name}."
                : $"{fleet.Name}: {action} approved for {system.Name}. {candidate.Reach.Reason}",
            candidate,
            local);
    }

    public MissionReachAssessment AssessOperationalReach(
        GalaxyState galaxy,
        FleetState fleet,
        int destinationSystemId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(fleet);
        var missionKind = fleet.Role switch
        {
            FleetRole.Scout => InterstellarMissionKind.ScoutReconnaissance,
            FleetRole.Science => InterstellarMissionKind.ScienceSurvey,
            FleetRole.Military => InterstellarMissionKind.MilitaryDeployment,
            FleetRole.Logistics => InterstellarMissionKind.Logistics,
            _ => InterstellarMissionKind.ScoutReconnaissance,
        };
        return _operationalReach.Assess(
            galaxy,
            fleet.CivilizationId,
            fleet,
            destinationSystemId,
            missionKind);
    }

    private ExplorationMissionCandidate BuildCandidate(
        GalaxyState galaxy,
        FleetState fleet,
        StarSystemState system)
    {
        var level = galaxy.Knowledge.GetSystemSurveyLevel(fleet.CivilizationId, system.Id);
        var progress = galaxy.Knowledge.GetSystemSurveyProgress(fleet.CivilizationId, system.Id);
        var profile = level >= SystemSurveyLevel.PartiallySurveyed
            ? _surveyProfiler.Build(galaxy, system.Id)
            : null;
        double? remainingDays = fleet.Role == FleetRole.Science && profile is not null
            ? Math.Max(0.0, profile.EstimatedScienceSurveyDays * (1.0 - progress))
            : null;
        var distance = InterstellarDistance.FromFleet(galaxy, fleet, system);
        var reach = AssessOperationalReach(galaxy, fleet, system.Id);
        var priority = SurveyPriority(fleet.Role, level);

        return new ExplorationMissionCandidate(
            system.Id,
            system.Name,
            level,
            progress,
            priority,
            distance,
            remainingDays,
            profile?.OperationalHazard,
            reach,
            BuildReason(fleet.Role, level, progress, remainingDays, reach));
    }

    internal static bool NeedsSurveyWork(GalaxyState galaxy, FleetState fleet, int systemId)
    {
        var level = galaxy.Knowledge.GetSystemSurveyLevel(fleet.CivilizationId, systemId);
        return fleet.Role switch
        {
            FleetRole.Scout => level < SystemSurveyLevel.PartiallySurveyed,
            FleetRole.Science => level < SystemSurveyLevel.FullySurveyed,
            _ => false,
        };
    }

    internal static int SurveyPriority(FleetRole role, SystemSurveyLevel level)
    {
        if (role == FleetRole.Science)
        {
            // Finish scout/sensor work before striking into catalog-only targets.
            return level switch
            {
                SystemSurveyLevel.PartiallySurveyed => 0,
                SystemSurveyLevel.Detected => 1,
                _ => 2,
            };
        }

        // Scouts extend the detected frontier first, then visit catalog-only targets.
        return level == SystemSurveyLevel.Detected ? 0 : 1;
    }

    private static string BuildReason(
        FleetRole role,
        SystemSurveyLevel level,
        double progress,
        double? remainingDays,
        MissionReachAssessment reach)
    {
        var work = role switch
        {
            FleetRole.Scout when level == SystemSurveyLevel.Detected => "Detected target still needs a reconnaissance pass.",
            FleetRole.Scout => "Catalog target can be reconnoitered on arrival.",
            FleetRole.Science when level == SystemSurveyLevel.PartiallySurveyed && remainingDays is double days =>
                $"Detailed survey is {progress:P0} complete; approximately {days:0.#} survey days remain.",
            FleetRole.Science when level == SystemSurveyLevel.Detected => "Detected target needs a detailed science survey.",
            FleetRole.Science => "Catalog target can receive a first detailed science survey on arrival.",
            _ => "Survey work is available.",
        };

        return reach.IsSupported ? $"{work} {reach.Reason}" : $"{work} Blocked: {reach.Reason}";
    }
}

public sealed record ExplorationMissionPlan(
    int FleetId,
    string FleetName,
    FleetRole FleetRole,
    bool CanReceiveOrders,
    string Status,
    IReadOnlyList<ExplorationMissionCandidate> Candidates)
{
    public static ExplorationMissionPlan Unavailable(int fleetId, string status) =>
        new(fleetId, string.Empty, FleetRole.Scout, false, status, Array.Empty<ExplorationMissionCandidate>());
}

public sealed record ExplorationMissionCandidate(
    int SystemId,
    string CatalogName,
    SystemSurveyLevel SurveyLevel,
    double SurveyProgress,
    int PriorityBand,
    double DistanceFromFleet,
    double? EstimatedRemainingScienceSurveyDays,
    SurveyOperationalHazard? SurveyOperationalHazard,
    MissionReachAssessment Reach,
    string Reason);

public sealed record ExplorationMissionOrderAssessment(
    bool Accepted,
    bool IsLocalSurvey,
    string Message,
    ExplorationMissionCandidate? Candidate)
{
    public static ExplorationMissionOrderAssessment Approve(
        string message,
        ExplorationMissionCandidate candidate,
        bool localSurvey) => new(true, localSurvey, message, candidate);

    public static ExplorationMissionOrderAssessment Rejected(
        string message,
        ExplorationMissionCandidate? candidate = null) => new(false, false, message, candidate);
}
