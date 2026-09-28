using Game.Simulation.Colonization;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Validation;

internal static class LegacyColonyOrderAvailabilityValidation
{
    public static void ValidateLegacyOrderSkipsCommittedFleet()
    {
        var fixture = CreateFixture(requiredTargets: 2);
        var committed = AddColonyFleet(fixture.Galaxy, fixture.Player, "Committed Lower-ID Colony Ship");
        var idle = AddColonyFleet(fixture.Galaxy, fixture.Player, "Idle Higher-ID Colony Ship");
        var firstTarget = fixture.Targets[0];
        var secondTarget = fixture.Targets[1];

        committed.DestinationSystemId = firstTarget.SystemId;
        committed.DestinationPlanetaryBodyId = firstTarget.Id;
        var originalSystem = committed.DestinationSystemId;
        var originalBody = committed.DestinationPlanetaryBodyId;

        var result = new ColonizationSimulation().IssuePlayerColonyOrder(
            fixture.Galaxy,
            fixture.Player.Id,
            secondTarget.SystemId,
            secondTarget.Id);

        Require(result.Accepted,
            "legacy civilization-scoped order could not use an idle populated colony fleet");
        Require(committed.DestinationSystemId == originalSystem && committed.DestinationPlanetaryBodyId == originalBody,
            "legacy civilization-scoped order hijacked an already committed colony fleet");
        Require(idle.DestinationSystemId == secondTarget.SystemId && idle.DestinationPlanetaryBodyId == secondTarget.Id,
            "legacy civilization-scoped order did not select the idle colony fleet for the new mission");
    }

    public static void ValidateLegacyOrderRejectsWhenOnlyCommittedFleetExists()
    {
        var fixture = CreateFixture(requiredTargets: 2);
        var committed = AddColonyFleet(fixture.Galaxy, fixture.Player, "Only Committed Colony Ship");
        var firstTarget = fixture.Targets[0];
        var secondTarget = fixture.Targets[1];
        committed.DestinationSystemId = firstTarget.SystemId;
        committed.DestinationPlanetaryBodyId = firstTarget.Id;

        var result = new ColonizationSimulation().IssuePlayerColonyOrder(
            fixture.Galaxy,
            fixture.Player.Id,
            secondTarget.SystemId,
            secondTarget.Id);

        Require(!result.Accepted,
            "legacy civilization-scoped order retargeted the only already committed colony fleet");
        Require(result.Message.Contains("No colony ship", StringComparison.OrdinalIgnoreCase),
            "legacy committed-fleet rejection did not report fleet unavailability");
        Require(committed.DestinationSystemId == firstTarget.SystemId && committed.DestinationPlanetaryBodyId == firstTarget.Id,
            "rejected legacy order mutated the existing committed mission");
    }

    public static void ValidateSettlementReadyAndLegacyArrivalsAreNotStolen()
    {
        var fixture = CreateFixture(requiredTargets: 2);
        var settlementReady = AddColonyFleet(fixture.Galaxy, fixture.Player, "Settlement-Ready Lower-ID Colony Ship");
        var idle = AddColonyFleet(fixture.Galaxy, fixture.Player, "Idle Colony Ship For New Mission");
        var arrivalBody = fixture.Targets[0];
        var nextBody = fixture.Targets[1];
        var arrivalSystem = fixture.Galaxy.Systems.First(system => system.Id == arrivalBody.SystemId);

        settlementReady.Position = arrivalSystem.Position;
        settlementReady.CurrentSystemId = arrivalSystem.Id;
        settlementReady.DestinationSystemId = null;
        settlementReady.DestinationPlanetaryBodyId = arrivalBody.Id;

        var result = new ColonizationSimulation().IssuePlayerColonyOrder(
            fixture.Galaxy,
            fixture.Player.Id,
            nextBody.SystemId,
            nextBody.Id);

        Require(result.Accepted,
            "legacy order could not use idle fleet while another fleet was settlement-ready");
        Require(settlementReady.CurrentSystemId == arrivalSystem.Id &&
                settlementReady.DestinationSystemId is null &&
                settlementReady.DestinationPlanetaryBodyId == arrivalBody.Id,
            "legacy order stole or retargeted a settlement-ready colony fleet");
        Require(idle.DestinationSystemId == nextBody.SystemId && idle.DestinationPlanetaryBodyId == nextBody.Id,
            "legacy order did not assign the separate idle fleet when settlement-ready fleet existed");

        // Simulate a migrated/body-less v7 arrival at an uncolonized target. With no other idle
        // fleet available, the compatibility command must reject instead of stealing the arrival.
        idle.IsActive = false;
        settlementReady.DestinationPlanetaryBodyId = null;
        var migratedArrivalResult = new ColonizationSimulation().IssuePlayerColonyOrder(
            fixture.Galaxy,
            fixture.Player.Id,
            nextBody.SystemId,
            nextBody.Id);

        Require(!migratedArrivalResult.Accepted,
            "legacy civilization-scoped order stole a body-less arrived colony mission from an uncolonized system");
        Require(settlementReady.CurrentSystemId == arrivalSystem.Id && settlementReady.DestinationSystemId is null,
            "rejected legacy order mutated the body-less arrived colony fleet");
    }

    public static void ValidateExplicitFleetOrderRemainsRetargetable()
    {
        var fixture = CreateFixture(requiredTargets: 2);
        var fleet = AddColonyFleet(fixture.Galaxy, fixture.Player, "Explicit Retarget Colony Ship");
        var firstTarget = fixture.Targets[0];
        var secondTarget = fixture.Targets[1];
        var simulation = new ColonizationSimulation();

        var first = simulation.IssueColonyFleetOrder(
            fixture.Galaxy,
            fleet.Id,
            firstTarget.SystemId,
            firstTarget.Id);
        Require(first.Accepted,
            "explicit fleet-ID colony order could not establish initial mission");

        var retarget = simulation.IssueColonyFleetOrder(
            fixture.Galaxy,
            fleet.Id,
            secondTarget.SystemId,
            secondTarget.Id);
        Require(retarget.Accepted,
            "legacy availability hardening incorrectly disabled explicit fleet-ID retargeting");
        Require(fleet.DestinationSystemId == secondTarget.SystemId && fleet.DestinationPlanetaryBodyId == secondTarget.Id,
            "explicit fleet-ID retargeting did not update the selected colony mission");
    }

    private static ValidationFixture CreateFixture(int requiredTargets)
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x4C45_4741_4359_434FL,
            new GalaxyGenerationSettings
            {
                SystemCount = 72,
                PreWarpCivilizationCount = 6,
                AncientCivilizationCount = 1,
                Radius = 620.0f,
            });
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);

        foreach (var fleet in galaxy.Fleets.Where(fleet => fleet.Role == FleetRole.Colony))
            fleet.IsActive = false;

        var occupiedSystems = galaxy.Colonies.Select(colony => colony.SystemId).ToHashSet();
        var evaluator = new SpeciesPlanetaryHabitabilityEvaluator();
        var targets = galaxy.PlanetaryBodies
            .Where(body => !occupiedSystems.Contains(body.SystemId))
            .Where(body => evaluator.Evaluate(body, player.SpeciesId).CanFoundCurrentColony)
            .GroupBy(body => body.SystemId)
            .OrderBy(group => group.Key)
            .Select(group => group.OrderBy(body => body.Id).First())
            .Take(requiredTargets)
            .ToArray();

        Require(targets.Length == requiredTargets,
            $"validation galaxy did not provide {requiredTargets} viable unoccupied colony target systems");
        foreach (var target in targets)
            galaxy.Knowledge.MarkSystemFullySurveyed(player.Id, target.SystemId);

        return new ValidationFixture(galaxy, player, targets);
    }

    private static FleetState AddColonyFleet(
        GalaxyState galaxy,
        CivilizationState civilization,
        string name)
    {
        var home = galaxy.Systems.First(system => system.Id == civilization.HomeSystemId);
        Require(galaxy.Colonies.Any(colony =>
                colony.CivilizationId == civilization.Id && colony.SystemId == home.Id),
            "validation civilization does not have a founded home colony");

        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 160000 : galaxy.Fleets.Max(existing => existing.Id) + 160000,
            CivilizationId = civilization.Id,
            Name = name,
            Role = FleetRole.Colony,
            Position = home.Position,
            CurrentSystemId = home.Id,
            StrategicSpeed = 12.0,
            SensorRange = 95.0f,
            IsActive = true,
            EmbarkedPopulationMillions = 120.0,
            EmbarkedPopulationSpeciesId = civilization.SpeciesId,
        };
        galaxy.Fleets.Add(fleet);
        return fleet;
    }

    private sealed record ValidationFixture(
        GalaxyState Galaxy,
        CivilizationState Player,
        PlanetaryBodyState[] Targets);

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
