using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Game.Simulation.Exploration;
using Game.Simulation.Economy;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;

namespace Game.Simulation.Colonization;

public sealed class ResourceOutpostOpportunityPlanner
{
    public const int DefaultMaximumCandidates = 32;
    public const int HardMaximumCandidates = 64;

    private readonly IInterstellarOperationalReachView _operationalReach;
    private readonly SpeciesPlanetaryReadModel _speciesReadModel = new();

    public ResourceOutpostOpportunityPlanner(IInterstellarOperationalReachView? operationalReach = null)
    {
        _operationalReach = operationalReach ?? new LaneInterstellarOperationalReachView();
    }

    public ResourceOutpostOpportunityPlan BuildPlan(GalaxyState galaxy, int fleetId, int maximumCandidates = DefaultMaximumCandidates)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        maximumCandidates = Math.Clamp(maximumCandidates, 1, HardMaximumCandidates);
        var fleet = FindOutpostFleet(galaxy, fleetId);
        if (fleet is null)
            return ResourceOutpostOpportunityPlan.Unavailable(fleetId, "No active staffed resource-outpost vessel with that fleet ID is available.");
        if (!TryResolvePersonnelSpecies(fleet, out var speciesId, out var speciesName, out var error))
            return ResourceOutpostOpportunityPlan.Unavailable(fleetId, error!);

        var systems = galaxy.Systems.ToDictionary(system => system.Id);
        var bodies = galaxy.PlanetaryBodies.ToDictionary(body => body.Id);
        var reaches = new Dictionary<int, MissionReachAssessment>();
        var suitability = _speciesReadModel.BuildForSpecies(galaxy, fleet.CivilizationId, speciesId!).ToDictionary(view => view.PlanetaryBodyId);
        var candidates = bodies.Values
            .Where(body => galaxy.Knowledge.GetSystemSurveyLevel(fleet.CivilizationId, body.SystemId) == SystemSurveyLevel.FullySurveyed)
            .Where(body => systems.ContainsKey(body.SystemId) && suitability.ContainsKey(body.Id))
            .Select(body =>
            {
                var system = systems[body.SystemId];
                if (!reaches.TryGetValue(system.Id, out var reach))
                {
                    reach = _operationalReach.Assess(galaxy, fleet.CivilizationId, fleet, system.Id, InterstellarMissionKind.Colony);
                    reaches[system.Id] = reach;
                }
                return BuildCandidate(galaxy, fleet, system, body, suitability[body.Id], reach, speciesName!);
            })
            .Where(candidate => candidate.HasRareResource)
            .OrderBy(candidate => candidate.CanOrder ? 0 : 1)
            .ThenBy(candidate => candidate.DistanceFromFleet)
            .ThenBy(candidate => candidate.SystemId)
            .ThenBy(candidate => candidate.PlanetaryBodyId)
            .Take(maximumCandidates)
            .ToArray();

        var orderable = candidates.Count(candidate => candidate.CanOrder);
        var status = candidates.Length == 0
            ? "No fully surveyed rare-resource worlds are currently available for outpost evaluation."
            : orderable == 0
                ? $"{candidates.Length} valuable world{(candidates.Length == 1 ? string.Empty : "s")} evaluated; none can currently receive a staffed outpost."
                : $"{orderable} staffed resource-outpost opportunit{(orderable == 1 ? "y" : "ies")} available in a {candidates.Length}-world planning window.";
        return new ResourceOutpostOpportunityPlan(fleet.Id, fleet.Name, fleet.CivilizationId, speciesId!, speciesName!, fleet.EmbarkedPopulationMillions, true, status, candidates);
    }

    public ResourceOutpostOrderAssessment AssessOrder(GalaxyState galaxy, int fleetId, int systemId, int bodyId)
    {
        var plan = BuildPlan(galaxy, fleetId, HardMaximumCandidates);
        if (!plan.CanReceiveOrders)
            return ResourceOutpostOrderAssessment.Reject(plan.Status);
        var candidate = plan.Candidates.FirstOrDefault(item => item.SystemId == systemId && item.PlanetaryBodyId == bodyId);
        if (candidate is null)
            return ResourceOutpostOrderAssessment.Reject("That body is not a fully surveyed rare-resource outpost candidate.");
        return candidate.CanOrder
            ? ResourceOutpostOrderAssessment.Approve($"{plan.FleetName}: staffed resource outpost approved for {candidate.PlanetaryBodyName} in {candidate.SystemName}. {candidate.Reason}", candidate)
            : ResourceOutpostOrderAssessment.Reject(candidate.Reason, candidate);
    }

    private static ResourceOutpostOpportunityCandidate BuildCandidate(
        GalaxyState galaxy, FleetState fleet, StarSystemState system, PlanetaryBodyState body,
        KnownSpeciesPlanetarySuitability suitability, MissionReachAssessment reach, string speciesName)
    {
        var deposit = ResourceDepositProfile.ForBody(body);
        var occupied = galaxy.Colonies.Any(colony => colony.SystemId == system.Id);
        var reserved = galaxy.Fleets.Any(other => other.Id != fleet.Id && other.IsActive &&
            other.CivilizationId == fleet.CivilizationId && other.Role == FleetRole.Colony &&
            other.DestinationSystemId == system.Id);
        var harsh = suitability.ColonizationViability == SpeciesColonizationViability.Unsuitable;
        var expeditionAffordable = fleet.DestinationSystemId is not null ||
            galaxy.Economies.First(economy => economy.CivilizationId == fleet.CivilizationId).Credits + 0.0001 >=
            ColonizationSimulation.ResourceOutpostExpeditionCreditCost;
        var canOrder = body.Environment.HasSolidSurface && body.HasRareResource && !body.HasPreWarpCivilization &&
            harsh && !occupied && !reserved && reach.IsSupported && expeditionAffordable;
        string reason;
        if (!body.Environment.HasSolidSurface) reason = $"{body.Name} has no solid surface for the current outpost model.";
        else if (!body.HasRareResource) reason = $"{body.Name} has no confirmed rare-resource deposit.";
        else if (body.HasPreWarpCivilization) reason = $"A native pre-warp civilization already inhabits {body.Name}.";
        else if (!harsh) reason = $"{body.Name} can support a colony for {speciesName}; use a colony ship instead of consuming a sealed outpost vessel.";
        else if (occupied) reason = "That system already contains a settlement in the current single-settlement model.";
        else if (reserved) reason = "Another friendly settlement vessel is already committed to that system.";
        else if (!reach.IsSupported) reason = reach.Reason;
        else if (!expeditionAffordable) reason = $"{Game.Simulation.Economy.SovereignCurrencyCatalog.ForCivilization(galaxy, fleet.CivilizationId).Format(ColonizationSimulation.ResourceOutpostExpeditionCreditCost)} is required to fund the resource-outpost expedition.";
        else reason = $"{body.Name} is too harsh for colonization but its confirmed deposit can support a sealed staffed outpost. {reach.Reason}";

        return new ResourceOutpostOpportunityCandidate(system.Id, system.Name, body.Id, body.Name,
            fleet.Id, suitability.SpeciesId, suitability.NaturalHabitability,
            suitability.UnprotectedOperationalCapacity, suitability.LimitingFactor,
            body.HasRareResource, deposit.MaterialName, deposit.Grade, deposit.Accessibility,
            deposit.ExtractionYieldMultiplier, ResourceOutpostOperations.InitialDepositReserve(body),
            occupied, harsh, InterstellarDistance.FromFleet(galaxy, fleet, system), reach, canOrder, reason);
    }

    public static bool IsOutpostFleet(FleetState fleet) =>
        fleet.IsActive && fleet.Role == FleetRole.Colony &&
        fleet.DesignId == ShipDesignRegistry.ResourceOutpostShipId && fleet.EmbarkedPopulationMillions > 0.0;

    private static FleetState? FindOutpostFleet(GalaxyState galaxy, int fleetId) =>
        galaxy.Fleets.FirstOrDefault(fleet => fleet.Id == fleetId && IsOutpostFleet(fleet));

    private static bool TryResolvePersonnelSpecies(FleetState fleet, out string? speciesId, out string? speciesName, out string? error)
    {
        speciesId = fleet.EmbarkedPopulationSpeciesId;
        if (string.IsNullOrWhiteSpace(speciesId) || !SpeciesCatalog.TryGet(speciesId, out var species) || species is null)
        {
            speciesName = null;
            error = "The outpost vessel's specialist personnel species identity is invalid.";
            return false;
        }
        speciesName = species.DisplayName;
        error = null;
        return true;
    }
}

public sealed record ResourceOutpostOpportunityPlan(int FleetId, string FleetName, int CivilizationId,
    string PersonnelSpeciesId, string PersonnelSpeciesName, double PersonnelMillions, bool CanReceiveOrders,
    string Status, IReadOnlyList<ResourceOutpostOpportunityCandidate> Candidates)
{
    public static ResourceOutpostOpportunityPlan Unavailable(int fleetId, string status) =>
        new(fleetId, string.Empty, -1, string.Empty, string.Empty, 0.0, false, status, Array.Empty<ResourceOutpostOpportunityCandidate>());
}

public sealed record ResourceOutpostOpportunityCandidate(int SystemId, string SystemName, int PlanetaryBodyId,
    string PlanetaryBodyName, int FleetId, string PersonnelSpeciesId, double NaturalHabitability,
    double UnprotectedOperationalCapacity, EnvironmentalLimitingFactor LimitingFactor, bool HasRareResource,
    string DepositMaterialName, string DepositGrade, double DepositAccessibility,
    double ExtractionYieldMultiplier, double InitialDepositMaterials,
    bool SystemOccupied, bool IsTooHarshForColony, double DistanceFromFleet, MissionReachAssessment Reach,
    bool CanOrder, string Reason);

public sealed record ResourceOutpostOrderAssessment(bool Accepted, string Message, ResourceOutpostOpportunityCandidate? Candidate)
{
    public static ResourceOutpostOrderAssessment Approve(string message, ResourceOutpostOpportunityCandidate candidate) => new(true, message, candidate);
    public static ResourceOutpostOrderAssessment Reject(string message, ResourceOutpostOpportunityCandidate? candidate = null) => new(false, message, candidate);
}
