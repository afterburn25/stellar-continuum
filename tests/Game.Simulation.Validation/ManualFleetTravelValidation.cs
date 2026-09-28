using Game.Persistence;
using Game.Simulation.Colonization;
using Game.Simulation.Economy;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class ManualFleetTravelValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Validate()
    {
        var galaxy = new GalaxyGenerator().Generate(12735, new GalaxyGenerationSettings
        { SystemCount = 64, PreWarpCivilizationCount = 5, AncientCivilizationCount = 1, Radius = 560 });
        var player = galaxy.PlayerCivilizationId;
        var origin = galaxy.Systems.Single(s => s.Id == galaxy.Civilizations.Single(c => c.Id == player).HomeSystemId);
        var target = galaxy.Systems.Where(s => s.Id != origin.Id).OrderBy(s => System.Numerics.Vector2.Distance(s.Position, origin.Position)).First();
        foreach (var system in galaxy.Systems) galaxy.Knowledge.MarkSystemFullySurveyed(player, system.Id);
        var exploration = new ExplorationSimulation();
        var colonization = new ColonizationSimulation();
        var id = galaxy.Fleets.Select(f => f.Id).DefaultIfEmpty(0).Max() + 100;
        foreach (var role in new[] { FleetRole.Scout, FleetRole.Science, FleetRole.Colony, FleetRole.Logistics })
        {
            var fleet = new FleetState { Id = id++, CivilizationId = player, Name = "Manual travel " + role,
                Role = role, Position = origin.Position, CurrentSystemId = origin.Id,
                MaximumLegRangeLightYears = 10000, FuelCapacityLightYears = 10000, FuelRemainingLightYears = 10000 };
            galaxy.Fleets.Add(fleet);
            var accepted = role switch
            {
                FleetRole.Colony => colonization.IssueTransitOrder(galaxy, player, fleet.Id, target.Id).Accepted,
                FleetRole.Logistics => new FreightSimulation().IssueTransitOrder(galaxy, player, fleet.Id, target.Id).Accepted,
                _ => exploration.IssueTravelOrder(galaxy, fleet.Id, target.Id).Accepted,
            };
            Require(accepted && fleet.Position == origin.Position && fleet.DestinationSystemId == target.Id,
                $"{role}: fully surveyed destination blocked travel or order teleported the vessel");
        }
        var colony = galaxy.Fleets.Single(f => f.Name == "Manual travel Colony");
        Require(colony.PreventAutomaticSettlement && colony.SettlementBodyId is null, "manual travel silently authorized settlement");
        var directory = Path.Combine(Path.GetTempPath(), "stellar-manual-travel-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try
        {
            var path = Path.Combine(directory, "campaign.json");
            var saves = new CampaignSaveService(); saves.Save(path, galaxy, 0);
            var restored = saves.Load(path).Galaxy;
            var traveler = restored.Fleets.Single(f => f.Id == colony.Id);
            Require(traveler.PreventAutomaticSettlement && traveler.DestinationSystemId == target.Id,
                "save/load lost the explicit travel-only colony order");
            var fuel = traveler.FuelRemainingLightYears;
            exploration.Advance(restored, 0);
            Require(traveler.Position == origin.Position && traveler.FuelRemainingLightYears == fuel, "paused travel consumed distance or fuel");
            exploration.Advance(restored, FleetLocalTransit.GateRadius / FleetLocalTransit.Rate(traveler) + .01);
            Require(traveler.TransitPhase == FleetTransitPhase.InterstellarWarp && traveler.DestinationSystemId is not null && traveler.FuelRemainingLightYears < fuel,
                "travel did not complete the timed local departure before beginning its fuelled lane");
            exploration.Advance(restored, 10000);
            Require(traveler.CurrentSystemId == target.Id && traveler.IsActive, "travel did not reach its ordered destination");
            var count = restored.Colonies.Count;
            colonization.Advance(restored, 10000);
            Require(restored.Colonies.Count == count && traveler.IsActive && traveler.SettlementBodyId is null,
                "a travel-only colony ship began settlement without a planet order");
        }
        finally { Directory.Delete(directory, true); }
    }

    private static void Require(bool condition, string message)
    { if (!condition) throw new InvalidOperationException(message); }
}
