using System.Numerics;
using System.Runtime.CompilerServices;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Diplomacy;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class ObservedSystemMilitaryPressureValidation
{
    [Game.Validation.RegressionCheck]
    internal static void ValidateObserverSafePressure()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x4F42_5350_5245_5353L,
            new GalaxyGenerationSettings
            {
                SystemCount = 24,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 0,
                Radius = 320.0f,
            });

        galaxy.Fleets.Clear();
        var observer = galaxy.Civilizations[0];
        var knownForeign = galaxy.Civilizations[1];
        var hiddenForeign = galaxy.Civilizations[2];
        var system = galaxy.Systems[0];

        var ownPatrol = CreatePatrol(8100, observer.Id, "Observer Patrol", system.Id, system.Position);
        var knownForeignPatrol = CreatePatrol(8200, knownForeign.Id, "Observed Rival", system.Id, system.Position);
        var hiddenForeignPatrol = CreatePatrol(8300, hiddenForeign.Id, "Hidden Heavy Presence", system.Id, system.Position);
        hiddenForeignPatrol.Combat!.Shields = CombatProfileRegistry.Get(CombatProfileIds.PatrolCorvetteMk1).MaxShields;
        hiddenForeignPatrol.Combat.Armor = CombatProfileRegistry.Get(CombatProfileIds.PatrolCorvetteMk1).MaxArmor;
        hiddenForeignPatrol.Combat.Hull = CombatProfileRegistry.Get(CombatProfileIds.PatrolCorvetteMk1).MaxHull;

        galaxy.Fleets.Add(ownPatrol);
        galaxy.Fleets.Add(knownForeignPatrol);
        galaxy.Fleets.Add(hiddenForeignPatrol);

        var contacts = new DiplomaticContactView[]
        {
            new(
                "known-rival",
                knownForeign.Id,
                ContactAwareness.ContactEstablished,
                ContactCondition.Hostile,
                CommunicationAvailable: false,
                Confidence: 0.76,
                LastObservedTick: 980,
                LastObservedSystemId: system.Id),
            new(
                "unknown-signal",
                TargetCivilizationId: null,
                ContactAwareness.DetectedUnidentified,
                ContactCondition.Active,
                CommunicationAvailable: false,
                Confidence: 0.31,
                LastObservedTick: 990,
                LastObservedSystemId: system.Id),
        };
        var relationships = new DiplomaticRelationshipView[]
        {
            new(
                knownForeign.Id,
                DiplomaticPoliticalState.AtWar,
                Trust: 0.05,
                Hostility: 0.95,
                Fear: 0.40,
                Respect: 0.25,
                Cooperation: 0.0,
                Grievances: Array.Empty<DiplomaticGrievanceSnapshot>()),
        };
        var diplomacy = new DiplomaticStateView(
            observer.Id,
            contacts,
            relationships,
            Array.Empty<DiplomaticAccessSnapshot>(),
            Array.Empty<TerritorialClaimSnapshot>(),
            Array.Empty<TerritorialClaimResponseSnapshot>(),
            Array.Empty<DiplomaticAgreementSnapshot>(),
            Array.Empty<DiplomaticProposalSnapshot>(),
            Array.Empty<DiplomaticHistoryEventSnapshot>());

        var known = new KnownCivilization(
            knownForeign.Id,
            Trust: 0.05,
            EstimatedMilitaryLow: 120.0,
            EstimatedMilitaryHigh: 310.0,
            EstimateConfidence: 0.43,
            LastMilitaryObservationTick: 900,
            HasSharedBorder: false,
            KnownTradeDependence: 0.0,
            KnownWarExhaustion: 0.0,
            KnownToBeAtWar: true,
            HasDefenseTreatyWithObserver: false);
        var knowledge = new KnowledgeSnapshot
        {
            ObservedAtTick = 1000,
            Civilizations = new Dictionary<int, KnownCivilization>
            {
                [knownForeign.Id] = known,
            },
        };

        var view = new ObserverSystemMilitaryPressureView().Build(
            galaxy,
            observer.Id,
            system.Id,
            diplomacy,
            knowledge);

        var ownReadiness = CombatReadinessCalculator.Build(galaxy, observer.Id);
        Require(view.OwnCombatEffectiveArmedVessels == 1,
            "observer-safe pressure did not expose exact own local effective vessel count");
        Require(view.OwnCombatEffectiveArmedStrength > 0.0 &&
                view.OwnCombatEffectiveArmedStrength <= ownReadiness.MaximumArmedStrength,
            "observer-safe pressure produced invalid own local strength");
        Require(view.ThreatState == ObservedSystemThreatState.HostileContactObserved && view.HasObservedHostileContact,
            "known hostile contact last seen in the system did not produce hostile observed pressure");
        Require(view.UnidentifiedContactCount == 1,
            "unidentified observer-local contact was dropped or assigned a hidden identity");
        Require(view.ForeignContacts.Count == 1,
            "hidden third-party physical fleet leaked into observer-safe foreign contact output");

        var foreign = view.ForeignContacts.Single();
        Require(foreign.CivilizationId == knownForeign.Id,
            "observer-safe pressure exposed the wrong foreign civilization identity");
        Require(foreign.EstimatedCivilizationMilitaryLow == 120.0 &&
                foreign.EstimatedCivilizationMilitaryHigh == 310.0 &&
                foreign.MilitaryEstimateConfidence == 0.43,
            "observer-safe pressure replaced supplied intelligence range/confidence with authoritative fleet state");
        Require(foreign.LastMilitaryObservationTick == 900 && foreign.LastContactObservationTick == 980,
            "observer-safe pressure lost distinct military-estimate and local-contact freshness");
        Require(foreign.PoliticalState == DiplomaticPoliticalState.AtWar,
            "observer-visible political state was not carried into military pressure context");
        Require(view.ForeignContacts.All(contact => contact.CivilizationId != hiddenForeign.Id),
            "completely hidden third-party civilization leaked through authoritative physical presence");

        // Diplomatic identification is not military intelligence. Deliberately leave nonzero
        // numeric placeholders in the record while marking the estimate unavailable: pressure
        // must preserve the contact/political facts but suppress all estimate fields to null.
        var unavailableEstimateKnowledge = new KnowledgeSnapshot
        {
            ObservedAtTick = 1000,
            Civilizations = new Dictionary<int, KnownCivilization>
            {
                [knownForeign.Id] = known with
                {
                    HasMilitaryEstimate = false,
                    EstimatedMilitaryLow = 777.0,
                    EstimatedMilitaryHigh = 999.0,
                    EstimateConfidence = 0.99,
                    LastMilitaryObservationTick = 995,
                },
            },
        };
        var unavailableEstimateView = new ObserverSystemMilitaryPressureView().Build(
            galaxy,
            observer.Id,
            system.Id,
            diplomacy,
            unavailableEstimateKnowledge);
        var unavailableForeign = unavailableEstimateView.ForeignContacts.Single();
        Require(unavailableForeign.EstimatedCivilizationMilitaryLow is null &&
                unavailableForeign.EstimatedCivilizationMilitaryHigh is null &&
                unavailableForeign.MilitaryEstimateConfidence is null &&
                unavailableForeign.LastMilitaryObservationTick is null,
            "observer-safe pressure published numeric military placeholders when HasMilitaryEstimate was false");
        Require(unavailableForeign.PoliticalState == DiplomaticPoliticalState.AtWar &&
                unavailableForeign.ContactCondition == ContactCondition.Hostile &&
                unavailableForeign.ContactConfidence == 0.76 &&
                unavailableEstimateView.ThreatState == ObservedSystemThreatState.HostileContactObserved,
            "suppressing unavailable military estimates also erased legitimate contact/political threat facts");

        // Remove all observer-local contact records while leaving both foreign fleets physically
        // present. The fair-information view must become empty rather than infer them from truth.
        var noContacts = diplomacy with { Contacts = Array.Empty<DiplomaticContactView>(), Relationships = Array.Empty<DiplomaticRelationshipView>() };
        var hiddenOnlyView = new ObserverSystemMilitaryPressureView().Build(
            galaxy,
            observer.Id,
            system.Id,
            noContacts,
            new KnowledgeSnapshot
            {
                ObservedAtTick = 1001,
                Civilizations = new Dictionary<int, KnownCivilization>(),
            });
        Require(hiddenOnlyView.ThreatState == ObservedSystemThreatState.NoObservedForeignContact &&
                hiddenOnlyView.UnidentifiedContactCount == 0 &&
                hiddenOnlyView.ForeignContacts.Count == 0,
            "observer-safe pressure inferred hidden foreign presence from authoritative galaxy state");

        Console.WriteLine("PASS: observer-safe system military pressure does not leak hidden foreign fleets");
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
