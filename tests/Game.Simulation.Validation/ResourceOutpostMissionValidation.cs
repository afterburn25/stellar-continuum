using Game.Simulation.Colonization;
using Game.Simulation.Economy;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;

namespace Game.Simulation.Validation;

internal static class ResourceOutpostMissionValidation
{
    public static void ValidateDedicatedVesselAndFoundingFlow()
    {
        var galaxy = new GalaxyGenerator().Generate(0x4F55_5450_4F53_544DL, new GalaxyGenerationSettings
        {
            SystemCount = 72,
            PreWarpCivilizationCount = 4,
            AncientCivilizationCount = 1,
            Radius = 600.0f,
        });
        var player = galaxy.Civilizations.First(candidate => candidate.Id == galaxy.PlayerCivilizationId);
        foreach (var system in galaxy.Systems)
            galaxy.Knowledge.MarkSystemFullySurveyed(player.Id, system.Id);
        var home = galaxy.Systems.First(system => system.Id == player.HomeSystemId);
        var design = ShipDesignRegistry.Get(ShipDesignRegistry.ResourceOutpostShipId);
        Require(design.PopulationCostMillions == 8.0 && design.CreditCost == 130.0,
            "dedicated outpost vessel did not preserve its specialist crew and construction cost");

        var vessel = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 5000 : galaxy.Fleets.Max(fleet => fleet.Id) + 5000,
            CivilizationId = player.Id,
            Name = "Prospector Validation",
            Role = FleetRole.Colony,
            DesignId = design.Id,
            Position = home.Position,
            CurrentSystemId = home.Id,
            StrategicSpeed = design.StrategicSpeed,
            MaximumLegRangeLightYears = 10000.0,
            FuelCapacityLightYears = 10000.0,
            FuelRemainingLightYears = 10000.0,
            SensorRange = design.SensorRange,
            IsActive = true,
            EmbarkedPopulationMillions = design.PopulationCostMillions,
            EmbarkedPopulationSpeciesId = player.SpeciesId,
        };
        galaxy.Fleets.Add(vessel);
        var simulation = new ColonizationSimulation(new AlwaysSupportedReach());
        var economy = galaxy.Economies.First(state => state.CivilizationId == player.Id);
        economy.Credits = 0.0;
        var plan = simulation.GetResourceOutpostOpportunityPlan(galaxy, vessel.Id, ResourceOutpostOpportunityPlanner.HardMaximumCandidates);
        Require(plan.Candidates.Any(candidate => candidate.HasRareResource && !candidate.CanOrder &&
                candidate.Reason.Contains("$900M UED", StringComparison.Ordinal)),
            "outpost planner advertised an expedition that the treasury could not fund");
        economy.Credits = 500.0;
        plan = simulation.GetResourceOutpostOpportunityPlan(galaxy, vessel.Id, ResourceOutpostOpportunityPlanner.HardMaximumCandidates);
        var target = plan.Candidates.FirstOrDefault(candidate => candidate.CanOrder)
            ?? throw new InvalidOperationException("validation galaxy did not produce an orderable harsh rare-resource world");
        Require(target.HasRareResource && target.IsTooHarshForColony,
            "outpost planner admitted a world that was not both valuable and too harsh for colonization");
        var plannedBody = galaxy.PlanetaryBodies.Single(body => body.Id == target.PlanetaryBodyId);
        var plannedDeposit = ResourceDepositProfile.ForBody(plannedBody);
        Require(target.DepositMaterialName == plannedDeposit.MaterialName &&
                target.DepositGrade == plannedDeposit.Grade &&
                target.DepositAccessibility == plannedDeposit.Accessibility &&
                target.ExtractionYieldMultiplier == plannedDeposit.ExtractionYieldMultiplier &&
                target.InitialDepositMaterials == ResourceOutpostOperations.InitialDepositReserve(plannedBody),
            "outpost planning did not expose the surveyed deposit economics before authorization");
        Require(!simulation.GetOpportunityPlan(galaxy, vessel.Id).CanReceiveOrders,
            "normal colonization planner treated an outpost vessel as a colony ship");

        var creditsBefore = economy.Credits;
        var order = simulation.IssueResourceOutpostFleetOrder(galaxy, vessel.Id, target.SystemId, target.PlanetaryBodyId);
        Require(order.Accepted, "valid resource-outpost order was rejected: " + order.Message);
        Require(Math.Abs(economy.Credits - (creditsBefore - ColonizationSimulation.ResourceOutpostExpeditionCreditCost)) < 0.000001,
            "resource-outpost expedition did not charge its authorization cost exactly once");
        Require(vessel.DestinationSystemId == target.SystemId && vessel.DestinationPlanetaryBodyId == target.PlanetaryBodyId,
            "resource-outpost order lost its exact destination body");

        var destination = galaxy.Systems.First(system => system.Id == target.SystemId);
        vessel.Position = destination.Position;
        vessel.CurrentSystemId = destination.Id;
        FleetRouteOrders.Clear(vessel);
        Require(simulation.Advance(galaxy).Count == 0, "arrival completed colony construction instantly");
        var events = simulation.Advance(galaxy, ColonizationSimulation.ColonyEstablishmentDays);
        var outpost = galaxy.Colonies.Single(colony => colony.SystemId == target.SystemId && colony.Kind == SettlementKind.ResourceOutpost);
        Require(Math.Abs(outpost.PopulationMillions - design.PopulationCostMillions) < 0.000001,
            "founded outpost did not transfer the vessel's specialist personnel");
        var targetBody = galaxy.PlanetaryBodies.Single(body => body.Id == target.PlanetaryBodyId);
        Require(outpost.RemainingExtractableMaterials == ResourceOutpostOperations.InitialDepositReserve(targetBody),
            "founded outpost did not record its finite body-scaled deposit reserve");
        Require(!vessel.IsActive && vessel.EmbarkedPopulationMillions == 0.0,
            "resource-outpost vessel was not consumed by founding");
        Require(events.Count == 1 && events[0].ColonyId == outpost.Id,
            "resource-outpost founding did not emit its settlement event");
    }

    private sealed class AlwaysSupportedReach : IInterstellarOperationalReachView
    {
        public MissionReachAssessment Assess(GalaxyState galaxy, int civilizationId, FleetState fleet,
            int targetSystemId, InterstellarMissionKind missionKind) =>
            MissionReachAssessment.Supported("OUTPOST VALIDATION REACH");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
