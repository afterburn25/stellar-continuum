using Game.Persistence;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Economy;
using System.Numerics;
using System.Text.Json.Nodes;

namespace Game.Simulation.Validation;

internal static class InterstellarTravelValidation
{
    public static void ValidateLaneRoutingAndPersistence()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x4C41_4E45_2026L,
            new GalaxyGenerationSettings
            {
                SystemCount = 64,
                PreWarpCivilizationCount = 5,
                AncientCivilizationCount = 1,
                Radius = 560.0f,
            });
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var origin = galaxy.Systems.First(system => system.Id == player.HomeSystemId);
        foreach (var system in galaxy.Systems)
            galaxy.Knowledge.RevealSystem(player.Id, system.Id);
        var network = new InterstellarLaneNetwork();
        var routedTarget = galaxy.Systems
            .Select(system => new
            {
                System = system,
                Route = network.FindShortestRoute(galaxy.Systems, origin.Id, system.Id, 360.0),
            })
            .Where(candidate => candidate.Route.Count >= 3)
            .OrderByDescending(candidate => candidate.Route.Count)
            .ThenBy(candidate => candidate.System.Id)
            .FirstOrDefault()
            ?? throw new InvalidOperationException("validation galaxy did not provide a multi-leg route");

        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 1000 : galaxy.Fleets.Max(existing => existing.Id) + 1000,
            CivilizationId = player.Id,
            Name = "Lane Persistence Validation Scout",
            Role = FleetRole.Scout,
            Position = origin.Position,
            CurrentSystemId = origin.Id,
            StrategicSpeed = 22.0,
            MaximumLegRangeLightYears = 360.0,
            FuelCapacityLightYears = 10000.0,
            FuelRemainingLightYears = 10000.0,
            SensorRange = 135.0f,
            IsActive = true,
        };
        galaxy.Fleets.Add(fleet);

        var reach = new LaneInterstellarOperationalReachView().Assess(
            galaxy,
            player.Id,
            fleet,
            routedTarget.System.Id,
            InterstellarMissionKind.ScoutReconnaissance);
        Require(reach.IsSupported && reach.IsAuthoritative, "multi-leg route was not authoritatively supported");
        Require(reach.Reason.Contains("km", StringComparison.Ordinal) && reach.Reason.Contains("ly", StringComparison.Ordinal) &&
                !reach.Reason.Contains("e+", StringComparison.Ordinal),
            "route confirmation did not lead with a readable metric distance");
        Require(reach.RouteSystemIds!.SequenceEqual(routedTarget.Route), "reach assessment returned a different deterministic route");
        FleetRouteOrders.Assign(galaxy, fleet, routedTarget.System.Id, reach);
        Require(fleet.DestinationSystemId == routedTarget.System.Id, "route replaced the final mission destination with a waypoint");
        Require(fleet.PlannedRouteSystemIds.Count == routedTarget.Route.Count - 1, "route did not retain every remaining waypoint");

        var economy = galaxy.Economies.First(state => state.CivilizationId == player.Id);
        economy.LastBaseOperationsFundingFraction = 0.0;
        var heldPosition = fleet.Position;
        var heldFuel = fleet.FuelRemainingLightYears;
        new ExplorationSimulation().Advance(galaxy, 0.1);
        Require(fleet.Position == heldPosition && fleet.FuelRemainingLightYears == heldFuel,
            "unfunded fleet continued moving or consuming fuel");
        var suspended = new ExplorationMissionStatusEvaluator().Build(galaxy, fleet);
        Require(suspended.Summary.Contains("operations are unfunded", StringComparison.Ordinal),
            "unfunded mission did not expose an actionable suspension reason");

        economy.LastBaseOperationsFundingFraction = 1.0;
        new ExplorationSimulation().Advance(galaxy, FleetLocalTransit.GateRadius / FleetLocalTransit.Rate(fleet) + 0.1);
        Require(fleet.CurrentSystemId is null && fleet.TransitPhase == FleetTransitPhase.InterstellarWarp,
            "elapsed local departure plus a small lane step did not leave the fleet between systems");
        Require(fleet.DestinationSystemId == routedTarget.System.Id, "mid-flight travel lost the final mission target");

        var directory = Path.Combine(Path.GetTempPath(), "stellar-lane-validation-" + Guid.NewGuid().ToString("N"));
        try
        {
            var path = Path.Combine(directory, "campaign.json");
            var saves = new CampaignSaveService();
            saves.Save(path, galaxy, 12.5);
            var restored = saves.Load(path).Galaxy.Fleets.Single(candidate => candidate.Id == fleet.Id);
            Require(restored.DestinationSystemId == fleet.DestinationSystemId, "save/load changed the final route destination");
            Require(restored.PlannedRouteSystemIds.SequenceEqual(fleet.PlannedRouteSystemIds), "save/load changed remaining lane waypoints");
            Require(Math.Abs(restored.MaximumLegRangeLightYears - 360.0) < 0.000001, "save/load changed maximum leg range");
            Require(restored.FuelRemainingLightYears < restored.FuelCapacityLightYears,
                "travel did not consume persisted fuel endurance");

            var completedGalaxy = saves.Load(path).Galaxy;
            new ExplorationSimulation().Advance(completedGalaxy, 1000.0);
            var completed = completedGalaxy.Fleets.Single(candidate => candidate.Id == fleet.Id);
            Require(completed.CurrentSystemId == routedTarget.System.Id && completed.DestinationSystemId is null,
                "fleet did not finish its full persisted route at the final target");
            Require(completed.PlannedRouteSystemIds.Count == 0, "completed route retained stale waypoints");

            var returnReach = new LaneInterstellarOperationalReachView().Assess(
                completedGalaxy, player.Id, completed, origin.Id, InterstellarMissionKind.ScoutReconnaissance);
            Require(returnReach.IsSupported, "fleet could not plot a fueled return route to its home colony");
            FleetRouteOrders.Assign(completedGalaxy, completed, origin.Id, returnReach);
            new ExplorationSimulation().Advance(completedGalaxy, 1000.0);
            Require(completed.CurrentSystemId == origin.Id &&
                    Math.Abs(completed.FuelRemainingLightYears - completed.FuelCapacityLightYears) < 0.000001,
                "arrival at an owned colony did not refill fleet endurance");
        }
        finally
        {
            if (Directory.Exists(directory))
                Directory.Delete(directory, recursive: true);
        }

        var shortRangeFleet = new FleetState
        {
            Id = fleet.Id + 1,
            CivilizationId = player.Id,
            Name = "Short Range Validation Vessel",
            Role = FleetRole.Science,
            Position = origin.Position,
            CurrentSystemId = origin.Id,
            MaximumLegRangeLightYears = 0.01,
            IsActive = true,
        };
        var blocked = new LaneInterstellarOperationalReachView().Assess(
            galaxy,
            player.Id,
            shortRangeFleet,
            routedTarget.System.Id,
            InterstellarMissionKind.ScienceSurvey);
        Require(!blocked.IsSupported && blocked.Reason.Contains("maximum leg range", StringComparison.OrdinalIgnoreCase),
            "unreachable route did not return a useful leg-range rejection");
        Require(blocked.Reason.Contains("km", StringComparison.Ordinal) && blocked.Reason.Contains("ly", StringComparison.Ordinal),
            "leg-range rejection did not lead with a metric distance");

        var lowFuelFleet = new FleetState
        {
            Id = fleet.Id + 2,
            CivilizationId = player.Id,
            Name = "Low Fuel Validation Vessel",
            Role = FleetRole.Science,
            Position = origin.Position,
            CurrentSystemId = origin.Id,
            MaximumLegRangeLightYears = 10000.0,
            FuelCapacityLightYears = 0.01,
            FuelRemainingLightYears = 0.01,
            IsActive = true,
        };
        var fuelBlocked = new LaneInterstellarOperationalReachView().Assess(
            galaxy, player.Id, lowFuelFleet, routedTarget.System.Id, InterstellarMissionKind.ScienceSurvey);
        Require(!fuelBlocked.IsSupported && fuelBlocked.Reason.Contains("fuel endurance", StringComparison.OrdinalIgnoreCase),
            "fuel-limited route did not return a useful endurance rejection");
        Require(fuelBlocked.Reason.Contains("km", StringComparison.Ordinal) && fuelBlocked.Reason.Contains("ly", StringComparison.Ordinal),
            "fuel-endurance rejection did not lead with a metric distance");
    }

    public static void ValidatePhysicalDepthDistanceAndPersistence()
    {
        var origin = new StarSystemState(701, "Depth Origin", Vector2.Zero, StarArchetype.Standard,
            false, false, false, false, GalacticDepthLightYears: 0.0, StellarCatalogId: "hyg-v41:701");
        var target = new StarSystemState(702, "Depth Target", new Vector2(3, 4), StarArchetype.Standard,
            false, false, false, false, GalacticDepthLightYears: 12.0, StellarCatalogId: "hyg-v41:702");
        Require(Math.Abs(InterstellarDistance.Between(origin, target) - 13.0) < 0.0000001,
            "physical 3-4-12 system distance was not 13 light-years");

        var flatTarget = target with { GalacticDepthLightYears = null };
        Require(Math.Abs(InterstellarDistance.Between(origin with { GalacticDepthLightYears = null }, flatTarget) - 5.0) < 0.0000001,
            "legacy systems without depth did not retain their flat distance");
        var lanes = new InterstellarLaneNetwork().Build(new[] { origin, target });
        Require(lanes.Count == 1 && Math.Abs(lanes[0].LengthLightYears - 13.0) < 0.0000001,
            "lane length did not use physical galactic depth");

        var source = new GalaxyGenerator().Generate(0x445054485F4C59L, new GalaxyGenerationSettings
        {
            SystemCount = 8, PreWarpCivilizationCount = 2, AncientCivilizationCount = 0, Radius = 60,
        });
        var writableSystems = (IList<StarSystemState>)source.Systems;
        origin = origin with { Id = source.Systems[0].Id };
        target = target with { Id = source.Systems[1].Id };
        writableSystems[0] = origin;
        writableSystems[1] = target;
        var galaxy = source;
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var legacyOrigin = new StarSystemState(source.Systems[2].Id, "Legacy Origin", new Vector2(11, 17),
            StarArchetype.Standard, false, false, false, false);
        var legacyWaypoint = new StarSystemState(source.Systems[3].Id, "Legacy Waypoint", new Vector2(14, 21),
            StarArchetype.Standard, false, false, false, false);
        var legacyDestination = new StarSystemState(source.Systems[4].Id, "Legacy Destination", new Vector2(18, 27),
            StarArchetype.Standard, false, false, false, false);
        writableSystems[2] = legacyOrigin;
        writableSystems[3] = legacyWaypoint;
        writableSystems[4] = legacyDestination;
        var legacyFleet = new FleetState
        {
            Id = 9700, CivilizationId = player.Id, Name = "Legacy Route Metric Vessel", Role = FleetRole.Scout,
            Position = legacyOrigin.Position, CurrentSystemId = legacyOrigin.Id,
            DestinationSystemId = legacyDestination.Id,
            PlannedRouteSystemIds = new List<int> { legacyWaypoint.Id, legacyDestination.Id },
        };
        var expectedLegacyDistance = (double)Vector2.Distance(legacyOrigin.Position, legacyWaypoint.Position) +
            Vector2.Distance(legacyWaypoint.Position, legacyDestination.Position);
        Require(FleetRouteMetrics.Measure(galaxy, legacyFleet).DistanceLightYears == expectedLegacyDistance,
            "legacy multi-leg route measurement did not advance from each prior waypoint");
        Require(InterstellarDistance.Between(legacyOrigin, legacyWaypoint) == Vector2.Distance(legacyOrigin.Position, legacyWaypoint.Position) &&
                InterstellarDistance.SquaredBetween(legacyOrigin, legacyWaypoint) == Vector2.DistanceSquared(legacyOrigin.Position, legacyWaypoint.Position),
            "depthless distance helper did not retain exact Vector2 compatibility");
        var fleet = new FleetState
        {
            Id = 9701, CivilizationId = player.Id, Name = "Depth Validation Vessel", Role = FleetRole.Scout,
            Position = origin.Position, CurrentSystemId = origin.Id, StrategicSpeed = 1.0,
            MaximumLegRangeLightYears = 13.0, FuelCapacityLightYears = 13.0, FuelRemainingLightYears = 13.0,
            SensorRange = 12.5f, IsActive = true,
        };
        galaxy.Fleets.Add(fleet);
        var reach = new LaneInterstellarOperationalReachView().Assess(galaxy, player.Id, fleet, target.Id,
            InterstellarMissionKind.ScoutReconnaissance);
        Require(reach.IsSupported && Math.Abs(reach.RouteDistanceLightYears - 13.0) < 0.0000001,
            "range and fuel route assessment did not use 13-light-year depth distance");
        Require(galaxy.Knowledge.RevealWithinSensorRange(-500, origin.Id, galaxy.Systems, fleet.SensorRange) == 1,
            "sensor range incorrectly revealed a system outside the physical 3D radius");

        FleetRouteOrders.Assign(galaxy, fleet, target.Id, reach);
        fleet.CurrentSystemId = null;
        fleet.TransitPhase = FleetTransitPhase.InterstellarWarp;
        fleet.TransitOriginSystemId = origin.Id;
        fleet.TransitTargetSystemId = target.Id;
        fleet.TransitProgress = 0.0;
        var etaBefore = new ExplorationMissionStatusEvaluator().Build(galaxy, fleet).EstimatedTransitDaysRemaining;
        Require(Math.Abs(FleetRouteMetrics.Measure(galaxy, fleet).DistanceLightYears - 13.0) < 0.000001,
            "remaining route metric did not start from the physical 13-light-year lane length");
        new ExplorationSimulation().Advance(galaxy, 6.5);
        Require(Math.Abs(fleet.TransitProgress - .5) < 0.000001 &&
                Vector2.Distance(fleet.Position, new Vector2(1.5f, 2.0f)) < .000001f &&
                Math.Abs(fleet.FuelRemainingLightYears - 6.5) < 0.000001,
            "3D transit did not consume physical distance while interpolating the 2D chart position by progress");
        var etaAfter = new ExplorationMissionStatusEvaluator().Build(galaxy, fleet).EstimatedTransitDaysRemaining;
        Require(Math.Abs(InterstellarDistance.FromFleet(galaxy, fleet, target) - 6.5) < 0.000001,
            "opportunity distances lost physical depth halfway through a warp leg");
        Require(etaBefore is double before && etaAfter is double after &&
                Math.Abs(before - after - 6.5) < 0.000001 &&
                Math.Abs(FleetRouteMetrics.Measure(galaxy, fleet).DistanceLightYears - 6.5) < 0.000001,
            "3D transit ETA did not track physical remaining distance");

        var zeroProjectionOrigin = origin with
        {
            Id = source.Systems[5].Id,
            Name = "Zero Projection Origin",
            Position = new Vector2(80, 80),
            GalacticDepthLightYears = 0.0,
        };
        var zeroProjectionTarget = target with
        {
            Id = source.Systems[6].Id,
            Name = "Zero Projection Target",
            Position = new Vector2(80, 80),
            GalacticDepthLightYears = 10.0,
        };
        writableSystems[5] = zeroProjectionOrigin;
        writableSystems[6] = zeroProjectionTarget;
        var zeroProjectionFleet = new FleetState
        {
            Id = 9702, CivilizationId = player.Id, Name = "Zero Projection Depth Vessel", Role = FleetRole.Scout,
            Position = zeroProjectionOrigin.Position, CurrentSystemId = null, DestinationSystemId = zeroProjectionTarget.Id,
            PlannedRouteSystemIds = new List<int> { zeroProjectionTarget.Id }, TransitPhase = FleetTransitPhase.InterstellarWarp,
            TransitOriginSystemId = zeroProjectionOrigin.Id, TransitTargetSystemId = zeroProjectionTarget.Id,
            StrategicSpeed = 1.0, MaximumLegRangeLightYears = 10.0,
            FuelCapacityLightYears = 10.0, FuelRemainingLightYears = 10.0, IsActive = true,
        };
        galaxy.Fleets.Add(zeroProjectionFleet);
        new ExplorationSimulation().Advance(galaxy, 5.0);
        Require(zeroProjectionFleet.TransitPhase == FleetTransitPhase.InterstellarWarp &&
                Math.Abs(zeroProjectionFleet.TransitProgress - .5) < 0.000001 &&
                zeroProjectionFleet.Position == zeroProjectionOrigin.Position &&
                Math.Abs(zeroProjectionFleet.FuelRemainingLightYears - 5.0) < 0.000001,
            "same-chart-position systems with depth separation did not advance by physical distance");

        var directory = Path.Combine(Path.GetTempPath(), "stellar-depth-validation-" + Guid.NewGuid().ToString("N"));
        try
        {
            var path = Path.Combine(directory, "depth.json");
            var saves = new CampaignSaveService();
            saves.Save(path, galaxy, 6.5);
            var restored = saves.Load(path).Galaxy.Systems.OrderBy(system => system.Id).ToArray();
            Require(restored[0].GalacticDepthLightYears == 0.0 && restored[1].GalacticDepthLightYears == 12.0 &&
                    restored[1].StellarCatalogId == "hyg-v41:702",
                "save/load did not round-trip galactic depth and catalog identity");

            var oldProgressJson = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
            var oldProgressFleet = oldProgressJson["Galaxy"]!["Fleets"]!.AsArray()
                .Single(node => node!["Id"]!.GetValue<int>() == zeroProjectionFleet.Id)!.AsObject();
            oldProgressFleet.Remove("TransitProgress");
            var oldProgressPath = Path.Combine(directory, "missing-transit-progress.json");
            File.WriteAllText(oldProgressPath, oldProgressJson.ToJsonString());
            var restoredOldProgress = saves.Load(oldProgressPath).Galaxy.Fleets.Single(candidate => candidate.Id == zeroProjectionFleet.Id);
            Require(restoredOldProgress.TransitProgress == 0.0,
                "missing legacy transit progress did not default to the physical leg origin");
            var oldProgressGalaxy = saves.Load(oldProgressPath).Galaxy;
            var oldProgressFleetState = oldProgressGalaxy.Fleets.Single(candidate => candidate.Id == zeroProjectionFleet.Id);
            new ExplorationSimulation().Advance(oldProgressGalaxy, 5.0);
            Require(oldProgressFleetState.TransitPhase == FleetTransitPhase.InterstellarWarp &&
                    Math.Abs(oldProgressFleetState.TransitProgress - .5) < 0.000001 &&
                    oldProgressFleetState.Position == zeroProjectionOrigin.Position,
                "missing legacy transit progress collapsed a zero-projection physical lane");

            var json = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
            foreach (var system in json["Galaxy"]!["Systems"]!.AsArray())
                system!.AsObject().Remove("GalacticDepthLightYears");
            var legacyPath = Path.Combine(directory, "flat-legacy.json");
            File.WriteAllText(legacyPath, json.ToJsonString());
            var legacy = saves.Load(legacyPath).Galaxy.Systems.OrderBy(system => system.Id).ToArray();
            Require(legacy.All(system => system.GalacticDepthLightYears is null) &&
                    Math.Abs(InterstellarDistance.Between(legacy[0], legacy[1]) - 5.0) < 0.0000001,
                "legacy save without depth did not retain flat behavior");
        }
        finally
        {
            if (Directory.Exists(directory)) Directory.Delete(directory, recursive: true);
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
