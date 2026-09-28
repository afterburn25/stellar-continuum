using System.Numerics;
using System.Text.Json;
using Game.Presentation.Audio.Voice;
using Game.Simulation.Colonization;
using Game.Simulation.Combat;
using Game.Simulation.Construction;
using Game.Simulation.Diplomacy;
using Game.Simulation.Exploration;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;

namespace Game.CoreRuntime.Validation;

internal static class GameplayVoiceEventBridgeValidation
{
    private static readonly IReadOnlyDictionary<GameplayVoiceEventKind, (string Key, VoiceSpeakerRole Role)> Required =
        new Dictionary<GameplayVoiceEventKind, (string, VoiceSpeakerRole)>
        {
            [GameplayVoiceEventKind.ResearchCompleted] = ("research.completed", VoiceSpeakerRole.ChiefScientist),
            [GameplayVoiceEventKind.MajorResearchBreakthrough] = ("research.breakthrough.major", VoiceSpeakerRole.ChiefScientist),
            [GameplayVoiceEventKind.ConstructionCompleted] = ("construction.completed", VoiceSpeakerRole.OperationsOfficer),
            [GameplayVoiceEventKind.OrbitalLaunchComplexCompleted] = ("construction.orbital_launch_complex.completed", VoiceSpeakerRole.OperationsOfficer),
            [GameplayVoiceEventKind.OrbitalShipyardCompleted] = ("construction.orbital_shipyard.completed", VoiceSpeakerRole.FleetCommander),
            [GameplayVoiceEventKind.ShipCompleted] = ("ship.completed", VoiceSpeakerRole.FleetCommander),
            [GameplayVoiceEventKind.ShipLaunched] = ("ship.launched", VoiceSpeakerRole.FleetCommander),
            [GameplayVoiceEventKind.FirstInterstellarLaunch] = ("ship.interstellar.first_launch", VoiceSpeakerRole.FleetCommander),
            [GameplayVoiceEventKind.SystemReached] = ("exploration.system.reached", VoiceSpeakerRole.ChiefScientist),
            [GameplayVoiceEventKind.SurveyCompleted] = ("exploration.survey.completed", VoiceSpeakerRole.ChiefScientist),
            [GameplayVoiceEventKind.AnomalyDiscovered] = ("exploration.anomaly.discovered", VoiceSpeakerRole.ChiefScientist),
            [GameplayVoiceEventKind.ColonyFounded] = ("colony.founded", VoiceSpeakerRole.Governor),
            [GameplayVoiceEventKind.UnknownContactDetected] = ("contact.unknown.detected", VoiceSpeakerRole.ShipComputer),
            [GameplayVoiceEventKind.FirstContact] = ("contact.first", VoiceSpeakerRole.Diplomat),
            [GameplayVoiceEventKind.AlienTransmission] = ("diplomacy.alien.transmission", VoiceSpeakerRole.AlienDiplomat),
            [GameplayVoiceEventKind.WarDeclared] = ("diplomacy.war.declared", VoiceSpeakerRole.Diplomat),
            [GameplayVoiceEventKind.FleetAttacked] = ("combat.fleet.attacked", VoiceSpeakerRole.FleetCommander),
            [GameplayVoiceEventKind.FleetRetreatInitiated] = ("combat.fleet.retreat_initiated", VoiceSpeakerRole.FleetCommander),
            [GameplayVoiceEventKind.FleetDestroyed] = ("combat.fleet.destroyed", VoiceSpeakerRole.FleetCommander),
            [GameplayVoiceEventKind.EngagementConcluded] = ("combat.engagement.concluded", VoiceSpeakerRole.FleetCommander),
            [GameplayVoiceEventKind.HullCritical] = ("combat.hull.critical", VoiceSpeakerRole.ShipComputer),
            [GameplayVoiceEventKind.DiplomaticProposalRejected] = ("diplomacy.proposal.rejected", VoiceSpeakerRole.Diplomat),
            [GameplayVoiceEventKind.DiplomaticAgreementActivated] = ("diplomacy.agreement.activated", VoiceSpeakerRole.Diplomat),
            [GameplayVoiceEventKind.DiplomaticBorderWarningIssued] = ("diplomacy.border_warning.issued", VoiceSpeakerRole.Diplomat),
            [GameplayVoiceEventKind.MajorLogisticsWarning] = ("logistics.critical", VoiceSpeakerRole.OperationsOfficer),
            [GameplayVoiceEventKind.CriticalEconomyWarning] = ("economy.treasury.critical", VoiceSpeakerRole.EconomicAdvisor),
        };

    [Game.Validation.RegressionCheck]
    public static void Run()
    {
        CanonicalCatalogCoversEveryRequiredEvent();
        AuthoritativeEventsUseNamedDataAndCurrentScope();
        AvailableCombatAndDiplomaticTransitionsRemainTruthful();
        ForeignEventsCannotLeakAndDirectMessagesRetainTheirSource();
        FrequencyAndFirstLaunchRoutingAreDistinct();
    }

    private static void CanonicalCatalogCoversEveryRequiredEvent()
    {
        Require(Enum.GetValues<GameplayVoiceEventKind>().Length == Required.Count,
            "gameplay voice vocabulary and fixture coverage have diverged");
        var catalogPath = FindRepositoryFile(Path.Combine("data", "voice_profiles", "events.json"));
        using var document = JsonDocument.Parse(File.ReadAllText(catalogPath));
        var catalog = document.RootElement.EnumerateArray().ToDictionary(
            value => value.GetProperty("event").GetString()!, StringComparer.Ordinal);

        foreach (var (kind, expected) in Required)
        {
            Require(GameplayVoiceEventBridge.EventKey(kind) == expected.Key,
                $"{kind} mapped to the wrong canonical key");
            Require(catalog.TryGetValue(expected.Key, out var cue),
                $"dialogue catalogue is missing {expected.Key}");
            Require(Enum.TryParse<VoiceSpeakerRole>(cue.GetProperty("speakerRole").GetString(), true, out var role) &&
                    role == expected.Role,
                $"{expected.Key} has the wrong speaker role");
        }
    }

    private static void AuthoritativeEventsUseNamedDataAndCurrentScope()
    {
        var requests = new List<SpeechRequest>();
        var bridge = CreateBridge(requests,
            Cue(GameplayVoiceEventKind.ResearchCompleted, "Research complete. {research_name} is now available.", VoiceSpeakerRole.ChiefScientist),
            Cue(GameplayVoiceEventKind.OrbitalShipyardCompleted, "Orbital shipyard online.", VoiceSpeakerRole.FleetCommander),
            Cue(GameplayVoiceEventKind.ShipCompleted, "{ship_class} construction complete. {ship_name} awaits orders.", VoiceSpeakerRole.FleetCommander),
            Cue(GameplayVoiceEventKind.SystemReached, "We have reached {system_name}.", VoiceSpeakerRole.ChiefScientist),
            Cue(GameplayVoiceEventKind.ColonyFounded, "Settlement established on {planet_name}. Welcome to {colony_name}.", VoiceSpeakerRole.Governor),
            Cue(GameplayVoiceEventKind.FleetAttacked, "{fleet_name} is under attack in {system_name}.", VoiceSpeakerRole.FleetCommander));
        var scope = Scope(player: 1, VoiceFrequency.Frequent, tick: 7500);

        Require(bridge.PublishResearch(new ResearchEvent(1, "orbital_industry", "authoritative"),
            TechnologyRegistry.Get("orbital_industry"), "terran_baseline", scope),
            "owned research event was not routed");
        Require(requests[^1].Text == "Research complete. Orbital Industry is now available.",
            "research_name was not populated from the technology definition");
        var researchEventId = requests[^1].EventId;
        Require(researchEventId is not null &&
                researchEventId.Contains("technology:orbital_industry", StringComparison.Ordinal) &&
                researchEventId.EndsWith(":tick:7500", StringComparison.Ordinal),
            "research event identity omitted authoritative identity or simulation tick");
        Require(requests[^1].SpeakerContext is { SourceCivilizationId: 1, SourceSpeciesId: "terran_baseline" },
            "owned event lost civilization/species scope");

        Require(bridge.PublishConstruction(new ConstructionEvent(1, "orbital_shipyard", "authoritative"),
            ConstructionRegistry.Get("orbital_shipyard"), "terran_baseline", scope),
            "orbital shipyard completion was not routed");
        Require(requests[^1].LocalizationKey!.StartsWith("voice.construction.orbital_shipyard.completed.", StringComparison.Ordinal),
            "orbital shipyard used a legacy or generic dialogue key");

        var fleet = Fleet(11, 1, "ISS Horizon");
        Require(bridge.PublishShipCompleted(new ShipbuildingEvent(1, fleet.Id, "warp_scout", "authoritative"),
            fleet, ShipDesignRegistry.Get("warp_scout"), "terran_baseline", scope),
            "ship completion was not routed");
        Require(requests[^1].Text.Contains("Pathfinder Scout", StringComparison.Ordinal) &&
                requests[^1].Text.Contains("ISS Horizon", StringComparison.Ordinal),
            "ship event omitted its real class or assigned name");

        Require(!bridge.PublishExploration(new ExplorationEvent(ExplorationEventType.SystemDetected,
                1, fleet.Id, 9, "authoritative"), "terran_baseline", scope, "Alpha Centauri"),
            "sensor-only system detection was incorrectly voiced as physical arrival");
        Require(bridge.PublishSystemReached(fleet, 9, "Alpha Centauri", "terran_baseline", scope),
            "authoritative fleet arrival was not routed");
        Require(requests[^1].Text == "We have reached Alpha Centauri.",
            "system_name was not populated from the authoritative system identity");

        var colony = new ColonyState { Id = 20, CivilizationId = 1, SystemId = 9, PlanetaryBodyId = 88, Name = "New Dawn" };
        Require(bridge.PublishColonyFounded(new ColonizationEvent(1, 12, 9, 20, "authoritative"),
            colony, "Proxima b", "terran_baseline", scope), "colony founding was not routed");
        Require(requests[^1].Text.Contains("Proxima b", StringComparison.Ordinal) &&
                requests[^1].Text.Contains("New Dawn", StringComparison.Ordinal),
            "colony event omitted its real colony or planet name");

        var attack = new CombatEvent(CombatEventType.EngagementStarted, 9, 2, 44, 1, fleet.Id,
            0, 0, 0, "authoritative");
        Require(bridge.PublishFleetAttacked(attack, 1, "terran_baseline", fleet.Name,
            "Alpha Centauri", scope), "player-involved combat event was not routed");
        Require(requests[^1].Text == "ISS Horizon is under attack in Alpha Centauri.",
            "combat event omitted its scoped fleet or system name");
    }

    private static void AvailableCombatAndDiplomaticTransitionsRemainTruthful()
    {
        var requests = new List<SpeechRequest>();
        var bridge = CreateBridge(requests,
            Cue(GameplayVoiceEventKind.FleetRetreatInitiated, "{fleet_name} is withdrawing from {system_name}.", VoiceSpeakerRole.FleetCommander),
            Cue(GameplayVoiceEventKind.FleetDestroyed, "{fleet_name} has been lost in {system_name}.", VoiceSpeakerRole.FleetCommander),
            Cue(GameplayVoiceEventKind.EngagementConcluded, "The engagement in {system_name} has concluded.", VoiceSpeakerRole.FleetCommander),
            Cue(GameplayVoiceEventKind.DiplomaticProposalRejected, "{message}", VoiceSpeakerRole.Diplomat),
            Cue(GameplayVoiceEventKind.DiplomaticAgreementActivated, "{message}", VoiceSpeakerRole.Diplomat),
            Cue(GameplayVoiceEventKind.DiplomaticBorderWarningIssued, "{message}", VoiceSpeakerRole.Diplomat));
        var scope = Scope(player: 1, VoiceFrequency.Frequent, tick: 8000);

        var retreat = new CombatEvent(CombatEventType.FleetRetreatInitiated, 9, 1, 11, null, null,
            0, 0, 0, "ISS Horizon began retreating.");
        Require(bridge.PublishFleetRetreat(retreat, 1, "terran_baseline", "ISS Horizon",
            "Alpha Centauri", scope), "authoritative owned retreat was not routed");
        Require(requests[^1].Text == "ISS Horizon is withdrawing from Alpha Centauri.",
            "retreat report omitted its real fleet or observed system");

        var destroyed = new CombatEvent(CombatEventType.FleetDestroyed, 9, 2, 44, 1, 11,
            0, 0, 0, "ISS Horizon was destroyed.");
        Require(bridge.PublishFleetDestroyed(destroyed, 1, "terran_baseline", "ISS Horizon",
            "Alpha Centauri", scope with { SimulationTick = 8001 }), "authoritative owned fleet loss was not routed");

        var ended = new CombatEvent(CombatEventType.EngagementEnded, 9, 2, 44, 1, 11,
            0, 0, 0, "Engagement ended.");
        Require(bridge.PublishEngagementConcluded(ended, 1, "terran_baseline", "Alpha Centauri",
            scope with { SimulationTick = 8002 }), "player-involved engagement end was not routed");
        Require(requests[^1].Text == "The engagement in Alpha Centauri has concluded.",
            "engagement conclusion invented a winner or victory");

        var transitions = new[]
        {
            new DiplomaticHistoryEventSnapshot(81, 8003, DiplomaticEventKind.ProposalRejected,
                1, 2, null, "Proposal 42 was rejected.", new[] { 1, 2 }),
            new DiplomaticHistoryEventSnapshot(82, 8004, DiplomaticEventKind.AgreementActivated,
                1, 2, null, "Agreement 9 (Trade) became active.", new[] { 1, 2 }),
            new DiplomaticHistoryEventSnapshot(83, 8005, DiplomaticEventKind.BorderWarningIssued,
                2, 1, 9, "Civilization 2 warned civilization 1 against unauthorized presence in system 9.",
                new[] { 1, 2 }),
        };
        foreach (var transition in transitions)
        {
            Require(bridge.PublishDiplomaticTransition(transition, 1, "terran_baseline",
                    scope with { SimulationTick = transition.Tick }),
                $"{transition.Kind} was not routed");
            Require(requests[^1].Text == transition.Summary,
                $"{transition.Kind} report was fabricated instead of preserving the authoritative summary");
        }
    }

    private static void ForeignEventsCannotLeakAndDirectMessagesRetainTheirSource()
    {
        var requests = new List<SpeechRequest>();
        var bridge = CreateBridge(requests,
            Cue(GameplayVoiceEventKind.ResearchCompleted, "{research_name}", VoiceSpeakerRole.ChiefScientist),
            Cue(GameplayVoiceEventKind.AlienTransmission, "{message}", VoiceSpeakerRole.AlienDiplomat),
            Cue(GameplayVoiceEventKind.WarDeclared, "{enemy_name} has declared war.", VoiceSpeakerRole.Diplomat));
        var scope = Scope(player: 1, VoiceFrequency.Frequent, tick: 9000);

        Require(!bridge.PublishResearch(new ResearchEvent(2, "orbital_industry", "hidden"),
            TechnologyRegistry.Get("orbital_industry"), "vespari", scope),
            "foreign internal research leaked to the player");
        Require(requests.Count == 0, "rejected foreign research still submitted speech");

        var incoming = new DiplomaticProposalSnapshot(51, 2, 1, DiplomaticProposalKind.Demand, null,
            DiplomaticProposalStatus.Pending, 9000, null, "Withdraw from our frontier.", null);
        Require(bridge.PublishAlienTransmission(incoming, "vespari", scope),
            "authorized incoming alien transmission was not routed");
        Require(requests.Count == 1 && requests[0].Text == incoming.Summary,
            "alien speech was fabricated instead of using the actual transmission");
        Require(requests[0].SpeakerContext is { SourceCivilizationId: 2, SourceSpeciesId: "vespari",
                    Role: VoiceSpeakerRole.AlienDiplomat },
            "alien transmission lost its actual civilization/species/role identity");

        var thirdParty = incoming with { ProposalId = 52, RecipientCivilizationId = 3 };
        Require(!bridge.PublishAlienTransmission(thirdParty, "vespari", scope),
            "third-party transmission leaked to the player");
        Require(requests.Count == 1, "unauthorized direct communication submitted speech");

        var playerDeclaration = new DiplomaticHistoryEventSnapshot(72, 9001, DiplomaticEventKind.WarDeclared,
            1, 2, null, "authoritative", new[] { 1, 2 });
        Require(bridge.PublishWarDeclared(playerDeclaration, 1, "terran_baseline", "Vespari", scope),
            "observer-visible war declaration was not routed");
        Require(requests[^1].Text == "War has been declared against Vespari.",
            "player declaration was voiced as a false foreign declaration");

        var thirdPartyWar = playerDeclaration with
        {
            EventId = 73,
            PrimaryCivilizationId = 2,
            SecondaryCivilizationId = 3,
            KnownToCivilizationIds = new[] { 1, 2, 3 },
        };
        var count = requests.Count;
        Require(!bridge.PublishWarDeclared(thirdPartyWar, 1, "terran_baseline", "Vespari", scope),
            "observer-visible third-party war was voiced as the player's war");
        Require(requests.Count == count, "rejected third-party war still submitted speech");
    }

    private static void FrequencyAndFirstLaunchRoutingAreDistinct()
    {
        var requests = new List<SpeechRequest>();
        var bridge = CreateBridge(requests,
            Cue(GameplayVoiceEventKind.ResearchCompleted, "{research_name}", VoiceSpeakerRole.ChiefScientist,
                VoiceFrequency.Normal),
            Cue(GameplayVoiceEventKind.MajorResearchBreakthrough, "{research_name}", VoiceSpeakerRole.ChiefScientist,
                VoiceFrequency.Minimal),
            Cue(GameplayVoiceEventKind.FirstInterstellarLaunch, "First: {ship_name}", VoiceSpeakerRole.FleetCommander,
                VoiceFrequency.Minimal),
            Cue(GameplayVoiceEventKind.ShipLaunched, "Routine: {ship_name}", VoiceSpeakerRole.FleetCommander,
                VoiceFrequency.Normal));
        var minimal = Scope(player: 1, VoiceFrequency.Minimal, tick: 11000);

        Require(!bridge.PublishResearch(new ResearchEvent(1, "orbital_industry", "authoritative"),
            TechnologyRegistry.Get("orbital_industry"), "terran_baseline", minimal),
            "Minimal frequency admitted routine research");
        Require(bridge.PublishResearch(new ResearchEvent(1, "prototype_warp_drive", "authoritative"),
            TechnologyRegistry.Get("prototype_warp_drive"), "terran_baseline", minimal),
            "Minimal frequency rejected a major FTL breakthrough");

        var fleet = Fleet(70, 1, "Pathfinder 1");
        Require(bridge.PublishShipDeparture(fleet, "terran_baseline", minimal with { SimulationTick = 11001 }, true),
            "first interstellar departure was not routed at Minimal frequency");
        Require(requests[^1].LocalizationKey!.StartsWith("voice.ship.interstellar.first_launch.", StringComparison.Ordinal),
            "first departure used the routine launch key");
        Require(!bridge.PublishShipDeparture(fleet, "terran_baseline", minimal with { SimulationTick = 11002 }, false),
            "Minimal frequency admitted a routine later launch");

        var normal = minimal with { Frequency = VoiceFrequency.Normal, SimulationTick = 11003 };
        Require(bridge.PublishShipDeparture(fleet, "terran_baseline", normal, false),
            "Normal frequency rejected a routine later launch");
        Require(requests[^1].LocalizationKey!.StartsWith("voice.ship.launched.", StringComparison.Ordinal),
            "routine departure reused the first-launch key");
    }

    private static GameplayVoiceEventBridge CreateBridge(List<SpeechRequest> requests,
        params VoiceEventCue[] cues) => new(new VoiceEventRouter(cues, requests.Add));

    private static VoiceEventCue Cue(GameplayVoiceEventKind kind, string line, VoiceSpeakerRole role,
        VoiceFrequency frequency = VoiceFrequency.Minimal) => new(
        GameplayVoiceEventBridge.EventKey(kind), "test-profile", new[] { line }, CooldownSeconds: 0)
    {
        DialogueKey = GameplayVoiceEventBridge.EventKey(kind),
        SpeakerRole = role,
        Frequency = frequency,
    };

    private static GameplayVoiceRoutingScope Scope(int player, VoiceFrequency frequency, long tick) =>
        new(player, frequency, tick, "2050-01-08");

    private static FleetState Fleet(int id, int civilizationId, string name) => new()
    {
        Id = id,
        CivilizationId = civilizationId,
        Name = name,
        Role = FleetRole.Scout,
        DesignId = "warp_scout",
        Position = Vector2.Zero,
        CurrentSystemId = 0,
    };

    private static string FindRepositoryFile(string relativePath)
    {
        foreach (var start in new[] { Directory.GetCurrentDirectory(), AppContext.BaseDirectory })
        {
            var directory = new DirectoryInfo(start);
            while (directory is not null)
            {
                var candidate = Path.Combine(directory.FullName, relativePath);
                if (File.Exists(candidate)) return candidate;
                directory = directory.Parent;
            }
        }
        throw new FileNotFoundException($"Could not locate repository file '{relativePath}'.");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
