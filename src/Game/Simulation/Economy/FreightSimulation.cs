using System;
using System.Linq;
using Game.Simulation.Construction;
using Game.Simulation.Exploration;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;

namespace Game.Simulation.Economy;

public sealed class FreightSimulation
{
    private readonly IInterstellarOperationalReachView _reach;

    public FreightSimulation(IInterstellarOperationalReachView? reach = null)
    {
        _reach = reach ?? new LaneInterstellarOperationalReachView();
    }

    public FreightOrderResult IssueTransitOrder(GalaxyState galaxy, int civilizationId, int fleetId, int targetSystemId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var fleet = galaxy.Fleets.FirstOrDefault(f => f.Id == fleetId && f.IsActive &&
            f.CivilizationId == civilizationId && f.Role == FleetRole.Logistics);
        if (fleet is null) return new(false, "Select an owned freighter first.");
        if (fleet.FreightHomeColonyId is not null || fleet.FreightTargetOutpostId is not null)
            return new(false, "Finish the current cargo collection and delivery before assigning another course.");
        var reach = _reach.Assess(galaxy, civilizationId, fleet, targetSystemId, InterstellarMissionKind.Logistics);
        if (!reach.IsSupported) return new(false, reach.Reason);
        FleetRouteOrders.Assign(galaxy, fleet, targetSystemId, reach);
        return new(true, $"{fleet.Name}: course set. {reach.Reason}");
    }

    public FreightOrderResult IssueCollectionOrder(GalaxyState galaxy, int civilizationId, int fleetId, int outpostId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var fleet = galaxy.Fleets.FirstOrDefault(candidate => candidate.Id == fleetId && candidate.IsActive &&
            candidate.CivilizationId == civilizationId && candidate.Role == FleetRole.Logistics &&
            candidate.DesignId == ShipDesignRegistry.BulkFreighterId);
        if (fleet is null) return new(false, "No controllable bulk freighter with that fleet ID is available.");
        if (fleet.DestinationSystemId is not null || fleet.FreightHomeColonyId is not null || fleet.FreightTargetOutpostId is not null || fleet.CargoMaterials > 0.0)
            return new(false, $"{fleet.Name} is already assigned to a freight run.");
        if (fleet.CurrentSystemId is not int originSystemId)
            return new(false, $"{fleet.Name} must finish its current lane leg before receiving a freight order.");
        var home = galaxy.Colonies.FirstOrDefault(colony => colony.CivilizationId == civilizationId &&
            colony.SystemId == originSystemId && colony.Kind == SettlementKind.Colony);
        if (home is null) return new(false, "A freight run must depart from one of your developed colonies.");
        var outpost = galaxy.Colonies.FirstOrDefault(colony => colony.Id == outpostId &&
            colony.CivilizationId == civilizationId && colony.Kind == SettlementKind.ResourceOutpost);
        if (outpost is null) return new(false, "That staffed resource outpost is unavailable.");
        var operations = ResourceOutpostOperations.GetSnapshot(galaxy, outpost);
        if (operations.StoredMaterials <= 0.0 && operations.ExtractionPerDay <= 0.0)
            return new(false, operations.Status);
        var assessment = _reach.Assess(galaxy, civilizationId, fleet, outpost.SystemId, InterstellarMissionKind.Logistics);
        if (!assessment.IsSupported) return new(false, assessment.Reason);

        fleet.FreightHomeColonyId = home.Id;
        fleet.FreightTargetOutpostId = outpost.Id;
        FleetRouteOrders.Assign(galaxy, fleet, outpost.SystemId, assessment);
        return new(true, $"{fleet.Name} dispatched to collect up to {fleet.CargoMaterialCapacity:0.#} material units from {outpost.Name}. {assessment.Reason}");
    }

    public void Advance(GalaxyState galaxy, double simulationDays = 1.0)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        if (!double.IsFinite(simulationDays) || simulationDays < 0.0)
            throw new ArgumentOutOfRangeException(nameof(simulationDays), "Freight transfer time must be finite and nonnegative.");
        if (simulationDays <= 0.0) return;
        foreach (var fleet in galaxy.Fleets.Where(candidate => candidate.IsActive && candidate.Role == FleetRole.Logistics &&
                     candidate.TransitPhase == FleetTransitPhase.None && candidate.DestinationSystemId is null && candidate.FreightHomeColonyId is not null))
        {
            if (CivilizationOperatingCapacity.GetFundingFraction(galaxy, fleet.CivilizationId) <= 0.0000001)
                continue;
            var home = galaxy.Colonies.FirstOrDefault(colony => colony.Id == fleet.FreightHomeColonyId &&
                colony.CivilizationId == fleet.CivilizationId && colony.Kind == SettlementKind.Colony);
            if (home is null) continue;

            if (fleet.FreightTargetOutpostId is int outpostId)
            {
                var outpost = galaxy.Colonies.FirstOrDefault(colony => colony.Id == outpostId &&
                    colony.CivilizationId == fleet.CivilizationId && colony.Kind == SettlementKind.ResourceOutpost);
                if (outpost is null || fleet.CurrentSystemId != outpost.SystemId) continue;
                var loaded = Math.Min(GetEffectiveTransferRatePerDay(fleet, outpost) * simulationDays,
                    Math.Min(outpost.StoredExtractedMaterials, fleet.CargoMaterialCapacity - fleet.CargoMaterials));
                outpost.StoredExtractedMaterials -= loaded;
                fleet.CargoMaterials += loaded;
                if (fleet.CargoMaterials + 0.0000001 < fleet.CargoMaterialCapacity &&
                    outpost.StoredExtractedMaterials > 0.0000001)
                    continue;

                var returnReach = _reach.Assess(galaxy, fleet.CivilizationId, fleet, home.SystemId, InterstellarMissionKind.Logistics);
                if (returnReach.IsSupported)
                {
                    fleet.FreightTargetOutpostId = null;
                    FleetRouteOrders.Assign(galaxy, fleet, home.SystemId, returnReach);
                }
                continue;
            }

            if (fleet.CurrentSystemId != home.SystemId) continue;
            var economy = galaxy.Economies.First(state => state.CivilizationId == fleet.CivilizationId);
            var freeStorage = Math.Max(0.0,
                EconomySimulation.GetIndustryStorageCapacity(galaxy, fleet.CivilizationId) - economy.Industry);
            var unloaded = Math.Min(GetEffectiveTransferRatePerDay(fleet, home) * simulationDays,
                Math.Min(fleet.CargoMaterials, freeStorage));
            economy.Industry += unloaded;
            fleet.CargoMaterials -= unloaded;
            if (fleet.CargoMaterials <= 0.0000001)
            {
                fleet.CargoMaterials = 0.0;
                fleet.FreightHomeColonyId = null;
            }
        }
    }

    public static double GetCargoTransferRatePerDay(FleetState fleet) =>
        ShipDesignRegistry.TryGet(fleet.DesignId, out var design) && design!.CargoTransferRatePerDay > 0.0
            ? design.CargoTransferRatePerDay
            : fleet.CargoMaterialCapacity > 0.0 ? 20.0 : 0.0;

    public const double BasicHubTransferCapacityPerDay = 4.0;

    public static double GetPortTransferCapacityPerDay(ColonyState colony) =>
        BasicHubTransferCapacityPerDay + SurfaceConstruction.GetOutput(colony).CargoTransferCapacityPerDay;

    public static double GetEffectiveTransferRatePerDay(FleetState fleet, ColonyState colony) =>
        Math.Min(GetCargoTransferRatePerDay(fleet), GetPortTransferCapacityPerDay(colony));
}

public sealed record FreightOrderResult(bool Accepted, string Message);
