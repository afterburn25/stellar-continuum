using System.Runtime.CompilerServices;
using Game.Simulation.Diplomacy;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class DiplomacyExplorationValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunDiplomacyExplorationChecks()
    {
        ValidatePhysicalEncounterFeedsDirectionalDiplomacy();
        Console.WriteLine("PASS: exploration-to-diplomacy directional first contact");
    }

    private static void ValidatePhysicalEncounterFeedsDirectionalDiplomacy()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x4449_504C_4F4D_4143L,
            new GalaxyGenerationSettings
            {
                SystemCount = 40,
                PreWarpCivilizationCount = 5,
                AncientCivilizationCount = 1,
                Radius = 480.0f,
            });

        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var foreign = galaxy.Civilizations.First(civilization => civilization.Id != player.Id);
        var neutral = galaxy.Systems.First(system =>
            system.Id != player.HomeSystemId &&
            system.Id != foreign.HomeSystemId &&
            !galaxy.Colonies.Any(colony => colony.SystemId == system.Id));
        var foreignHome = galaxy.Systems.First(system => system.Id == foreign.HomeSystemId);

        var scout = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 6000 : galaxy.Fleets.Max(fleet => fleet.Id) + 6000,
            CivilizationId = player.Id,
            Name = "Diplomacy Contact Validation Scout",
            Role = FleetRole.Scout,
            Position = neutral.Position,
            CurrentSystemId = null,
            DestinationSystemId = neutral.Id,
            StrategicSpeed = 5000.0,
            SensorRange = 5000.0f,
            IsActive = true,
        };
        galaxy.Fleets.Add(scout);

        var exploration = new ExplorationSimulation();
        var diplomacyState = new DiplomacyState();
        var bridge = new ExplorationDiplomacyBridge(new DiplomacySimulation(diplomacyState));

        // Remote sensor/catalog knowledge may reveal the foreign star, but it is not a
        // civilization-level diplomatic contact and therefore must not feed diplomacy.
        var remoteEvents = exploration.Advance(galaxy, 1.0);
        Require(bridge.Process(remoteEvents, observedAtTick: 10) == 0,
            "non-contact exploration events were incorrectly converted into diplomacy");
        Require(diplomacyState.BuildViewFor(player.Id).Contacts.Count == 0,
            "remote star detection incorrectly created a diplomatic contact");
        Require(diplomacyState.BuildViewFor(foreign.Id).Contacts.Count == 0,
            "remote detection incorrectly created reciprocal diplomacy");

        Require(exploration.IssueMoveOrder(galaxy, scout.Id, foreignHome.Id),
            "validation scout could not be ordered into the foreign system");
        var contactEvents = exploration.Advance(galaxy, 1.0);
        var firstContact = contactEvents.SingleOrDefault(evt =>
            evt.Type == ExplorationEventType.FirstContact &&
            evt.CivilizationId == player.Id &&
            evt.SystemId == foreignHome.Id)
            ?? throw new InvalidOperationException("physical encounter did not emit first contact");

        Require(firstContact.TargetCivilizationId == foreign.Id,
            "first-contact event did not carry the legitimately identified target civilization");
        Require(bridge.Process(contactEvents, observedAtTick: 20) == 1,
            "diplomacy bridge did not consume the exploration first-contact event");

        var playerView = diplomacyState.BuildViewFor(player.Id);
        Require(playerView.Contacts.Count == 1, "observer did not receive directional diplomatic contact");
        Require(playerView.Contacts[0].TargetCivilizationId == foreign.Id,
            "observer diplomacy view lost identified contact identity");
        Require(playerView.Contacts[0].Awareness == ContactAwareness.ContactEstablished,
            "physical first contact did not establish diplomatic contact state");
        Require(!playerView.Contacts[0].CommunicationAvailable,
            "physical encounter incorrectly granted automatic communication capability");
        Require(playerView.Relationships.Count == 1 &&
                playerView.Relationships[0].OtherCivilizationId == foreign.Id &&
                playerView.Relationships[0].PoliticalState == DiplomaticPoliticalState.Peace,
            "established contact did not begin the neutral bilateral political relationship");

        var foreignView = diplomacyState.BuildViewFor(foreign.Id);
        Require(foreignView.Contacts.Count == 0,
            "one-way exploration encounter incorrectly granted reciprocal diplomatic knowledge");
        Require(foreignView.Relationships.Count == 0,
            "unaware target was incorrectly shown the bilateral relationship");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
