using System.Numerics;
using System.Runtime.CompilerServices;
using System.Text.Json;
using Game.Simulation.Combat;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class CombatCommandBatchPreviewValidation
{
    [Game.Validation.RegressionCheck]
    internal static void ValidateBatchPreviewParityAndNonMutation()
    {
        var fixture = CreateFixture();
        var selection = new[]
        {
            fixture.Remote.Id,
            fixture.Unarmed.Id,
            fixture.SecondArmed.Id,
            fixture.FirstArmed.Id,
            fixture.FirstArmed.Id,
            fixture.Target.Id,
            999999,
        };
        var order = new MilitaryOrder(MilitaryOrderType.Attack, fixture.Target.Id);
        var before = Snapshot(fixture.Galaxy);

        var singlePreview = CreateSinglePreview(fixture);
        var batch = new CombatCommandBatchPreviewService(singlePreview)
            .Preview(fixture.Galaxy, fixture.OwnerCivilizationId, selection, order);

        Require(batch.RequestedFleetCount == 6,
            "batch preview did not de-duplicate the repeated fleet ID");
        Require(batch.AcceptedCount == 2 && batch.RejectedCount == 4,
            "batch preview mixed-selection acceptance totals are incorrect");
        Require(batch.AnyAccepted && !batch.AllAccepted,
            "batch preview aggregate acceptance flags are incorrect");
        Require(batch.FleetResults.Select(result => result.FleetId).SequenceEqual(
                batch.FleetResults.Select(result => result.FleetId).OrderBy(id => id)),
            "batch preview fleet results are not in deterministic ascending fleet-ID order");
        Require(before == Snapshot(fixture.Galaxy),
            "batch preview mutated authoritative fleet/combat state");

        foreach (var result in batch.FleetResults)
        {
            var direct = singlePreview.Preview(
                fixture.Galaxy,
                fixture.OwnerCivilizationId,
                result.FleetId,
                order);
            Require(result.Accepted == direct.Accepted && result.Message == direct.Message,
                $"batch preview result for fleet {result.FleetId} diverged from single-order preview");
        }

        var hiddenOwnershipResult = batch.FleetResults.Single(result => result.FleetId == fixture.Target.Id);
        var nonexistentResult = batch.FleetResults.Single(result => result.FleetId == 999999);
        Require(!hiddenOwnershipResult.Accepted && !nonexistentResult.Accepted &&
                hiddenOwnershipResult.Message == nonexistentResult.Message,
            "batch preview revealed foreign ownership by distinguishing a foreign fleet from an unknown fleet ID");

        var issuanceFixture = CreateFixture();
        var issuanceSelection = new[]
        {
            issuanceFixture.Remote.Id,
            issuanceFixture.Unarmed.Id,
            issuanceFixture.SecondArmed.Id,
            issuanceFixture.FirstArmed.Id,
            issuanceFixture.FirstArmed.Id,
            issuanceFixture.Target.Id,
            999999,
        };
        var issuanceOrder = new MilitaryOrder(MilitaryOrderType.Attack, issuanceFixture.Target.Id);
        var issued = new CombatCommandBatchService(CreateCombat(issuanceFixture))
            .IssueOrder(
                issuanceFixture.Galaxy,
                issuanceFixture.OwnerCivilizationId,
                issuanceSelection,
                issuanceOrder);

        Require(issued.RequestedFleetCount == batch.RequestedFleetCount &&
                issued.AcceptedCount == batch.AcceptedCount &&
                issued.RejectedCount == batch.RejectedCount,
            "batch preview aggregate acceptance counts diverged from real batch issuance");
        Require(issued.FleetResults.Count == batch.FleetResults.Count,
            "batch preview and real issuance produced different result cardinality");
        for (var index = 0; index < batch.FleetResults.Count; index++)
        {
            Require(issued.FleetResults[index].FleetId == batch.FleetResults[index].FleetId &&
                    issued.FleetResults[index].Accepted == batch.FleetResults[index].Accepted,
                $"batch preview acceptance diverged from real issuance at result index {index}");
        }

        var empty = new CombatCommandBatchPreviewService(singlePreview)
            .Preview(
                fixture.Galaxy,
                fixture.OwnerCivilizationId,
                Array.Empty<int>(),
                new MilitaryOrder(MilitaryOrderType.Hold));
        Require(empty.RequestedFleetCount == 0 &&
                empty.AcceptedCount == 0 &&
                empty.RejectedCount == 0 &&
                empty.FleetResults.Count == 0 &&
                !empty.AnyAccepted &&
                !empty.AllAccepted,
            "empty batch preview aggregate semantics are incorrect");

        Console.WriteLine("PASS: deterministic non-mutating batch military order preview");
    }

    private static CombatOrderPreviewService CreateSinglePreview(Fixture fixture) =>
        new(new DelegateCombatHostilityView((first, second) =>
            first == fixture.OwnerCivilizationId && second == fixture.TargetCivilizationId));

    private static CombatSimulation CreateCombat(Fixture fixture) =>
        new(new DelegateCombatHostilityView((first, second) =>
            first == fixture.OwnerCivilizationId && second == fixture.TargetCivilizationId));

    private static Fixture CreateFixture()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x4241_5443_4850_5256L,
            new GalaxyGenerationSettings
            {
                SystemCount = 24,
                PreWarpCivilizationCount = 3,
                AncientCivilizationCount = 0,
                Radius = 320.0f,
            });

        galaxy.Fleets.Clear();
        var localSystem = galaxy.Systems[0];
        var remoteSystem = galaxy.Systems[1];
        var owner = galaxy.Civilizations[0].Id;
        var targetCivilization = galaxy.Civilizations[1].Id;

        var firstArmed = CreateFleet(
            4101,
            owner,
            "Batch Sentinel One",
            localSystem.Id,
            localSystem.Position,
            CombatProfileIds.PatrolCorvetteMk1);
        var secondArmed = CreateFleet(
            4102,
            owner,
            "Batch Sentinel Two",
            localSystem.Id,
            localSystem.Position,
            CombatProfileIds.PatrolCorvetteMk1);
        var unarmed = CreateFleet(
            4103,
            owner,
            "Batch Utility Hull",
            localSystem.Id,
            localSystem.Position,
            CombatProfileIds.CivilianLight);
        var remote = CreateFleet(
            4104,
            owner,
            "Batch Remote Patrol",
            remoteSystem.Id,
            remoteSystem.Position,
            CombatProfileIds.PatrolCorvetteMk1);
        var target = CreateFleet(
            4900,
            targetCivilization,
            "Batch Rival",
            localSystem.Id,
            localSystem.Position,
            CombatProfileIds.PatrolCorvetteMk1);

        galaxy.Fleets.Add(firstArmed);
        galaxy.Fleets.Add(secondArmed);
        galaxy.Fleets.Add(unarmed);
        galaxy.Fleets.Add(remote);
        galaxy.Fleets.Add(target);

        return new Fixture(
            galaxy,
            owner,
            targetCivilization,
            firstArmed,
            secondArmed,
            unarmed,
            remote,
            target);
    }

    private static FleetState CreateFleet(
        int id,
        int civilizationId,
        string name,
        int systemId,
        Vector2 position,
        string combatProfileId) => new()
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
        Combat = CombatProfileRegistry.CreateInitialState(combatProfileId, FleetRole.Military),
    };

    private static string Snapshot(GalaxyState galaxy) =>
        JsonSerializer.Serialize(galaxy.Fleets
            .OrderBy(fleet => fleet.Id)
            .Select(fleet => new
            {
                fleet.Id,
                fleet.CivilizationId,
                fleet.IsActive,
                fleet.CurrentSystemId,
                fleet.DestinationSystemId,
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

    private sealed record Fixture(
        GalaxyState Galaxy,
        int OwnerCivilizationId,
        int TargetCivilizationId,
        FleetState FirstArmed,
        FleetState SecondArmed,
        FleetState Unarmed,
        FleetState Remote,
        FleetState Target);

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
