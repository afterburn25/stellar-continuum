using System;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Runtime.CompilerServices;
using Game.Campaign;
using Game.Simulation;
using Game.Simulation.Combat;
using Game.Simulation.Diplomacy;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.CoreRuntime.Validation;

internal static class CampaignV9DiplomacyCombatBindingValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunCampaignV9DiplomacyCombatBindingChecks()
    {
        ValidateRestoredDiplomacyControlsCombatHostility();
        Console.WriteLine("PASS: restored save v9 Diplomacy controls Combat hostility");
    }

    private static void ValidateRestoredDiplomacyControlsCombatHostility()
    {
        var directory = Path.Combine(
            Path.GetTempPath(),
            "stellar-continuum-v9-combat",
            Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);

        try
        {
            var settings = new GalaxyGenerationSettings
            {
                SystemCount = 24,
                PreWarpCivilizationCount = 3,
                AncientCivilizationCount = 0,
                Radius = 320.0f,
            };
            var session = new CampaignSessionService();
            var campaign = session.CreateNew(0x5639_434F_4D42_4154L, settings);
            var civilizations = campaign.Galaxy.Civilizations
                .Where(civilization => !civilization.IsSeededAncient)
                .Take(2)
                .ToArray();
            Require(civilizations.Length == 2, "validation campaign did not contain two ordinary civilizations");

            var first = civilizations[0];
            var second = civilizations[1];
            var diplomacy = new DiplomacySimulation(campaign.Diplomacy);
            EstablishCommunication(diplomacy, first.Id, second.Id, "v9combat:first-second", tick: 20, first.HomeSystemId);
            EstablishCommunication(diplomacy, second.Id, first.Id, "v9combat:second-first", tick: 21, second.HomeSystemId);
            diplomacy.DeclareWar(first.Id, second.Id, tick: 22);

            var path = Path.Combine(directory, "campaign.json");
            session.Save(path, campaign.Galaxy, campaign.Diplomacy, simulationDays: 44.0);
            var loaded = session.LoadOrCreate(path, fallbackSeed: 1L, fallbackSettings: settings);
            Require(loaded.Source == CampaignBootstrapSource.LoadedSave, "hostile v9 campaign did not reload");
            Require(
                loaded.Diplomacy.GetRelationship(first.Id, second.Id)?.PoliticalState == DiplomaticPoliticalState.AtWar,
                "v9 load lost the authoritative war state");

            loaded.Galaxy.Fleets.Clear();
            var system = loaded.Galaxy.Systems[0];
            var attacker = CreatePatrol(9401, first.Id, "Restored Hostility Attacker", system.Id, system.Position);
            var target = CreatePatrol(9402, second.Id, "Restored Hostility Target", system.Id, system.Position);
            loaded.Galaxy.Fleets.Add(attacker);
            loaded.Galaxy.Fleets.Add(target);

            var combat = new CombatSimulation(new DiplomacyCombatHostilityView(loaded.Diplomacy));
            var coordinator = new GalaxySimulationStepCoordinator(combat: combat);
            var order = coordinator.IssueMilitaryOrder(
                loaded.Galaxy,
                first.Id,
                attacker.Id,
                new MilitaryOrder(MilitaryOrderType.Attack, target.Id));

            Require(order.Accepted, $"restored v9 war state did not authorize Combat: {order.Message}");
        }
        finally
        {
            if (Directory.Exists(directory))
                Directory.Delete(directory, recursive: true);
        }
    }

    private static void EstablishCommunication(
        DiplomacySimulation diplomacy,
        int observer,
        int target,
        string contactId,
        long tick,
        int systemId)
    {
        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            observer,
            contactId,
            target,
            tick,
            systemId,
            ContactAwareness.CommunicationAvailable,
            ContactCondition.Active,
            CommunicationAvailable: true,
            Confidence: 1.0));
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
        Combat = CombatProfileRegistry.CreateInitialState(CombatProfileIds.PatrolCorvetteMk1, FleetRole.Military),
    };

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
