using System.Linq;
using System.Text;
using Game.Simulation.Economy;
using Game.Simulation.Exploration;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;

namespace Game.Presentation;

public sealed record UiOwnedFleetSnapshot(int FleetId, FleetRole Role, string Name, string Location,
    string Activity, string DesignId, string DesignName, string ArtworkPath, double StrategicSpeed, double MaximumLegRangeLightYears,
    double FuelRemainingLightYears, double FuelCapacityLightYears,
    double CargoMaterials, double CargoMaterialCapacity, double CargoTransferRatePerDay,
    int RemainingRouteLegs, double RemainingRouteDistanceLightYears,
    double OperatingCostPerDay, bool IsArmed, double Integrity, string MilitaryOrder,
    int? CurrentSystemId, int? DestinationSystemId, int? DestinationPlanetaryBodyId,
    string Destination, string NextStop, bool HoldRequested, bool ReturnToBaseRequested, string? ReturnToBaseFailureReason,
    int? SettlementBodyId, double EmbarkedPopulationMillions, double CombatPower = 0);
public sealed record UiExplorationMissionSnapshot(int FleetId, FleetRole Role, string FleetName,
    string Phase, string Destination, string Eta, string Summary);

/// <summary>
/// Player-facing exploration adapter. All mission phase/ETA calculations come from the
/// observer-safe ExplorationReadModel; presentation code does not recompute travel or survey math.
/// </summary>
public partial class Main
{
    private readonly ExplorationReadModel _explorationReadModel = new();
    private readonly ExplorationMissionStatusEvaluator _missionStatusEvaluator = new();

    public UiOwnedFleetSnapshot[] UiOwnedFleets
    {
        get
        {
            if (_galaxy is null) return System.Array.Empty<UiOwnedFleetSnapshot>();
            var combat = _coreSimulation.GetOwnCombatFleetStatus(_galaxy, _galaxy.PlayerCivilizationId)
                .Fleets.ToDictionary(status => status.FleetId);
            return _galaxy.Fleets
                .Where(fleet => fleet.IsActive && fleet.CivilizationId == _galaxy.PlayerCivilizationId)
                .OrderBy(fleet => fleet.Role).ThenBy(fleet => fleet.Id)
                .Select(fleet =>
                {
                    var location = fleet.CurrentSystemId is int id
                        ? _galaxy.Systems.FirstOrDefault(system => system.Id == id)?.Name ?? "Deep space"
                        : "Deep space";
                    var activity = fleet.HoldRequested
                        ? fleet.ReturnToBaseFailureReason is { Length: > 0 } recovery
                            ? $"Held: {recovery}"
                            : fleet.CurrentSystemId is int heldAt
                                ? $"Held at {_galaxy.Systems.First(system => system.Id == heldAt).Name}"
                                : "Holding at next system"
                        : fleet.ReturnToBaseFailureReason is { Length: > 0 } recoveryReason
                        ? $"Recovery hold: {recoveryReason}"
                        : fleet.ReturnToBaseRequested
                        ? "Returning to base"
                        : fleet.Role == FleetRole.Military && fleet.DestinationSystemId is int deployment
                        ? $"Deploying to {_galaxy.Systems.First(system => system.Id == deployment).Name}"
                        : fleet.Role == FleetRole.Logistics && fleet.FreightTargetOutpostId is not null
                        ? fleet.DestinationSystemId is not null
                            ? "Outbound freight collection"
                            : $"Loading cargo · {fleet.CargoMaterials:0.#}/{fleet.CargoMaterialCapacity:0.#}"
                        : fleet.Role == FleetRole.Logistics && fleet.FreightHomeColonyId is not null
                        ? fleet.DestinationSystemId is not null
                            ? $"Returning cargo · {fleet.CargoMaterials:0.#}/{fleet.CargoMaterialCapacity:0.#}"
                            : $"Unloading cargo · {fleet.CargoMaterials:0.#}/{fleet.CargoMaterialCapacity:0.#}"
                        : fleet.Role is FleetRole.Scout or FleetRole.Science or FleetRole.Colony
                        ? FormatMissionPhase(_missionStatusEvaluator.Build(_galaxy, fleet).Phase)
                        : fleet.CurrentSystemId.HasValue ? "On station" : "In transit";
                    var combatStatus = combat[fleet.Id];
                    var route = FleetRouteMetrics.Measure(_galaxy, fleet);
                    var destination = fleet.DestinationSystemId is int destinationId
                        ? _galaxy.Systems.FirstOrDefault(system => system.Id == destinationId)?.Name ?? "Unknown system"
                        : fleet.CurrentSystemId is int currentId
                            ? _galaxy.Systems.FirstOrDefault(system => system.Id == currentId)?.Name ?? "Current system"
                            : "No destination";
                    var nextStopId = fleet.PlannedRouteSystemIds.Count > 0
                        ? fleet.PlannedRouteSystemIds[0]
                        : fleet.DestinationSystemId;
                    var nextStop = nextStopId is int nextId
                        ? _galaxy.Systems.FirstOrDefault(system => system.Id == nextId)?.Name ?? "Unknown system"
                        : "No next stop";
                    var designName = ShipDesignRegistry.TryGet(fleet.DesignId, out var design)
                        ? design!.Name
                        : "Legacy vessel";
                    var artworkPath = design is null
                        ? ShipArtworkLibrary.PathForRole(fleet.Role)
                        : ShipArtworkLibrary.PathForDesign(design.Id);
                    return new UiOwnedFleetSnapshot(fleet.Id, fleet.Role, fleet.Name, location, activity,
                        fleet.DesignId ?? "warp_scout", designName, artworkPath, fleet.StrategicSpeed, fleet.MaximumLegRangeLightYears,
                        fleet.FuelRemainingLightYears, fleet.FuelCapacityLightYears,
                        fleet.CargoMaterials, fleet.CargoMaterialCapacity, FreightSimulation.GetCargoTransferRatePerDay(fleet),
                        route.RemainingLegs, route.DistanceLightYears,
                        EconomySimulation.GetFleetOperatingCost(fleet.Role), combatStatus.IsArmed,
                        combatStatus.DurabilityRatio, combatStatus.CurrentOrder.ToString(),
                        fleet.CurrentSystemId, fleet.DestinationSystemId, fleet.DestinationPlanetaryBodyId,
                        destination, nextStop, fleet.HoldRequested, fleet.ReturnToBaseRequested, fleet.ReturnToBaseFailureReason,
                        fleet.SettlementBodyId, fleet.EmbarkedPopulationMillions,
                        Game.Simulation.Combat.FleetCombatPower.OwnPower(fleet));
                }).ToArray();
        }
    }

    public string UiExplorationMissionDetails
    {
        get
        {
            if (_galaxy is null)
                return "Exploration missions are initializing…";

            var view = _explorationReadModel.Build(_galaxy, _galaxy.PlayerCivilizationId);
            if (view.ActiveMissions.Count == 0)
                return "No active scout, science, or colony missions.";

            var builder = new StringBuilder();
            builder.Append("Active missions: ").AppendLine(view.ActiveMissions.Count.ToString());

            foreach (var mission in view.ActiveMissions.Take(4))
            {
                builder.Append("• ").Append(mission.FleetName)
                    .Append(" — ").AppendLine(FormatMissionPhase(mission.Status.Phase));
                builder.Append("  ").AppendLine(mission.Status.Summary);
            }

            if (view.ActiveMissions.Count > 4)
                builder.Append("• +").Append(view.ActiveMissions.Count - 4).Append(" more active missions");

            return builder.ToString().TrimEnd();
        }
    }

    public UiExplorationMissionSnapshot[] UiExplorationMissions
    {
        get
        {
            if (_galaxy is null) return System.Array.Empty<UiExplorationMissionSnapshot>();
            var view = _explorationReadModel.Build(_galaxy, _galaxy.PlayerCivilizationId);
            return view.ActiveMissions.Take(8).Select(mission =>
            {
                var targetId = mission.DestinationSystemId ?? mission.CurrentSystemId;
                var destination = targetId is int id
                    ? _galaxy.Systems.FirstOrDefault(system => system.Id == id)?.Name ?? "Deep space"
                    : "Awaiting destination";
                var eta = mission.Status.EstimatedMissionDaysRemaining is double days
                    ? $"{days:0.0} days remaining"
                    : mission.Status.Phase == ExplorationMissionPhase.AwaitingOrder ? "Ready for orders" : "ETA unavailable";
                return new UiExplorationMissionSnapshot(mission.FleetId, mission.Role, mission.FleetName,
                    FormatMissionPhase(mission.Status.Phase), destination, eta, mission.Status.Summary);
            }).ToArray();
        }
    }

    private static string FormatMissionPhase(ExplorationMissionPhase phase) => phase switch
    {
        ExplorationMissionPhase.AwaitingOrder => "Awaiting order",
        ExplorationMissionPhase.Traveling => "Traveling",
        ExplorationMissionPhase.ReconnaissanceReady => "Scouting",
        ExplorationMissionPhase.ScienceSurveying => "Science survey",
        ExplorationMissionPhase.ColonySettlementReady => "Establishing colony",
        _ => phase.ToString(),
    };
}
