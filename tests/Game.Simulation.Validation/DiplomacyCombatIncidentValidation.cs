using System.Numerics;
using System.Runtime.CompilerServices;
using Game.Simulation.Combat;
using Game.Simulation.Diplomacy;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class DiplomacyCombatIncidentValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunDiplomacyCombatIncidentChecks()
    {
        ValidateAttributableCombatCreatesBoundedDiplomaticConsequences();
        ValidateUnattributedCombatCannotLeakIdentity();
        Console.WriteLine("PASS: diplomacy combat-incident attribution and consequences");
    }

    private static void ValidateAttributableCombatCreatesBoundedDiplomaticConsequences()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x4449_504C_494E_4349L,
            new GalaxyGenerationSettings
            {
                SystemCount = 24,
                PreWarpCivilizationCount = 3,
                AncientCivilizationCount = 0,
                Radius = 320.0f,
            });

        galaxy.Fleets.Clear();
        var attackerCivilization = galaxy.Civilizations[0];
        var victimCivilization = galaxy.Civilizations[1];
        var system = galaxy.Systems[0];
        var attacker = CreatePatrol(9100, attackerCivilization.Id, "Incident Attacker", system.Id, system.Position);
        var victim = CreatePatrol(9101, victimCivilization.Id, "Incident Victim", system.Id, system.Position);
        galaxy.Fleets.Add(attacker);
        galaxy.Fleets.Add(victim);

        var diplomacyState = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(diplomacyState);
        EstablishIdentifiedContact(diplomacy, attackerCivilization.Id, victimCivilization.Id, "incident-a-to-b", tick: 1, system.Id);
        EstablishIdentifiedContact(diplomacy, victimCivilization.Id, attackerCivilization.Id, "incident-b-to-a", tick: 1, system.Id);
        diplomacy.SetHostile(attackerCivilization.Id, victimCivilization.Id, tick: 2, reason: "Escalating armed confrontation.");

        // Use the accepted Combat-owned read-only hostility adapter from integration.
        var combat = new CombatSimulation(new DiplomacyCombatHostilityView(diplomacyState));
        var bridge = new CombatDiplomacyBridge(diplomacyState);

        var victimCombat = CombatProfileRegistry.EnsureState(victim);
        victimCombat.Shields = 0.0;
        victimCombat.Armor = 0.0;
        victimCombat.Hull = 1.0;

        var order = combat.IssueOrder(
            galaxy,
            attackerCivilization.Id,
            attacker.Id,
            new MilitaryOrder(MilitaryOrderType.Attack, victim.Id));
        Require(order.Accepted, $"hostile Combat order was rejected: {order.Message}");

        var combatEvents = combat.Advance(galaxy, 1.0);
        Require(combatEvents.Any(evt => evt.Type == CombatEventType.EngagementStarted),
            "attributable hostile encounter did not emit EngagementStarted");
        Require(combatEvents.Any(evt =>
                evt.Type == CombatEventType.FleetDestroyed &&
                evt.ActorCivilizationId == attackerCivilization.Id &&
                evt.TargetCivilizationId == victimCivilization.Id),
            "validation engagement did not emit attributable FleetDestroyed");

        var processed = bridge.Process(combatEvents, tick: 3);
        Require(processed >= 2,
            "diplomacy did not consume the meaningful engagement/destruction incidents");

        var relationship = diplomacyState.GetRelationship(attackerCivilization.Id, victimCivilization.Id)
            ?? throw new InvalidOperationException("combat consequence lost the diplomatic relationship");
        Require(relationship.PoliticalState == DiplomaticPoliticalState.Hostile,
            "Combat incident consequence independently changed the political war/peace state");
        Require(relationship.Grievances.Any(grievance =>
                grievance.SourceCivilizationId == attackerCivilization.Id &&
                Math.Abs(grievance.Severity - 1.0) < 0.000001 &&
                grievance.Reason.Contains("destroyed", StringComparison.OrdinalIgnoreCase)),
            "attributable fleet destruction did not become a major victim grievance");

        // Damage packets are intentionally transient and should not create one diplomatic
        // history entry per salvo in a long war.
        var victimView = diplomacyState.BuildViewFor(victimCivilization.Id);
        Require(!victimView.RecentEvents.Any(evt =>
                evt.Summary.Contains("DamageApplied", StringComparison.OrdinalIgnoreCase)),
            "per-salvo Combat detail leaked into bounded diplomatic history");
    }

    private static void ValidateUnattributedCombatCannotLeakIdentity()
    {
        var diplomacyState = new DiplomacyState();
        var bridge = new CombatDiplomacyBridge(diplomacyState);
        var combatEvents = new[]
        {
            new CombatEvent(
                CombatEventType.FleetDestroyed,
                SystemId: 44,
                ActorCivilizationId: 91,
                ActorFleetId: 9001,
                TargetCivilizationId: 92,
                TargetFleetId: 9002,
                ShieldDamage: 0.0,
                ArmorDamage: 0.0,
                HullDamage: 1.0,
                Message: "Unknown attacker destroyed an unprepared vessel."),
        };

        Require(bridge.Process(combatEvents, tick: 10) == 0,
            "unattributed authoritative Combat identity was converted into diplomatic knowledge");
        var victimView = diplomacyState.BuildViewFor(92);
        Require(victimView.Contacts.Count == 0 && victimView.Relationships.Count == 0,
            "Combat incident leaked hidden attacker identity into the victim's diplomatic view");
    }

    private static void EstablishIdentifiedContact(
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
            ContactAwareness.ContactEstablished,
            ContactCondition.Active,
            CommunicationAvailable: false,
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
