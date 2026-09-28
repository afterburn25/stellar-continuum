using System.Numerics;
using System.Runtime.CompilerServices;
using System.Text.Json;
using Game.Simulation;
using Game.Simulation.Combat;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.CoreRuntime.Validation;

internal static class CombatCommandRuntimeValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunMatchedCombatCommandRuntimeChecks()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x434F_4D4D_414E_4452L,
            new GalaxyGenerationSettings
            {
                SystemCount = 24,
                PreWarpCivilizationCount = 3,
                AncientCivilizationCount = 0,
                Radius = 320.0f,
            });

        galaxy.Fleets.Clear();
        var system = galaxy.Systems[0];
        var owner = galaxy.Civilizations[0].Id;
        var rival = galaxy.Civilizations[1].Id;
        var first = CreatePatrol(6101, owner, "Runtime Sentinel One", system.Id, system.Position);
        var second = CreatePatrol(6102, owner, "Runtime Sentinel Two", system.Id, system.Position);
        var target = CreatePatrol(6900, rival, "Runtime Rival", system.Id, system.Position);
        galaxy.Fleets.Add(first);
        galaxy.Fleets.Add(second);
        galaxy.Fleets.Add(target);

        var hostility = new MutableCountingHostilityView();
        var commandRuntime = new CombatCommandRuntime(hostility);
        var coordinator = new GalaxySimulationStepCoordinator(combatRuntime: commandRuntime);
        var attack = new MilitaryOrder(MilitaryOrderType.Attack, target.Id);
        var before = Snapshot(galaxy);

        var peacefulPreview = coordinator.PreviewMilitaryOrder(galaxy, owner, first.Id, attack);
        Require(!peacefulPreview.Accepted,
            "matched Core preview unexpectedly authorized attack while shared hostility denied it");
        Require(hostility.Calls == 1,
            "single Core preview did not query the shared hostility view exactly once");
        Require(before == Snapshot(galaxy),
            "single Core preview mutated authoritative Combat state");

        hostility.Hostile = true;
        var hostileBatchPreview = coordinator.PreviewMilitaryOrders(
            galaxy,
            owner,
            new[] { second.Id, first.Id, first.Id },
            attack);
        Require(hostileBatchPreview.RequestedFleetCount == 2 &&
                hostileBatchPreview.AcceptedCount == 2 &&
                hostileBatchPreview.RejectedCount == 0 &&
                hostileBatchPreview.AllAccepted,
            "matched Core batch preview did not reflect the live shared hostility policy");
        Require(hostility.Calls == 3,
            "batch Core preview did not route both unique fleets through the shared hostility view");
        Require(before == Snapshot(galaxy),
            "batch Core preview mutated authoritative Combat state");

        var issued = coordinator.IssueMilitaryOrder(galaxy, owner, first.Id, attack);
        Require(issued.Accepted,
            "authoritative issuance diverged from the matched hostile Core preview");
        Require(hostility.Calls == 4,
            "authoritative issuance did not query the same shared hostility view");
        Require(first.Combat?.Order == MilitaryOrderType.Attack &&
                first.Combat.TargetFleetId == target.Id,
            "authoritative issuance did not apply the previewed attack order");

        hostility.Hostile = false;
        var changedPreview = coordinator.PreviewMilitaryOrder(galaxy, owner, second.Id, attack);
        Require(!changedPreview.Accepted && hostility.Calls == 5,
            "Core preview did not observe a live hostility-policy change after prior issuance");

        var lowerTarget = CreatePatrol(6800, rival, "Runtime Priority Rival", system.Id, system.Position);
        galaxy.Fleets.Add(lowerTarget);
        hostility.Hostile = true;
        var engage = coordinator.IssueEngageHostilesOrder(galaxy, owner, second.Id);
        Require(engage.Accepted && second.Combat?.Order == MilitaryOrderType.Attack &&
                second.Combat.TargetFleetId == lowerTarget.Id,
            "Engage Hostiles did not choose the first valid co-located hostile deterministically");
        lowerTarget.IsActive = false;
        target.IsActive = false;
        var noTargetSnapshot = Snapshot(galaxy);
        var noTarget = coordinator.IssueEngageHostilesOrder(galaxy, owner, second.Id);
        Require(!noTarget.Accepted && noTarget.Message.Contains("No attackable hostile", StringComparison.Ordinal) &&
                noTargetSnapshot == Snapshot(galaxy),
            "Engage Hostiles leaked unavailable targets or mutated state on rejection");
        var deploymentTarget = galaxy.Systems[1];
        var deployment = coordinator.IssueMilitaryDeploymentOrder(galaxy, owner, second.Id, deploymentTarget.Id);
        Require(deployment.Accepted && second.DestinationSystemId == deploymentTarget.Id &&
                second.PlannedRouteSystemIds.Count > 0 && second.PlannedRouteSystemIds[^1] == deploymentTarget.Id &&
                second.Combat?.Order == MilitaryOrderType.Hold && second.Combat.TargetFleetId is null,
            "military deployment did not set destination and clear the prior tactical order");
        var deployedSnapshot = Snapshot(galaxy);
        Require(!coordinator.IssueMilitaryDeploymentOrder(galaxy, owner, second.Id, int.MaxValue).Accepted &&
                deployedSnapshot == Snapshot(galaxy),
            "invalid military deployment mutated the fleet");
        new ExplorationSimulation().Advance(galaxy, 1000);
        Require(second.CurrentSystemId == deploymentTarget.Id && second.DestinationSystemId is null &&
                second.Position == deploymentTarget.Position,
            "deployed military fleet did not arrive through authoritative strategic movement");

        // Backward compatibility: callers may still inject a standalone CombatSimulation for
        // authoritative stepping/issuance. Preview must fail closed because Core cannot inspect
        // that simulation's private hostility policy and must never silently invent a second one.
        var standalone = new GalaxySimulationStepCoordinator(
            combat: new CombatSimulation(hostility));
        RequireThrows<InvalidOperationException>(
            () => standalone.PreviewMilitaryOrder(
                galaxy,
                owner,
                second.Id,
                new MilitaryOrder(MilitaryOrderType.Hold)),
            "standalone CombatSimulation coordinator unexpectedly exposed an unmatched preview");
        var compatibleIssue = standalone.IssueMilitaryOrder(
            galaxy,
            owner,
            second.Id,
            new MilitaryOrder(MilitaryOrderType.Hold));
        Require(compatibleIssue.Accepted,
            "standalone CombatSimulation compatibility path no longer supports authoritative issuance");
        Require(!standalone.IssueEngageHostilesOrder(galaxy, owner, second.Id).Accepted,
            "standalone unmatched Combat runtime exposed automatic hostile target selection");

        Console.WriteLine("PASS: Core Combat command runtime shares live hostility across preview and issuance");
    }

    private static FleetState CreatePatrol(
        int id,
        int civilizationId,
        string name,
        int systemId,
        Vector2 position) => new()
    {
        Id = id,
        CivilizationId = civilizationId,
        Name = name,
        Role = FleetRole.Military,
        Position = position,
        CurrentSystemId = systemId,
        StrategicSpeed = 21.0,
        SensorRange = 125.0f,
        IsActive = true,
        Combat = CombatProfileRegistry.CreateInitialState(
            CombatProfileIds.PatrolCorvetteMk1,
            FleetRole.Military),
    };

    private static string Snapshot(GalaxyState galaxy) =>
        JsonSerializer.Serialize(galaxy.Fleets
            .OrderBy(fleet => fleet.Id)
            .Select(fleet => new
            {
                fleet.Id,
                fleet.IsActive,
                fleet.CurrentSystemId,
                Combat = fleet.Combat is null ? null : new
                {
                    fleet.Combat.ProfileId,
                    fleet.Combat.Shields,
                    fleet.Combat.Armor,
                    fleet.Combat.Hull,
                    fleet.Combat.WeaponCooldownRemainingDays,
                    fleet.Combat.Order,
                    fleet.Combat.TargetFleetId,
                    fleet.Combat.DefendSystemId,
                    fleet.Combat.RetreatProgressDays,
                    fleet.Combat.RetreatStarted,
                    fleet.Combat.IsDisengaged,
                    fleet.Combat.DisengagedSystemId,
                },
            }));

    private sealed class MutableCountingHostilityView : ICombatHostilityView
    {
        public bool Hostile { get; set; }
        public int Calls { get; private set; }

        public bool AreHostile(int firstCivilizationId, int secondCivilizationId)
        {
            Calls++;
            return Hostile;
        }
    }

    private static void RequireThrows<TException>(Action action, string message)
        where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException)
        {
            return;
        }

        throw new InvalidOperationException(message);
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
