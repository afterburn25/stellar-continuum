using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Economy;

namespace Game.Simulation.Exploration;

public sealed class ExplorationSimulation
{
    // Retained as the original/reference survey rate for callers that display historical tuning.
    // Active science progress now comes from the bounded SurveyOperationsProfile per system.
    public const double ScienceSurveyProgressPerDay = 0.08;
    public const double ScoutReconnaissanceProgress = 0.35;
    public const double ScoutReconnaissanceDays = 2;

    private readonly SurveyOperationsProfiler _surveyProfiler;
    private readonly ExplorationMissionPlanner _missionPlanner;
    private readonly ExplorationAiMissionCoordinator _aiMissionCoordinator;

    public ExplorationSimulation(
        IInterstellarOperationalReachView? operationalReach = null,
        SurveyOperationsProfiler? surveyProfiler = null)
    {
        var reach = operationalReach ?? new LaneInterstellarOperationalReachView();
        _surveyProfiler = surveyProfiler ?? new SurveyOperationsProfiler();
        _missionPlanner = new ExplorationMissionPlanner(reach, _surveyProfiler);
        _aiMissionCoordinator = new ExplorationAiMissionCoordinator(_missionPlanner);
    }

    public IReadOnlyList<ExplorationEvent> Advance(GalaxyState galaxy, double simulationDelta)
    {
        if (!double.IsFinite(simulationDelta) || simulationDelta < 0)
            throw new ArgumentOutOfRangeException(nameof(simulationDelta));
        if (simulationDelta <= 0.0)
            return Array.Empty<ExplorationEvent>();

        var events = new List<ExplorationEvent>();

        foreach (var fleet in galaxy.Fleets.Where(fleet => fleet.IsActive))
        {
            var civilization = galaxy.Civilizations.First(c => c.Id == fleet.CivilizationId);
            var operatingCapacity = CivilizationOperatingCapacity.GetFundingFraction(galaxy, fleet.CivilizationId);
            if (operatingCapacity <= 0.0000001)
                continue;
            if (fleet.CurrentSystemId is int refuelSystemId)
            {
                var service = RefuelingServiceLevel(galaxy, fleet.CivilizationId, refuelSystemId);
                if (service > 0.0)
                    fleet.FuelRemainingLightYears = Math.Max(
                        fleet.FuelRemainingLightYears,
                        fleet.FuelCapacityLightYears * service);
            }

            // Local holds stop immediately. A warp hold deliberately completes its active lane
            // and becomes a hold at the inbound chart gate.
            if (fleet.HoldRequested && fleet.CurrentSystemId is not null && fleet.TransitPhase != FleetTransitPhase.InterstellarWarp)
                continue;

            if (fleet.TransitPhase == FleetTransitPhase.None && fleet.DestinationSystemId is null &&
                IsSurveyFleet(fleet) &&
                fleet.CurrentSystemId is int localSystemId &&
                ProcessLocalSurvey(galaxy, fleet, localSystemId, simulationDelta * operatingCapacity, events))
            {
                // An actively surveying vessel is a legitimate directional observer of foreign
                // presence in its current system. This keeps first-contact semantics one-way:
                // passive foreign fleets/colonies do not automatically receive reciprocal
                // knowledge merely because another civilization is surveying nearby.
                DetectCivilizationContacts(galaxy, fleet, events);

                // Surveying consumes this fleet's activity for the current simulation step.
                // This avoids frame/order-dependent survey + movement in the same tick.
                continue;
            }

            if (fleet.TransitPhase == FleetTransitPhase.None && fleet.DestinationSystemId is null && !civilization.IsPlayer && IsSurveyFleet(fleet))
                AssignAiSurveyDestination(galaxy, fleet);

            if (fleet.DestinationSystemId is null && fleet.TransitPhase == FleetTransitPhase.LocalArrival && fleet.CurrentSystemId is not null)
            {
                _ = FleetLocalTransit.Advance(fleet, simulationDelta * operatingCapacity);
                if (FleetLocalTransit.Complete(fleet))
                {
                    fleet.TransitPhase = FleetTransitPhase.None;
                    fleet.TransitOriginSystemId = null;
                    fleet.TransitTargetSystemId = null;
                    fleet.TransitProgress = 0;
                    fleet.LocalTransitPosition = Vector2.Zero;
                }
                continue;
            }
            if (fleet.DestinationSystemId is null)
                continue;

            // Compatibility for callers that construct the pre-local-transit in-flight shape.
            // Preserve its strategic coordinate and finish that lane before creating local state.
            if (fleet.CurrentSystemId is null && fleet.TransitPhase == FleetTransitPhase.None)
            {
                fleet.TransitPhase = FleetTransitPhase.InterstellarWarp;
                fleet.TransitTargetSystemId = fleet.PlannedRouteSystemIds.Count > 0
                    ? fleet.PlannedRouteSystemIds[0] : fleet.DestinationSystemId;
            }

            var remainingDays = simulationDelta * operatingCapacity;
            while (fleet.DestinationSystemId is not null && remainingDays > .0000001)
            {
                var movementTargetId = fleet.PlannedRouteSystemIds.Count > 0
                    ? fleet.PlannedRouteSystemIds[0]
                    : fleet.DestinationSystemId.Value;
                var target = galaxy.Systems.First(s => s.Id == movementTargetId);
                if (fleet.TransitPhase == FleetTransitPhase.None)
                {
                    if (fleet.CurrentSystemId is not int originId) break;
                    var origin = galaxy.Systems.First(s => s.Id == originId);
                    fleet.Position = origin.Position;
                    fleet.TransitOriginSystemId = origin.Id;
                    fleet.TransitTargetSystemId = target.Id;
                    FleetLocalTransit.Begin(fleet, FleetTransitPhase.LocalDeparture,
                        FleetLocalTransit.Finite(fleet.LocalTransitPosition) ? fleet.LocalTransitPosition : Vector2.Zero,
                        FleetLocalTransit.GateTowards(target.Position, origin.Position));
                }

                if (fleet.TransitPhase is FleetTransitPhase.LocalDeparture or FleetTransitPhase.LocalArrival)
                {
                    var spentDays = FleetLocalTransit.Advance(fleet, remainingDays);
                    remainingDays -= spentDays;
                    if (!FleetLocalTransit.Complete(fleet)) break;
                    if (fleet.TransitPhase == FleetTransitPhase.LocalDeparture)
                    {
                        fleet.TransitPhase = FleetTransitPhase.InterstellarWarp;
                        fleet.TransitProgress = 0;
                        fleet.CurrentSystemId = null;
                        continue;
                    }
                    // An intermediate system is crossed from its inbound gate straight to the
                    // next outbound gate. Only a final inbound-to-centre leg is an arrival.
                    var finalArrival = target.Id == fleet.DestinationSystemId && fleet.PlannedRouteSystemIds.Count <= 1;
                    if (!finalArrival)
                    {
                        if (fleet.PlannedRouteSystemIds.Count > 0)
                            fleet.PlannedRouteSystemIds.RemoveAt(0);
                        fleet.TransitOriginSystemId = target.Id;
                        fleet.TransitTargetSystemId = fleet.PlannedRouteSystemIds.Count > 0
                            ? fleet.PlannedRouteSystemIds[0] : fleet.DestinationSystemId;
                        fleet.TransitPhase = FleetTransitPhase.InterstellarWarp;
                        fleet.TransitProgress = 0;
                        fleet.CurrentSystemId = null;
                        continue;
                    }
                    fleet.TransitPhase = FleetTransitPhase.None;
                    fleet.TransitOriginSystemId = null;
                    fleet.TransitTargetSystemId = null;
                    fleet.TransitProgress = 0;
                    fleet.Position = target.Position;
                    if (fleet.PlannedRouteSystemIds.Count > 0)
                        fleet.PlannedRouteSystemIds.RemoveAt(0);
                    var reachedFinalDestination = target.Id == fleet.DestinationSystemId && fleet.PlannedRouteSystemIds.Count == 0;
                    if (reachedFinalDestination && !fleet.HoldRequested && !fleet.ReturnToBaseRequested)
                        fleet.DestinationSystemId = null;
                    // Preserve the final destination (including a paid colony authorization)
                    // when this is the held arrival lane. Resume consumes the zero-distance
                    // arrival normally and then allows local work on a later simulation step.
                    if (fleet.HoldRequested)
                        break;
                    continue;
                }

                // Physical lane distance includes the optional galactic depth. Fleet.Position
                // remains a chart coordinate, so mid-warp position is interpolated in 2D from
                // the same physical progress ratio.
                var physicalOrigin = fleet.TransitOriginSystemId is int transitOriginId
                    ? galaxy.Systems.FirstOrDefault(system => system.Id == transitOriginId)
                    : null;
                var fullDistance = physicalOrigin is null ? 0.0 : InterstellarDistance.Between(physicalOrigin, target);
                var progress = Math.Clamp(fleet.TransitProgress, 0.0, 1.0);
                if (fullDistance <= .000001 && physicalOrigin is not null)
                    fullDistance = Vector2.Distance(physicalOrigin.Position, target.Position);
                var distance = physicalOrigin is null
                    ? Vector2.Distance(fleet.Position, target.Position)
                    : fullDistance * (1.0 - progress);
                var availableDistance = Math.Min(fleet.StrategicSpeed * remainingDays, fleet.FuelRemainingLightYears);
                if (availableDistance <= 0 && distance > .000001f) break;
                if (distance > availableDistance)
                {
                    var nextProgress = physicalOrigin is null || fullDistance <= .000001
                        ? progress
                        : Math.Clamp(progress + availableDistance / fullDistance, 0.0, 1.0);
                    fleet.Position = physicalOrigin is null
                        ? fleet.Position + Vector2.Normalize(target.Position - fleet.Position) * (float)availableDistance
                        : InterstellarDistance.InterpolateChartPosition(physicalOrigin, target, nextProgress);
                    fleet.FuelRemainingLightYears -= availableDistance;
                    fleet.TransitProgress = physicalOrigin is null || fullDistance <= .000001
                        ? 0
                        : nextProgress;
                    break;
                }
                var warpDays = distance / Math.Max(.1, fleet.StrategicSpeed);
                remainingDays -= warpDays;
                fleet.FuelRemainingLightYears = Math.Max(0, fleet.FuelRemainingLightYears - distance);
                var arrivalApproachPosition = fleet.Position;
                fleet.Position = target.Position;
                fleet.CurrentSystemId = target.Id;
                var originPosition = physicalOrigin?.Position ?? arrivalApproachPosition;
                var inbound = FleetLocalTransit.GateTowards(originPosition, target.Position);
                var finalTarget = target.Id == fleet.DestinationSystemId && fleet.PlannedRouteSystemIds.Count <= 1
                    ? Vector2.Zero
                    : FleetLocalTransit.GateTowards(
                        galaxy.Systems.First(s => s.Id == (fleet.PlannedRouteSystemIds.Count > 1 ? fleet.PlannedRouteSystemIds[1] : fleet.DestinationSystemId!.Value)).Position,
                        target.Position);
                FleetLocalTransit.Begin(fleet, FleetTransitPhase.LocalArrival, inbound, finalTarget);
                if (HandleInboundSystemReached(galaxy, fleet, target, events)) break;
                if (fleet.HoldRequested) break;
            }
        }

        return events;
    }

    private static bool HandleInboundSystemReached(
        GalaxyState galaxy, FleetState fleet, StarSystemState target, ICollection<ExplorationEvent> events)
    {
        var service = RefuelingServiceLevel(galaxy, fleet.CivilizationId, target.Id);
        if (service > 0)
            fleet.FuelRemainingLightYears = Math.Max(fleet.FuelRemainingLightYears, fleet.FuelCapacityLightYears * service);
        var alreadyKnown = galaxy.Knowledge.IsSystemKnown(fleet.CivilizationId, target.Id);
        var revealed = galaxy.Knowledge.RevealWithinSensorRange(fleet.CivilizationId, target.Id, galaxy.Systems, fleet.SensorRange);
        if (!alreadyKnown)
            events.Add(new ExplorationEvent(ExplorationEventType.SystemDetected, fleet.CivilizationId, fleet.Id, target.Id,
                $"{fleet.Name} reached astronomical target {target.Id + 1:000}; detailed system data still requires survey work."));
        if (revealed > 0)
            events.Add(new ExplorationEvent(ExplorationEventType.SensorContact, fleet.CivilizationId, fleet.Id, target.Id,
                $"Sensors added {revealed} system{(revealed == 1 ? string.Empty : "s")} to the local chart."));
        DetectCivilizationContacts(galaxy, fleet, events);
        if (!fleet.ReturnToBaseRequested) return false;
        _ = CivilianFleetReturnOrders.ActivateQueuedReturnAtSystem(galaxy, fleet);
        return true;
    }

    private static double RefuelingServiceLevel(GalaxyState galaxy, int civilizationId, int systemId)
    {
        var settlements = galaxy.Colonies.Where(colony =>
            colony.CivilizationId == civilizationId && colony.SystemId == systemId);
        return settlements.Any(colony => colony.Kind == SettlementKind.Colony)
            ? 1.0
            : settlements.Any(colony => colony.Kind == SettlementKind.ResourceOutpost) ? 0.5
                : Game.Simulation.Territory.TerritorialConstruction.Refueling(galaxy, civilizationId, systemId);
    }

    /// <summary>
    /// Compatibility wrapper retained for existing callers. New UI should use IssueSurveyOrder
    /// so a rejected order carries the same reason and candidate state used by AI planning.
    /// </summary>
    public bool IssueMoveOrder(GalaxyState galaxy, int fleetId, int destinationSystemId) =>
        IssueSurveyOrder(galaxy, fleetId, destinationSystemId).Accepted;

    public ExplorationMissionOrderAssessment IssueTravelOrder(GalaxyState galaxy, int fleetId, int destinationSystemId) =>
        IssueOrder(galaxy, fleetId, destinationSystemId, false);

    public ExplorationMissionOrderAssessment IssueSurveyOrder(GalaxyState galaxy, int fleetId, int destinationSystemId) =>
        IssueOrder(galaxy, fleetId, destinationSystemId, true);

    private ExplorationMissionOrderAssessment IssueOrder(GalaxyState galaxy, int fleetId, int destinationSystemId, bool requireSurveyWork)
    {
        var assessment = _missionPlanner.AssessOrder(galaxy, fleetId, destinationSystemId, requireSurveyWork);
        if (!assessment.Accepted)
            return assessment;

        var fleet = galaxy.Fleets.First(f => f.Id == fleetId && f.IsActive);
        if (assessment.IsLocalSurvey)
        {
            FleetRouteOrders.Clear(fleet);
            return assessment;
        }

        FleetRouteOrders.Assign(galaxy, fleet, destinationSystemId, assessment.Candidate!.Reach);
        return assessment;
    }

    public ExplorationMissionPlan GetMissionPlan(
        GalaxyState galaxy,
        int fleetId,
        int maximumCandidates = ExplorationMissionPlanner.DefaultMaximumCandidates) =>
        _missionPlanner.BuildPlan(galaxy, fleetId, maximumCandidates);

    public MissionReachAssessment AssessOperationalReach(GalaxyState galaxy, int fleetId, int destinationSystemId)
    {
        var fleet = galaxy.Fleets.FirstOrDefault(f => f.Id == fleetId && f.IsActive);
        return fleet is null
            ? MissionReachAssessment.Unsupported("No active exploration vessel is available.")
            : _missionPlanner.AssessOperationalReach(galaxy, fleet, destinationSystemId);
    }

    public SurveyOperationsProfile GetSurveyOperationsProfile(GalaxyState galaxy, int systemId) =>
        _surveyProfiler.Build(galaxy, systemId);

    private static bool IsSurveyFleet(FleetState fleet) => fleet.Role is FleetRole.Scout or FleetRole.Science;

    private bool ProcessLocalSurvey(
        GalaxyState galaxy,
        FleetState fleet,
        int systemId,
        double simulationDelta,
        ICollection<ExplorationEvent> events)
    {
        if (fleet.Role == FleetRole.Scout)
        {
            if (galaxy.Knowledge.GetSystemSurveyLevel(fleet.CivilizationId, systemId) >= SystemSurveyLevel.PartiallySurveyed)
                return false;

            if (fleet.ReconnaissanceSystemId != systemId)
            {
                fleet.ReconnaissanceSystemId = systemId;
                fleet.ReconnaissanceDaysCompleted = 0;
            }
            fleet.ReconnaissanceDaysCompleted = Math.Min(ScoutReconnaissanceDays, fleet.ReconnaissanceDaysCompleted + simulationDelta);
            if (fleet.ReconnaissanceDaysCompleted + 1e-9 < ScoutReconnaissanceDays) return true;

            if (!galaxy.Knowledge.RecordReconnaissance(
                    fleet.CivilizationId,
                    systemId,
                    ScoutReconnaissanceProgress))
            {
                return false;
            }

            var system = galaxy.Systems.First(s => s.Id == systemId);
            var profile = _surveyProfiler.Build(galaxy, systemId);
            events.Add(new ExplorationEvent(
                ExplorationEventType.SystemReconnoitered,
                fleet.CivilizationId,
                fleet.Id,
                systemId,
                $"{fleet.Name} completed a rapid reconnaissance pass of {system.Name}; estimated detailed survey effort is {profile.EstimatedScienceSurveyDays:0.#} days ({profile.OperationalHazard.ToString().ToLowerInvariant()} survey conditions)."));
            EmitReconnaissanceSignatures(galaxy, fleet, systemId, events);
            return true;
        }

        if (fleet.Role != FleetRole.Science || galaxy.Knowledge.IsSystemFullySurveyed(fleet.CivilizationId, systemId))
            return false;

        var previousLevel = galaxy.Knowledge.GetSystemSurveyLevel(fleet.CivilizationId, systemId);
        var previousProgress = galaxy.Knowledge.GetSystemSurveyProgress(fleet.CivilizationId, systemId);
        var profileForSystem = _surveyProfiler.Build(galaxy, systemId);
        var completed = galaxy.Knowledge.AdvanceSystemSurvey(
            fleet.CivilizationId,
            systemId,
            profileForSystem.ProgressPerDay * simulationDelta);
        var currentProgress = galaxy.Knowledge.GetSystemSurveyProgress(fleet.CivilizationId, systemId);
        var currentLevel = galaxy.Knowledge.GetSystemSurveyLevel(fleet.CivilizationId, systemId);

        if (currentProgress <= previousProgress + 0.0000001)
            return false;

        var systemState = galaxy.Systems.First(s => s.Id == systemId);
        if (previousLevel < SystemSurveyLevel.PartiallySurveyed)
        {
            events.Add(new ExplorationEvent(
                ExplorationEventType.SystemSurveyStarted,
                fleet.CivilizationId,
                fleet.Id,
                systemId,
                $"{fleet.Name} began a detailed science survey of {systemState.Name}; estimated total effort is {profileForSystem.EstimatedScienceSurveyDays:0.#} days."));

            // A science vessel can establish reconnaissance-grade knowledge without a scout.
            // Emit the same positive-only signature evidence exactly on that transition so
            // event consumers stay synchronized with the observer-safe read model. If a very
            // large deterministic step jumps directly to a full survey, skip transient
            // unconfirmed signatures and emit only confirmed discoveries below.
            if (currentLevel == SystemSurveyLevel.PartiallySurveyed)
                EmitReconnaissanceSignatures(galaxy, fleet, systemId, events);
        }

        if (completed)
        {
            events.Add(new ExplorationEvent(
                ExplorationEventType.SystemSurveyed,
                fleet.CivilizationId,
                fleet.Id,
                systemId,
                $"{fleet.Name} completed a detailed survey of {systemState.Name}."));
            EmitConfirmedBodyDiscoveries(galaxy, fleet, systemId, events);
        }

        return true;
    }

    private static void EmitReconnaissanceSignatures(
        GalaxyState galaxy,
        FleetState fleet,
        int systemId,
        ICollection<ExplorationEvent> events)
    {
        foreach (var body in galaxy.PlanetaryBodies.Where(body => body.SystemId == systemId).OrderBy(body => body.Id))
        {
            if (body.HasRareResource)
            {
                events.Add(new ExplorationEvent(
                    ExplorationEventType.ResourceSignatureDetected,
                    fleet.CivilizationId,
                    fleet.Id,
                    systemId,
                    $"{fleet.Name} detected an unusual resource signature near {body.Name}; detailed survey is required to confirm it.",
                    PlanetaryBodyId: body.Id));
            }
            if (body.HasAnomaly)
            {
                events.Add(new ExplorationEvent(
                    ExplorationEventType.AnomalySignatureDetected,
                    fleet.CivilizationId,
                    fleet.Id,
                    systemId,
                    $"{fleet.Name} detected an anomalous signature near {body.Name}; its nature remains unconfirmed.",
                    PlanetaryBodyId: body.Id));
            }
            if (body.HasPreWarpCivilization)
            {
                events.Add(new ExplorationEvent(
                    ExplorationEventType.ActivitySignatureDetected,
                    fleet.CivilizationId,
                    fleet.Id,
                    systemId,
                    $"{fleet.Name} detected unresolved activity signatures from {body.Name}; detailed survey is required before classification.",
                    PlanetaryBodyId: body.Id));
            }
        }
    }

    private static void EmitConfirmedBodyDiscoveries(
        GalaxyState galaxy,
        FleetState fleet,
        int systemId,
        ICollection<ExplorationEvent> events)
    {
        foreach (var body in galaxy.PlanetaryBodies.Where(body => body.SystemId == systemId).OrderBy(body => body.Id))
        {
            if (body.HasAnomaly)
            {
                events.Add(new ExplorationEvent(
                    ExplorationEventType.AnomalySurveyed,
                    fleet.CivilizationId,
                    fleet.Id,
                    systemId,
                    $"{fleet.Name} confirmed an anomaly on or near {body.Name}.",
                    PlanetaryBodyId: body.Id));
            }
            if (body.HasRareResource)
            {
                events.Add(new ExplorationEvent(
                    ExplorationEventType.ResourceSurveyed,
                    fleet.CivilizationId,
                    fleet.Id,
                    systemId,
                    $"{fleet.Name} confirmed a rare-resource deposit or signature associated with {body.Name}.",
                    PlanetaryBodyId: body.Id));
            }
            if (body.HasPreWarpCivilization)
            {
                events.Add(new ExplorationEvent(
                    ExplorationEventType.NativeCivilizationSurveyed,
                    fleet.CivilizationId,
                    fleet.Id,
                    systemId,
                    $"{fleet.Name} confirmed a native pre-warp civilization on {body.Name}.",
                    PlanetaryBodyId: body.Id));
            }
        }
    }

    private void AssignAiSurveyDestination(GalaxyState galaxy, FleetState fleet)
    {
        var selection = _aiMissionCoordinator.SelectMission(galaxy, fleet);
        if (selection.Candidate is not null)
            FleetRouteOrders.Assign(galaxy, fleet, selection.Candidate.SystemId, selection.Candidate.Reach);
    }

    private static void DetectCivilizationContacts(GalaxyState galaxy, FleetState fleet, ICollection<ExplorationEvent> events)
    {
        if (fleet.CurrentSystemId is not int currentSystemId)
            return;

        foreach (var other in galaxy.Civilizations)
        {
            if (other.Id == fleet.CivilizationId ||
                galaxy.Knowledge.IsCivilizationKnown(fleet.CivilizationId, other.Id))
            {
                continue;
            }

            // A catalog/sensor-known star is not evidence of who lives there. Identification
            // requires actual same-system foreign presence until Diplomacy provides a richer
            // legitimate-contact opportunity model (signals, hails, remote detection, etc.).
            var foreignColonyPresent = galaxy.Colonies.Any(colony =>
                colony.CivilizationId == other.Id && colony.SystemId == currentSystemId);
            var foreignFleetPresent = galaxy.Fleets.Any(otherFleet =>
                otherFleet.IsActive &&
                otherFleet.CivilizationId == other.Id &&
                otherFleet.CurrentSystemId == currentSystemId);
            if (!foreignColonyPresent && !foreignFleetPresent)
                continue;

            galaxy.Knowledge.RevealCivilization(fleet.CivilizationId, other.Id);
            events.Add(new ExplorationEvent(
                ExplorationEventType.FirstContact,
                fleet.CivilizationId,
                fleet.Id,
                currentSystemId,
                $"First contact: {other.Name}.",
                other.Id));
        }
    }
}

public enum ExplorationEventType
{
    SystemDetected,
    SystemReconnoitered,
    SystemSurveyStarted,
    SystemSurveyed,
    ResourceSignatureDetected,
    AnomalySignatureDetected,
    ActivitySignatureDetected,
    ResourceSurveyed,
    AnomalySurveyed,
    NativeCivilizationSurveyed,
    SensorContact,
    FirstContact,
}

public sealed record ExplorationEvent(
    ExplorationEventType Type,
    int CivilizationId,
    int FleetId,
    int SystemId,
    string Message,
    int? TargetCivilizationId = null,
    int? PlanetaryBodyId = null);
