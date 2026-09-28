using System;
using System.Linq;
using System.Numerics;
using System.Runtime.CompilerServices;
using Game.Simulation.Combat;
using Game.Simulation.Diplomacy;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.CoreRuntime.Validation;

internal static class DiplomacyCampaignRuntimeCoordinatorValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunDiplomacyCampaignRuntimeCoordinatorChecks()
    {
        ValidatePlainCSharpCampaignDiplomacyComposition();
        Console.WriteLine("PASS: plain-C# campaign Diplomacy runtime composition");
    }

    private static void ValidatePlainCSharpCampaignDiplomacyComposition()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x4450_4C4F_5255_4E54L,
            new GalaxyGenerationSettings
            {
                SystemCount = 28,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 0,
                Radius = 360.0f,
            });

        var civilizations = galaxy.Civilizations
            .Where(civilization => !civilization.IsSeededAncient)
            .OrderBy(civilization => civilization.Id)
            .Take(3)
            .ToArray();
        Require(civilizations.Length == 3,
            "campaign Diplomacy runtime validation requires three ordinary civilizations");

        var first = civilizations[0];
        var second = civilizations[1];
        var third = civilizations[2];
        var state = new DiplomacyState();
        var runtime = new DiplomacyCampaignRuntimeCoordinator(
            state,
            new DiplomacyCampaignMaintenancePolicy(
                ReviewIntervalTicks: DiplomacyCampaignClock.TicksForWholeDays(1),
                ContactStaleAfterTicks: DiplomacyCampaignClock.TicksForWholeDays(2),
                ProposalLifetimeTicks: DiplomacyCampaignClock.TicksForWholeDays(1)));
        runtime.Reset(simulationDays: 0.0, reviewImmediately: false);

        var firstContactEvents = new[]
        {
            new ExplorationEvent(
                ExplorationEventType.FirstContact,
                first.Id,
                FleetId: 8001,
                first.HomeSystemId,
                "First observed the second civilization.",
                second.Id),
            new ExplorationEvent(
                ExplorationEventType.FirstContact,
                second.Id,
                FleetId: 8002,
                second.HomeSystemId,
                "Second observed the first civilization.",
                first.Id),
            new ExplorationEvent(
                ExplorationEventType.FirstContact,
                first.Id,
                FleetId: 8003,
                first.HomeSystemId,
                "First observed a third civilization without reciprocal contact.",
                third.Id),
        };

        var contactStep = runtime.Process(
            firstContactEvents,
            Array.Empty<CombatEvent>(),
            simulationDays: 1.0);
        Require(contactStep.FirstContactEventsProcessed == 3,
            "campaign Diplomacy runtime did not route all first-contact events");
        Require(contactStep.CombatIncidentsProcessed == 0,
            "campaign Diplomacy runtime invented Combat consequences without Combat events");
        Require(contactStep.Maintenance.Ran,
            "campaign Diplomacy runtime did not run due low-frequency maintenance");
        Require(HasIdentified(runtime, first.Id, second.Id) && HasIdentified(runtime, second.Id, first.Id),
            "campaign Diplomacy runtime lost mutual legitimate identification");
        Require(HasIdentified(runtime, first.Id, third.Id) && !HasIdentified(runtime, third.Id, first.Id),
            "campaign Diplomacy runtime manufactured reciprocal third-party contact");

        var communicationTick = DiplomacyCampaignClock.FromSimulationDays(1.001);
        var communication = runtime.Commands.EstablishCommunication(
            first.Id,
            second.Id,
            communicationTick);
        Require(communication.Accepted,
            "campaign runtime observer command surface could not establish mutual communication");
        var diplomacy = new DiplomacySimulation(state);
        diplomacy.DeclareWar(first.Id, second.Id, communicationTick + 1);

        var ceasefire = runtime.Commands.SendProposal(
            first.Id,
            second.Id,
            DiplomaticProposalKind.CeasefireOffer,
            communicationTick + 2,
            "Ceasefire offer should expire if unanswered.");
        Require(ceasefire.Accepted && ceasefire.ProposalId.HasValue,
            "observer-safe command surface was not bound to the campaign Diplomacy state");
        Require(runtime.BuildView(first.Id).Proposals.Any(proposal => proposal.ProposalId == ceasefire.ProposalId),
            "campaign runtime read surface did not expose the observer's own proposal");
        Require(runtime.HostilityView.AreHostile(first.Id, second.Id),
            "campaign runtime hostility view did not reflect authoritative war state");

        galaxy.Fleets.Clear();
        var combatSystem = galaxy.Systems[0];
        var attacker = CreatePatrol(9101, first.Id, "Runtime Attacker", combatSystem.Id, combatSystem.Position);
        var target = CreatePatrol(9102, second.Id, "Runtime Target", combatSystem.Id, combatSystem.Position);
        galaxy.Fleets.Add(attacker);
        galaxy.Fleets.Add(target);

        var combat = runtime.CreateCombatSimulation();
        var order = combat.IssueOrder(
            galaxy,
            first.Id,
            attacker.Id,
            new MilitaryOrder(MilitaryOrderType.Attack, target.Id));
        Require(order.Accepted,
            $"campaign runtime Combat binding rejected authoritative hostility: {order.Message}");

        var combatEvents = combat.Advance(galaxy, simulationDeltaDays: 0.1);
        Require(combatEvents.Any(evt => evt.Type == CombatEventType.EngagementStarted),
            "validation Combat did not emit an engagement event");
        var combatStep = runtime.Process(
            Array.Empty<ExplorationEvent>(),
            combatEvents,
            simulationDays: 1.25);
        Require(combatStep.CombatIncidentsProcessed >= 1,
            "campaign Diplomacy runtime did not route attributable Combat incidents");
        Require(runtime.HostilityView.AreHostile(first.Id, second.Id),
            "Combat incident processing disconnected the shared hostility view");

        var maintenanceStep = runtime.Process(
            Array.Empty<ExplorationEvent>(),
            Array.Empty<CombatEvent>(),
            simulationDays: 3.0);
        Require(maintenanceStep.Maintenance.Ran,
            "campaign Diplomacy runtime skipped overdue maintenance review");
        Require(maintenanceStep.Maintenance.ContactAging.NewlyStaleContacts >= 1,
            "observation-only contact did not age through campaign runtime maintenance");
        Require(maintenanceStep.Maintenance.ProposalLifecycle.NewlyExpiredProposals >= 1,
            "pending proposal did not expire through campaign runtime maintenance");

        var firstView = runtime.BuildView(first.Id);
        var thirdContact = firstView.Contacts.Single(contact => contact.TargetCivilizationId == third.Id);
        Require(thirdContact.Condition == ContactCondition.StaleOrLost && !thirdContact.CommunicationAvailable,
            "campaign runtime maintenance did not preserve stale-contact semantics");
        var expired = firstView.Proposals.Single(proposal => proposal.ProposalId == ceasefire.ProposalId);
        Require(expired.Status == DiplomaticProposalStatus.Expired,
            "campaign runtime maintenance did not preserve proposal expiration semantics");
        Require(firstView.Contacts.Single(contact => contact.TargetCivilizationId == second.Id).CommunicationAvailable,
            "campaign runtime incorrectly aged an active diplomatic communication channel");

        var regressedTimeRejected = false;
        try
        {
            runtime.Process(
                Array.Empty<ExplorationEvent>(),
                Array.Empty<CombatEvent>(),
                simulationDays: 2.5);
        }
        catch (InvalidOperationException)
        {
            regressedTimeRejected = true;
        }
        Require(regressedTimeRejected,
            "campaign Diplomacy runtime accepted regressing campaign chronology");
    }

    private static bool HasIdentified(
        DiplomacyCampaignRuntimeCoordinator runtime,
        int observerCivilizationId,
        int targetCivilizationId) =>
        runtime.BuildView(observerCivilizationId).Contacts.Any(contact =>
            contact.TargetCivilizationId == targetCivilizationId &&
            contact.Awareness >= ContactAwareness.Identified);

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

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
