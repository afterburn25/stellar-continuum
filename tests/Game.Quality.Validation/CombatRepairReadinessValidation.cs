using System;
using System.Linq;
using System.Numerics;
using System.Runtime.CompilerServices;
using Game.Simulation.Combat;
using Game.Simulation.Economy;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Quality.Validation;

internal static class CombatRepairReadinessValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidateBoundedRepairFacilityReadiness();
        Console.WriteLine("PASS: logistics repair readiness respects represented facilities");
    }

    private static void ValidateBoundedRepairFacilityReadiness()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x5245_5041_4952_4C47L,
            new GalaxyGenerationSettings
            {
                SystemCount = 32,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 0,
                Radius = 380.0f,
            });

        var civilization = galaxy.Civilizations.First(candidate => !candidate.IsPlayer);
        var foreign = galaxy.Civilizations.First(candidate => candidate.Id != civilization.Id);
        var home = galaxy.Systems.First(system => system.Id == civilization.HomeSystemId);
        var remote = galaxy.Systems.First(system => system.Id != civilization.HomeSystemId);
        var construction = galaxy.ConstructionStates.First(state => state.CivilizationId == civilization.Id);
        construction.CompletedProjectIds.Add("orbital_shipyard");

        foreach (var existing in galaxy.Fleets.Where(fleet => fleet.CivilizationId == civilization.Id))
            existing.IsActive = false;

        var nextId = galaxy.Fleets.Count == 0 ? 50000 : galaxy.Fleets.Max(fleet => fleet.Id) + 50000;
        var homeDamaged = CreateDamagedPatrol(nextId++, civilization.Id, home.Id, home.Position, "Repair-Ready Home Patrol");
        var remoteDamaged = CreateDamagedPatrol(nextId++, civilization.Id, remote.Id, remote.Position, "Remote Damaged Patrol");
        var transitDamaged = CreateDamagedPatrol(nextId++, civilization.Id, null, Vector2.Lerp(home.Position, remote.Position, 0.5f), "Transit Damaged Patrol");
        var healthy = CreateHealthyPatrol(nextId++, civilization.Id, home.Id, home.Position, "Healthy Home Patrol");
        var foreignDamaged = CreateDamagedPatrol(nextId++, foreign.Id, home.Id, home.Position, "Foreign Damaged Patrol");
        galaxy.Fleets.Add(homeDamaged);
        galaxy.Fleets.Add(remoteDamaged);
        galaxy.Fleets.Add(transitDamaged);
        galaxy.Fleets.Add(healthy);
        galaxy.Fleets.Add(foreignDamaged);

        var economy = galaxy.Economies.First(state => state.CivilizationId == civilization.Id);
        var creditsBefore = economy.Credits;
        var industryBefore = economy.Industry;
        var scienceBefore = economy.Science;
        var fleetSnapshots = galaxy.Fleets.Select(CaptureFleet).ToArray();

        ICombatRepairReadinessView view = new PrototypeCombatRepairReadinessView();
        var first = view.Build(galaxy, civilization.Id);
        var second = view.Build(galaxy, civilization.Id);
        var combatDemand = CombatRepairDemandCalculator.Build(galaxy, civilization.Id);

        Require(first == second || Equivalent(first, second), "identical repair-readiness inputs produced different outputs");
        Require(first.HasRepresentedHomeShipyard && first.HomeShipyardNodeId is not null,
            "completed Orbital Shipyard did not create represented home repair readiness");
        Require(first.DamagedFleetCount == 3, $"expected 3 damaged owned fleets, got {first.DamagedFleetCount}");
        Require(first.Fleets.All(entry => entry.RepairDemand.CivilizationId == civilization.Id),
            "repair-readiness view leaked foreign fleet state");
        Require(first.Fleets.All(entry => entry.RepairDemand.FleetId != healthy.Id),
            "pristine owned fleet appeared as repair demand");
        Require(first.Fleets.All(entry => entry.RepairDemand.FleetId != foreignDamaged.Id),
            "foreign damaged fleet appeared in exact-own repair readiness");

        var homeReadiness = first.Fleets.Single(entry => entry.RepairDemand.FleetId == homeDamaged.Id);
        var remoteReadiness = first.Fleets.Single(entry => entry.RepairDemand.FleetId == remoteDamaged.Id);
        var transitReadiness = first.Fleets.Single(entry => entry.RepairDemand.FleetId == transitDamaged.Id);

        Require(homeReadiness.CanReceiveRepresentedRepairs &&
                homeReadiness.FacilityStatus == FleetRepairFacilityStatus.ReadyAtRepresentedHomeShipyard &&
                homeReadiness.RepairNodeId == first.HomeShipyardNodeId,
            "damaged home-system fleet was not mapped to the represented shipyard");
        Require(!remoteReadiness.CanReceiveRepresentedRepairs &&
                remoteReadiness.FacilityStatus == FleetRepairFacilityStatus.OutsideRepresentedRepairNetwork &&
                remoteReadiness.RepairNodeId is null,
            "remote damaged fleet was incorrectly credited with a home-system repair path");
        Require(!transitReadiness.CanReceiveRepresentedRepairs &&
                transitReadiness.FacilityStatus == FleetRepairFacilityStatus.InTransitOrDeepSpace,
            "in-transit damaged fleet was incorrectly repair-ready");
        Require(first.ReadyFleetCount == 1 && first.UnreadyFleetCount == 2,
            "repair-ready/unready fleet counts changed");
        RequireNear(first.TotalRepairDeficit, combatDemand.TotalMissingDurability,
            "logistics readiness changed Combat's exact repair deficit");
        RequireNear(first.ReadyRepairDeficit, homeReadiness.RepairDemand.TotalMissingDurability,
            "ready repair deficit did not equal the sole facility-ready fleet demand");
        RequireNear(first.UnreadyRepairDeficit,
            remoteReadiness.RepairDemand.TotalMissingDurability + transitReadiness.RepairDemand.TotalMissingDurability,
            "unready repair deficit changed");

        construction.CompletedProjectIds.Remove("orbital_shipyard");
        var withoutShipyard = view.Build(galaxy, civilization.Id);
        var homeWithoutShipyard = withoutShipyard.Fleets.Single(entry => entry.RepairDemand.FleetId == homeDamaged.Id);
        Require(!withoutShipyard.HasRepresentedHomeShipyard && withoutShipyard.HomeShipyardNodeId is null,
            "removed shipyard still appeared as represented repair infrastructure");
        Require(!homeWithoutShipyard.CanReceiveRepresentedRepairs &&
                homeWithoutShipyard.FacilityStatus == FleetRepairFacilityStatus.NoRepresentedHomeShipyard,
            "home-system damage remained repair-ready after represented shipyard removal");
        Require(withoutShipyard.ReadyFleetCount == 0,
            "repair readiness remained after all represented repair facilities were removed");

        Require(economy.Credits == creditsBefore && economy.Industry == industryBefore && economy.Science == scienceBefore,
            "repair-readiness view mutated economy resources");
        var fleetSnapshotsAfter = galaxy.Fleets.Select(CaptureFleet).ToArray();
        Require(fleetSnapshots.SequenceEqual(fleetSnapshotsAfter),
            "repair-readiness view mutated fleet or Combat state");
    }

    private static FleetReadSnapshot CaptureFleet(FleetState fleet) => new(
        fleet.Id,
        fleet.IsActive,
        fleet.CurrentSystemId,
        fleet.Combat?.ProfileId,
        fleet.Combat?.Shields,
        fleet.Combat?.Armor,
        fleet.Combat?.Hull,
        fleet.Combat?.Order,
        fleet.Combat?.IsDisengaged);

    private static FleetState CreateDamagedPatrol(
        int id,
        int civilizationId,
        int? systemId,
        Vector2 position,
        string name)
    {
        var combat = CombatProfileRegistry.CreateInitialState(CombatProfileIds.PatrolCorvetteMk1, FleetRole.Military);
        combat.Shields = Math.Max(0.0, combat.Shields - 10.0);
        combat.Armor = Math.Max(0.0, combat.Armor - 12.0);
        combat.Hull = Math.Max(1.0, combat.Hull - 18.0);
        return new FleetState
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
            Combat = combat,
        };
    }

    private static FleetState CreateHealthyPatrol(
        int id,
        int civilizationId,
        int systemId,
        Vector2 position,
        string name) => new()
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
        Combat = CombatProfileRegistry.CreateInitialState(CombatProfileIds.PatrolCorvetteMk1, FleetRole.Military),
    };

    private static bool Equivalent(CivilizationCombatRepairReadiness left, CivilizationCombatRepairReadiness right) =>
        left.CivilizationId == right.CivilizationId &&
        left.HomeSystemId == right.HomeSystemId &&
        left.HasRepresentedHomeShipyard == right.HasRepresentedHomeShipyard &&
        left.HomeShipyardNodeId == right.HomeShipyardNodeId &&
        left.Fleets.SequenceEqual(right.Fleets);

    private static void RequireNear(double actual, double expected, string message, double tolerance = 0.000001)
    {
        if (Math.Abs(actual - expected) > tolerance)
            throw new InvalidOperationException($"{message}: expected {expected:0.######}, got {actual:0.######}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }

    private sealed record FleetReadSnapshot(
        int FleetId,
        bool IsActive,
        int? CurrentSystemId,
        string? CombatProfileId,
        double? Shields,
        double? Armor,
        double? Hull,
        MilitaryOrderType? Order,
        bool? IsDisengaged);
}
