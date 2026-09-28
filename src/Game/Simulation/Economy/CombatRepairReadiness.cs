using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Combat;
using Game.Simulation.Models;

namespace Game.Simulation.Economy;

/// <summary>
/// Why a damaged owned fleet can or cannot currently reach a represented repair facility.
/// This is a logistics/facility readiness classification only; it does not grant repair
/// capacity, create resources, spend Industry, or apply Combat durability restoration.
/// </summary>
public enum FleetRepairFacilityStatus
{
    ReadyAtRepresentedHomeShipyard,
    NoRepresentedHomeShipyard,
    OutsideRepresentedRepairNetwork,
    InTransitOrDeepSpace,
}

/// <summary>
/// Logistics-side readiness for one exact owned Combat repair demand.
/// </summary>
public sealed record FleetCombatRepairReadiness(
    FleetCombatRepairDemand RepairDemand,
    FleetRepairFacilityStatus FacilityStatus,
    int? RepairNodeId,
    string Reason)
{
    public bool CanReceiveRepresentedRepairs =>
        FacilityStatus == FleetRepairFacilityStatus.ReadyAtRepresentedHomeShipyard;
}

/// <summary>
/// Civilization-wide exact-own repair readiness. Foreign fleet truth must never be routed
/// through this view; CombatRepairDemandCalculator already scopes the demand to the owning
/// civilization supplied by the caller.
/// </summary>
public sealed record CivilizationCombatRepairReadiness(
    int CivilizationId,
    int HomeSystemId,
    bool HasRepresentedHomeShipyard,
    int? HomeShipyardNodeId,
    IReadOnlyList<FleetCombatRepairReadiness> Fleets)
{
    public int DamagedFleetCount => Fleets.Count;
    public int ReadyFleetCount => Fleets.Count(fleet => fleet.CanReceiveRepresentedRepairs);
    public int UnreadyFleetCount => DamagedFleetCount - ReadyFleetCount;
    public double TotalRepairDeficit => Fleets.Sum(fleet => fleet.RepairDemand.TotalMissingDurability);
    public double ReadyRepairDeficit => Fleets
        .Where(fleet => fleet.CanReceiveRepresentedRepairs)
        .Sum(fleet => fleet.RepairDemand.TotalMissingDurability);
    public double UnreadyRepairDeficit => Math.Max(0.0, TotalRepairDeficit - ReadyRepairDeficit);
}

public interface ICombatRepairReadinessView
{
    CivilizationCombatRepairReadiness Build(GalaxyState galaxy, int civilizationId);
}

/// <summary>
/// Early-release repair-facility adapter. The current authoritative infrastructure model only
/// places a completed civilization-level Orbital Shipyard in the civilization's home system.
/// Therefore only damaged owned fleets physically in that home system can currently be marked
/// repair-ready. Remote systems and in-transit fleets remain explicit unsupported cases until
/// authoritative remote yards, towing/freight access, or interstellar repair logistics exist.
/// </summary>
public sealed class PrototypeCombatRepairReadinessView : ICombatRepairReadinessView
{
    private readonly IHomeSystemLogisticsNetworkView _homeSystemView;

    public PrototypeCombatRepairReadinessView(IHomeSystemLogisticsNetworkView? homeSystemView = null)
    {
        _homeSystemView = homeSystemView ?? new PrototypeHomeSystemLogisticsNetworkView();
    }

    public CivilizationCombatRepairReadiness Build(GalaxyState galaxy, int civilizationId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);

        var civilization = galaxy.Civilizations.FirstOrDefault(candidate => candidate.Id == civilizationId)
            ?? throw new InvalidOperationException($"Unknown civilization {civilizationId}.");
        var homeNetwork = _homeSystemView.Build(galaxy, civilizationId);
        var shipyardNode = homeNetwork.Nodes
            .Where(node => node.Kind == LogisticsNodeKind.Shipyard)
            .OrderBy(node => node.Id)
            .FirstOrDefault();

        var demand = CombatRepairDemandCalculator.Build(galaxy, civilizationId);
        var readiness = demand.Fleets
            .OrderBy(fleet => fleet.FleetId)
            .Select(fleet => Classify(fleet, civilization.HomeSystemId, shipyardNode))
            .ToArray();

        return new CivilizationCombatRepairReadiness(
            civilizationId,
            civilization.HomeSystemId,
            shipyardNode is not null,
            shipyardNode?.Id,
            readiness);
    }

    private static FleetCombatRepairReadiness Classify(
        FleetCombatRepairDemand demand,
        int homeSystemId,
        LogisticsNode? shipyardNode)
    {
        if (demand.SystemId is null)
        {
            return new FleetCombatRepairReadiness(
                demand,
                FleetRepairFacilityStatus.InTransitOrDeepSpace,
                null,
                "Fleet is not currently located at a represented star system; no repair facility is reachable under the current model.");
        }

        if (demand.SystemId.Value != homeSystemId)
        {
            return new FleetCombatRepairReadiness(
                demand,
                FleetRepairFacilityStatus.OutsideRepresentedRepairNetwork,
                null,
                "Fleet is outside the represented home-system repair network; no remote yard or interstellar repair corridor is currently modeled.");
        }

        if (shipyardNode is null)
        {
            return new FleetCombatRepairReadiness(
                demand,
                FleetRepairFacilityStatus.NoRepresentedHomeShipyard,
                null,
                "Fleet is in the home system, but no completed represented Orbital Shipyard exists to service Combat repairs.");
        }

        return new FleetCombatRepairReadiness(
            demand,
            FleetRepairFacilityStatus.ReadyAtRepresentedHomeShipyard,
            shipyardNode.Id,
            "Fleet is in the home system and a represented Orbital Shipyard is available for a future externally budgeted repair allocation.");
    }
}
