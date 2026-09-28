using System.Numerics;
using Game.Persistence;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class FleetLocalTransitValidation
{
    [Game.Validation.RegressionCheck]
    internal static void ValidatePersistedGateCrossingsAndPartitioning()
    {
        var first = CreateScenario();
        var fleet = first.Fleet;
        var simulation = new ExplorationSimulation();
        var initialPosition = fleet.Position;
        simulation.Advance(first.Galaxy, 0);
        Require(fleet.Position == initialPosition && fleet.TransitPhase == FleetTransitPhase.None,
            "zero delta changed an unstarted local transit");

        simulation.Advance(first.Galaxy, .2);
        Require(fleet.TransitPhase == FleetTransitPhase.LocalDeparture && fleet.Position == initialPosition,
            "local departure changed the strategic system coordinate");
        Require(fleet.LocalTransitPosition.Length() > 0 && fleet.LocalTransitPosition.Length() < FleetLocalTransit.GateRadius,
            "local departure did not advance through normalized chart space");
        Require(Vector2.Dot(Vector2.Normalize(fleet.LocalTransitTarget),
                Vector2.Normalize(first.Next.Position - first.Origin.Position)) > .999f,
            "outbound gate did not follow the next lane direction");

        var oneStep = CreateScenario();
        var splitStep = CreateScenario();
        new ExplorationSimulation().Advance(oneStep.Galaxy, .4);
        var splitSimulation = new ExplorationSimulation();
        for (var i = 0; i < 4; i++) splitSimulation.Advance(splitStep.Galaxy, .1);
        var oneEta = new ExplorationMissionStatusEvaluator().Build(oneStep.Galaxy, oneStep.Fleet).EstimatedTransitDaysRemaining;
        var splitEta = new ExplorationMissionStatusEvaluator().Build(splitStep.Galaxy, splitStep.Fleet).EstimatedTransitDaysRemaining;
        Require(oneStep.Fleet.TransitPhase == splitStep.Fleet.TransitPhase &&
                Math.Abs(oneStep.Fleet.TransitProgress - splitStep.Fleet.TransitProgress) < .000001 &&
                Vector2.Distance(oneStep.Fleet.LocalTransitPosition, splitStep.Fleet.LocalTransitPosition) < .000001f &&
                Math.Abs(oneStep.Fleet.FuelRemainingLightYears - splitStep.Fleet.FuelRemainingLightYears) < .000001 &&
                Math.Abs(oneEta!.Value - splitEta!.Value) < .000001,
            "partial local transit changed phase, position, fuel, progress, or ETA when partitioned");

        FleetRouteOrders.Clear(first.Fleet);
        Require(first.Fleet.TransitPhase == FleetTransitPhase.LocalArrival && first.Fleet.LocalTransitPosition.Length() > 0,
            "clearing a local route teleported the vessel instead of requiring its final approach");
        simulation.Advance(first.Galaxy, 10);
        Require(first.Fleet.TransitPhase == FleetTransitPhase.None && first.Fleet.LocalTransitPosition == Vector2.Zero,
            "cleared route never completed its local final approach");

        var savedScenario = CreateScenario();
        new ExplorationSimulation().Advance(savedScenario.Galaxy, .2);
        var directory = Path.Combine(Path.GetTempPath(), "stellar-local-transit-" + Guid.NewGuid().ToString("N"));
        try
        {
            var savePath = Path.Combine(directory, "campaign.json");
            new CampaignSaveService().Save(savePath, savedScenario.Galaxy, 1);
            var restored = new CampaignSaveService().Load(savePath).Galaxy;
            var restoredFleet = restored.Fleets.Single(candidate => candidate.Id == savedScenario.Fleet.Id);
            Require(restoredFleet.TransitPhase == FleetTransitPhase.LocalDeparture &&
                    Vector2.Distance(restoredFleet.LocalTransitPosition, savedScenario.Fleet.LocalTransitPosition) < .000001f,
                "save/load lost a partial local crossing");

            File.WriteAllText(savePath, File.ReadAllText(savePath).Replace("\"TransitPhase\": 1", "\"TransitPhase\": 99"));
            try
            {
                _ = new CampaignSaveService().Load(savePath);
                throw new InvalidOperationException("malformed transit phase was accepted");
            }
            catch (InvalidDataException exception)
            {
                Require(exception.Message.Contains($"Fleet {savedScenario.Fleet.Id}", StringComparison.Ordinal),
                    "malformed transit diagnostic did not identify its fleet");
            }
            new CampaignSaveService().Save(savePath, savedScenario.Galaxy, 1);

            CivilianFleetHoldOrders.Hold(restored, restoredFleet.CivilizationId, restoredFleet.Id);
            var held = restoredFleet.LocalTransitPosition;
            new ExplorationSimulation().Advance(restored, 3);
            Require(restoredFleet.LocalTransitPosition == held && restoredFleet.FuelRemainingLightYears == fleet.FuelRemainingLightYears,
                "a local hold changed chart position or light-year fuel");
            CivilianFleetHoldOrders.Resume(restored, restoredFleet.CivilizationId, restoredFleet.Id);
            new ExplorationSimulation().Advance(restored, 10_000);
            Require(restoredFleet.CurrentSystemId == savedScenario.Final.Id && restoredFleet.DestinationSystemId is null &&
                    restoredFleet.TransitPhase == FleetTransitPhase.None && restoredFleet.LocalTransitPosition == Vector2.Zero,
                "route did not complete the final inbound-gate-to-mission-centre crossing");
        }
        finally
        {
            if (Directory.Exists(directory)) Directory.Delete(directory, true);
        }

        var large = CreateScenario();
        new ExplorationSimulation().Advance(large.Galaxy, 10_000);
        var partitioned = CreateScenario();
        var partitionedSimulation = new ExplorationSimulation();
        for (var i = 0; i < 100; i++) partitionedSimulation.Advance(partitioned.Galaxy, 100);
        Require(large.Fleet.CurrentSystemId == partitioned.Fleet.CurrentSystemId &&
                large.Fleet.DestinationSystemId == partitioned.Fleet.DestinationSystemId &&
                large.Fleet.TransitPhase == partitioned.Fleet.TransitPhase &&
                Vector2.Distance(large.Fleet.LocalTransitPosition, partitioned.Fleet.LocalTransitPosition) < .000001f,
            "local and warp time budgets changed outcome when partitioned");

        var returnScenario = CreateScenario();
        var returnSimulation = new ExplorationSimulation();
        returnSimulation.Advance(returnScenario.Galaxy, FleetLocalTransit.GateRadius / FleetLocalTransit.Rate(returnScenario.Fleet) + .01);
        Require(returnScenario.Fleet.TransitPhase == FleetTransitPhase.InterstellarWarp && returnScenario.Fleet.CurrentSystemId is null,
            "validation vessel did not enter warp before its queued return");
        Require(CivilianFleetReturnOrders.RequestReturn(returnScenario.Galaxy, returnScenario.Fleet.CivilizationId, returnScenario.Fleet.Id).Accepted,
            "return request was rejected during warp");
        returnSimulation.Advance(returnScenario.Galaxy, 10_000);
        Require(returnScenario.Fleet.CurrentSystemId == returnScenario.Next.Id &&
                returnScenario.Fleet.DestinationSystemId == returnScenario.Origin.Id &&
                returnScenario.Fleet.TransitPhase == FleetTransitPhase.LocalDeparture,
            "queued return did not activate at the first inbound intermediate system");

        var gateApproach = CreateScenario();
        gateApproach.Fleet.Position = gateApproach.Final.Position;
        gateApproach.Fleet.DestinationSystemId = gateApproach.Final.Id;
        gateApproach.Fleet.PlannedRouteSystemIds = new List<int> { gateApproach.Final.Id };
        gateApproach.Fleet.CurrentSystemId = null;
        gateApproach.Fleet.TransitPhase = FleetTransitPhase.InterstellarWarp;
        gateApproach.Fleet.TransitTargetSystemId = gateApproach.Final.Id;
        gateApproach.Fleet.FuelRemainingLightYears = 0;
        new ExplorationSimulation().Advance(gateApproach.Galaxy, .000001);
        Require(gateApproach.Fleet.TransitPhase == FleetTransitPhase.LocalArrival && gateApproach.Fleet.CurrentSystemId == gateApproach.Final.Id,
            "zero fuel at a strategic lane endpoint did not enter the local final approach");
        new ExplorationSimulation().Advance(gateApproach.Galaxy, 10);
        Require(gateApproach.Fleet.TransitPhase == FleetTransitPhase.None && gateApproach.Fleet.DestinationSystemId is null,
            "zero fuel prevented an already-inbound vessel completing its final local approach");
    }

    private static (GalaxyState Galaxy, FleetState Fleet, StarSystemState Origin, StarSystemState Next, StarSystemState Final) CreateScenario()
    {
        var galaxy = new GalaxyGenerator().Generate(0x10CA1_2026L, new GalaxyGenerationSettings
        { SystemCount = 48, PreWarpCivilizationCount = 5, AncientCivilizationCount = 1, Radius = 500 });
        var player = galaxy.Civilizations.Single(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var origin = galaxy.Systems.Single(system => system.Id == player.HomeSystemId);
        var network = new InterstellarLaneNetwork();
        var choice = galaxy.Systems.Select(system => (System: system, Route: network.FindShortestRoute(galaxy.Systems, origin.Id, system.Id, 360)))
            .Where(candidate => candidate.Route.Count >= 3).OrderByDescending(candidate => candidate.Route.Count).First();
        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Select(existing => existing.Id).DefaultIfEmpty(0).Max() + 1000,
            CivilizationId = player.Id, Name = "Local gate validation", Role = FleetRole.Scout,
            Position = origin.Position, CurrentSystemId = origin.Id, StrategicSpeed = 22,
            MaximumLegRangeLightYears = 360, FuelCapacityLightYears = 10000, FuelRemainingLightYears = 10000,
            SensorRange = 135, IsActive = true,
        };
        galaxy.Fleets.Add(fleet);
        var reach = new LaneInterstellarOperationalReachView().Assess(galaxy, player.Id, fleet, choice.System.Id,
            InterstellarMissionKind.ScoutReconnaissance);
        FleetRouteOrders.Assign(galaxy, fleet, choice.System.Id, reach);
        return (galaxy, fleet, origin, galaxy.Systems.Single(system => system.Id == choice.Route[1]), choice.System);
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
