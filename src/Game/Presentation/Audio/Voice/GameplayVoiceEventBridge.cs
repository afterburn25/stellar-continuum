using System;
using System.Collections.Generic;
using System.Globalization;
using Game.Simulation.Colonization;
using Game.Simulation.Combat;
using Game.Simulation.Construction;
using Game.Simulation.Diplomacy;
using Game.Simulation.Exploration;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;

namespace Game.Presentation.Audio.Voice;

/// <summary>The finite gameplay vocabulary understood by the presentation voice catalogue.</summary>
public enum GameplayVoiceEventKind
{
    ResearchCompleted,
    MajorResearchBreakthrough,
    ConstructionCompleted,
    OrbitalLaunchComplexCompleted,
    OrbitalShipyardCompleted,
    ShipCompleted,
    ShipLaunched,
    FirstInterstellarLaunch,
    SystemReached,
    SurveyCompleted,
    AnomalyDiscovered,
    ColonyFounded,
    UnknownContactDetected,
    FirstContact,
    AlienTransmission,
    WarDeclared,
    FleetAttacked,
    FleetRetreatInitiated,
    FleetDestroyed,
    EngagementConcluded,
    HullCritical,
    DiplomaticProposalRejected,
    DiplomaticAgreementActivated,
    DiplomaticBorderWarningIssued,
    MajorLogisticsWarning,
    CriticalEconomyWarning,
}

/// <summary>Observer context captured at the presentation boundary for one simulation event.</summary>
public sealed record GameplayVoiceRoutingScope(
    int PlayerCivilizationId,
    VoiceFrequency Frequency,
    long SimulationTick,
    string SimulationDate);

/// <summary>
/// Converts scoped authoritative events and observed state transitions into the small,
/// data-driven contract consumed by <see cref="VoiceEventRouter"/>. It never queries the
/// simulation, discovers foreign state, or calls speech/audio services directly.
/// </summary>
public sealed class GameplayVoiceEventBridge
{
    private static readonly IReadOnlyDictionary<GameplayVoiceEventKind, string> EventKeys =
        new Dictionary<GameplayVoiceEventKind, string>
        {
            [GameplayVoiceEventKind.ResearchCompleted] = "research.completed",
            [GameplayVoiceEventKind.MajorResearchBreakthrough] = "research.breakthrough.major",
            [GameplayVoiceEventKind.ConstructionCompleted] = "construction.completed",
            [GameplayVoiceEventKind.OrbitalLaunchComplexCompleted] = "construction.orbital_launch_complex.completed",
            [GameplayVoiceEventKind.OrbitalShipyardCompleted] = "construction.orbital_shipyard.completed",
            [GameplayVoiceEventKind.ShipCompleted] = "ship.completed",
            [GameplayVoiceEventKind.ShipLaunched] = "ship.launched",
            [GameplayVoiceEventKind.FirstInterstellarLaunch] = "ship.interstellar.first_launch",
            [GameplayVoiceEventKind.SystemReached] = "exploration.system.reached",
            [GameplayVoiceEventKind.SurveyCompleted] = "exploration.survey.completed",
            [GameplayVoiceEventKind.AnomalyDiscovered] = "exploration.anomaly.discovered",
            [GameplayVoiceEventKind.ColonyFounded] = "colony.founded",
            [GameplayVoiceEventKind.UnknownContactDetected] = "contact.unknown.detected",
            [GameplayVoiceEventKind.FirstContact] = "contact.first",
            [GameplayVoiceEventKind.AlienTransmission] = "diplomacy.alien.transmission",
            [GameplayVoiceEventKind.WarDeclared] = "diplomacy.war.declared",
            [GameplayVoiceEventKind.FleetAttacked] = "combat.fleet.attacked",
            [GameplayVoiceEventKind.FleetRetreatInitiated] = "combat.fleet.retreat_initiated",
            [GameplayVoiceEventKind.FleetDestroyed] = "combat.fleet.destroyed",
            [GameplayVoiceEventKind.EngagementConcluded] = "combat.engagement.concluded",
            [GameplayVoiceEventKind.HullCritical] = "combat.hull.critical",
            [GameplayVoiceEventKind.DiplomaticProposalRejected] = "diplomacy.proposal.rejected",
            [GameplayVoiceEventKind.DiplomaticAgreementActivated] = "diplomacy.agreement.activated",
            [GameplayVoiceEventKind.DiplomaticBorderWarningIssued] = "diplomacy.border_warning.issued",
            [GameplayVoiceEventKind.MajorLogisticsWarning] = "logistics.critical",
            [GameplayVoiceEventKind.CriticalEconomyWarning] = "economy.treasury.critical",
        };

    private readonly VoiceEventRouter _router;

    public GameplayVoiceEventBridge(VoiceEventRouter router) =>
        _router = router ?? throw new ArgumentNullException(nameof(router));

    public static string EventKey(GameplayVoiceEventKind kind) => EventKeys[kind];

    public bool PublishResearch(ResearchEvent researchEvent, TechnologyDefinition technology,
        string sourceSpeciesId, GameplayVoiceRoutingScope scope, bool firstOccurrence = false)
    {
        ArgumentNullException.ThrowIfNull(researchEvent);
        ArgumentNullException.ThrowIfNull(technology);
        var kind = technology.Category == TechnologyCategory.Ftl
            ? GameplayVoiceEventKind.MajorResearchBreakthrough
            : GameplayVoiceEventKind.ResearchCompleted;
        return PublishOwned(kind, researchEvent.CivilizationId, sourceSpeciesId, scope,
            $"technology:{researchEvent.TechnologyId}", Values(("research_name", technology.Name)), firstOccurrence);
    }

    public bool PublishAdaptiveResearch(int sourceCivilizationId, string sourceSpeciesId,
        string nodeId, string researchName, bool major, GameplayVoiceRoutingScope scope,
        bool firstOccurrence = false) => PublishOwned(
        major ? GameplayVoiceEventKind.MajorResearchBreakthrough : GameplayVoiceEventKind.ResearchCompleted,
        sourceCivilizationId, sourceSpeciesId, scope, $"adaptive-research:{nodeId}",
        Values(("research_name", researchName)), firstOccurrence);

    public bool PublishConstruction(ConstructionEvent constructionEvent, ConstructionProjectDefinition project,
        string sourceSpeciesId, GameplayVoiceRoutingScope scope, bool firstOccurrence = false)
    {
        ArgumentNullException.ThrowIfNull(constructionEvent);
        ArgumentNullException.ThrowIfNull(project);
        var kind = project.Id switch
        {
            "orbital_launch_complex" => GameplayVoiceEventKind.OrbitalLaunchComplexCompleted,
            "orbital_shipyard" => GameplayVoiceEventKind.OrbitalShipyardCompleted,
            _ => GameplayVoiceEventKind.ConstructionCompleted,
        };
        return PublishOwned(kind, constructionEvent.CivilizationId, sourceSpeciesId, scope,
            $"project:{constructionEvent.ProjectId}", Values(("project_name", project.Name)), firstOccurrence);
    }

    public bool PublishShipCompleted(ShipbuildingEvent shipEvent, FleetState fleet, ShipDesignDefinition design,
        string sourceSpeciesId, GameplayVoiceRoutingScope scope, bool firstOccurrence = false)
    {
        ArgumentNullException.ThrowIfNull(shipEvent);
        ArgumentNullException.ThrowIfNull(fleet);
        ArgumentNullException.ThrowIfNull(design);
        return PublishOwned(GameplayVoiceEventKind.ShipCompleted, shipEvent.CivilizationId, sourceSpeciesId,
            scope, $"fleet:{shipEvent.FleetId}:design:{shipEvent.DesignId}",
            Values(("ship_name", fleet.Name), ("ship_class", design.Name)), firstOccurrence);
    }

    public bool PublishShipDeparture(FleetState fleet, string sourceSpeciesId, GameplayVoiceRoutingScope scope,
        bool firstInterstellarDeparture) => PublishOwned(
        firstInterstellarDeparture ? GameplayVoiceEventKind.FirstInterstellarLaunch : GameplayVoiceEventKind.ShipLaunched,
        fleet.CivilizationId, sourceSpeciesId, scope, $"fleet:{fleet.Id}:departure",
        Values(("ship_name", fleet.Name)), firstInterstellarDeparture);

    public bool PublishSystemReached(FleetState fleet, int systemId, string systemName,
        string sourceSpeciesId, GameplayVoiceRoutingScope scope, bool firstExtrasolarArrival = false) =>
        PublishOwned(GameplayVoiceEventKind.SystemReached, fleet.CivilizationId, sourceSpeciesId, scope,
            $"fleet:{fleet.Id}:arrival:system:{systemId}", Values(("system_name", systemName)),
            firstExtrasolarArrival);

    public bool PublishExploration(ExplorationEvent explorationEvent, string sourceSpeciesId,
        GameplayVoiceRoutingScope scope, string systemName, string? planetaryBodyName = null,
        string? targetCivilizationName = null, bool firstOccurrence = false)
    {
        ArgumentNullException.ThrowIfNull(explorationEvent);
        if (explorationEvent.Type is not (ExplorationEventType.SystemSurveyed or
            ExplorationEventType.AnomalySignatureDetected or ExplorationEventType.AnomalySurveyed or
            ExplorationEventType.ActivitySignatureDetected or ExplorationEventType.FirstContact)) return false;
        var (kind, variables) = explorationEvent.Type switch
        {
            ExplorationEventType.SystemSurveyed => (GameplayVoiceEventKind.SurveyCompleted,
                Values(("system_name", systemName))),
            ExplorationEventType.AnomalySignatureDetected or ExplorationEventType.AnomalySurveyed =>
                (GameplayVoiceEventKind.AnomalyDiscovered,
                    Values(("planet_name", planetaryBodyName ?? systemName))),
            ExplorationEventType.ActivitySignatureDetected => (GameplayVoiceEventKind.UnknownContactDetected,
                Values(("system_name", systemName))),
            ExplorationEventType.FirstContact => (GameplayVoiceEventKind.FirstContact,
                Values(("civilization_name", targetCivilizationName ?? "unidentified civilization"))),
            _ => throw new ArgumentOutOfRangeException(nameof(explorationEvent),
                $"Exploration event {explorationEvent.Type} has no voice mapping."),
        };
        return PublishOwned(kind, explorationEvent.CivilizationId, sourceSpeciesId, scope,
            $"exploration:{explorationEvent.Type}:fleet:{explorationEvent.FleetId}:system:{explorationEvent.SystemId}:body:{explorationEvent.PlanetaryBodyId?.ToString(CultureInfo.InvariantCulture) ?? "none"}",
            variables, firstOccurrence);
    }

    public bool PublishColonyFounded(ColonizationEvent colonizationEvent, ColonyState colony,
        string planetName, string sourceSpeciesId, GameplayVoiceRoutingScope scope, bool firstOccurrence = false)
    {
        ArgumentNullException.ThrowIfNull(colonizationEvent);
        ArgumentNullException.ThrowIfNull(colony);
        return PublishOwned(GameplayVoiceEventKind.ColonyFounded, colonizationEvent.CivilizationId,
            sourceSpeciesId, scope, $"colony:{colonizationEvent.ColonyId}",
            Values(("planet_name", planetName), ("colony_name", colony.Name)), firstOccurrence);
    }

    public bool PublishAlienTransmission(DiplomaticProposalSnapshot proposal, string sourceSpeciesId,
        GameplayVoiceRoutingScope scope)
    {
        ArgumentNullException.ThrowIfNull(proposal);
        return PublishDirect(GameplayVoiceEventKind.AlienTransmission, proposal.ProposerCivilizationId,
            sourceSpeciesId, proposal.RecipientCivilizationId, scope, $"proposal:{proposal.ProposalId}",
            Values(("message", proposal.Summary)));
    }

    public bool PublishWarDeclared(DiplomaticHistoryEventSnapshot diplomaticEvent, int speakingCivilizationId,
        string sourceSpeciesId, string enemyName, GameplayVoiceRoutingScope scope, bool firstOccurrence = false)
    {
        ArgumentNullException.ThrowIfNull(diplomaticEvent);
        if (diplomaticEvent.Kind != DiplomaticEventKind.WarDeclared)
            throw new ArgumentOutOfRangeException(nameof(diplomaticEvent), "The diplomatic event is not a war declaration.");
        if (diplomaticEvent.PrimaryCivilizationId != speakingCivilizationId &&
            diplomaticEvent.SecondaryCivilizationId != speakingCivilizationId) return false;
        var overrides = diplomaticEvent.PrimaryCivilizationId == speakingCivilizationId
            ? new VoiceEventOverrides { ExactLine = "War has been declared against {enemy_name}." }
            : null;
        return PublishOwned(GameplayVoiceEventKind.WarDeclared, speakingCivilizationId, sourceSpeciesId,
            scope, $"diplomacy-event:{diplomaticEvent.EventId}", Values(("enemy_name", enemyName)),
            firstOccurrence, overrides);
    }

    public bool PublishDiplomaticTransition(DiplomaticHistoryEventSnapshot diplomaticEvent,
        int speakingCivilizationId, string sourceSpeciesId, GameplayVoiceRoutingScope scope)
    {
        ArgumentNullException.ThrowIfNull(diplomaticEvent);
        if (diplomaticEvent.PrimaryCivilizationId != speakingCivilizationId &&
            diplomaticEvent.SecondaryCivilizationId != speakingCivilizationId) return false;
        var kind = diplomaticEvent.Kind switch
        {
            DiplomaticEventKind.ProposalRejected => GameplayVoiceEventKind.DiplomaticProposalRejected,
            DiplomaticEventKind.AgreementActivated => GameplayVoiceEventKind.DiplomaticAgreementActivated,
            DiplomaticEventKind.BorderWarningIssued => GameplayVoiceEventKind.DiplomaticBorderWarningIssued,
            _ => throw new ArgumentOutOfRangeException(nameof(diplomaticEvent),
                $"Diplomatic event {diplomaticEvent.Kind} has no transition voice mapping."),
        };
        return PublishOwned(kind, speakingCivilizationId, sourceSpeciesId, scope,
            $"diplomacy-event:{diplomaticEvent.EventId}", Values(("message", diplomaticEvent.Summary)));
    }

    public bool PublishFleetAttacked(CombatEvent combatEvent, int speakingCivilizationId,
        string sourceSpeciesId, string fleetName, string systemName, GameplayVoiceRoutingScope scope)
    {
        ArgumentNullException.ThrowIfNull(combatEvent);
        if (combatEvent.Type != CombatEventType.EngagementStarted)
            throw new ArgumentOutOfRangeException(nameof(combatEvent), "The combat event is not an engagement start.");
        if (combatEvent.ActorCivilizationId != speakingCivilizationId &&
            combatEvent.TargetCivilizationId != speakingCivilizationId) return false;
        return PublishOwned(GameplayVoiceEventKind.FleetAttacked, speakingCivilizationId, sourceSpeciesId,
            scope, $"combat:{combatEvent.Type}:actor:{combatEvent.ActorFleetId}:target:{combatEvent.TargetFleetId?.ToString(CultureInfo.InvariantCulture) ?? "none"}:system:{combatEvent.SystemId?.ToString(CultureInfo.InvariantCulture) ?? "none"}",
            Values(("fleet_name", fleetName), ("system_name", systemName)));
    }

    public bool PublishFleetRetreat(CombatEvent combatEvent, int speakingCivilizationId,
        string sourceSpeciesId, string fleetName, string systemName, GameplayVoiceRoutingScope scope)
    {
        ArgumentNullException.ThrowIfNull(combatEvent);
        if (combatEvent.Type != CombatEventType.FleetRetreatInitiated)
            throw new ArgumentOutOfRangeException(nameof(combatEvent), "The combat event is not a retreat.");
        if (combatEvent.ActorCivilizationId != speakingCivilizationId) return false;
        return PublishOwned(GameplayVoiceEventKind.FleetRetreatInitiated, speakingCivilizationId,
            sourceSpeciesId, scope, CombatIdentity(combatEvent),
            Values(("fleet_name", fleetName), ("system_name", systemName)));
    }

    public bool PublishFleetDestroyed(CombatEvent combatEvent, int speakingCivilizationId,
        string sourceSpeciesId, string fleetName, string systemName, GameplayVoiceRoutingScope scope)
    {
        ArgumentNullException.ThrowIfNull(combatEvent);
        if (combatEvent.Type != CombatEventType.FleetDestroyed)
            throw new ArgumentOutOfRangeException(nameof(combatEvent), "The combat event is not a fleet loss.");
        if (combatEvent.TargetCivilizationId != speakingCivilizationId) return false;
        return PublishOwned(GameplayVoiceEventKind.FleetDestroyed, speakingCivilizationId,
            sourceSpeciesId, scope, CombatIdentity(combatEvent),
            Values(("fleet_name", fleetName), ("system_name", systemName)));
    }

    public bool PublishEngagementConcluded(CombatEvent combatEvent, int speakingCivilizationId,
        string sourceSpeciesId, string systemName, GameplayVoiceRoutingScope scope)
    {
        ArgumentNullException.ThrowIfNull(combatEvent);
        if (combatEvent.Type != CombatEventType.EngagementEnded)
            throw new ArgumentOutOfRangeException(nameof(combatEvent), "The combat event is not an engagement conclusion.");
        if (combatEvent.ActorCivilizationId != speakingCivilizationId &&
            combatEvent.TargetCivilizationId != speakingCivilizationId) return false;
        return PublishOwned(GameplayVoiceEventKind.EngagementConcluded, speakingCivilizationId,
            sourceSpeciesId, scope, CombatIdentity(combatEvent), Values(("system_name", systemName)));
    }

    public bool PublishHullCritical(int sourceCivilizationId, string sourceSpeciesId, int fleetId,
        string shipName, GameplayVoiceRoutingScope scope) => PublishOwned(
        GameplayVoiceEventKind.HullCritical, sourceCivilizationId, sourceSpeciesId, scope,
        $"fleet:{fleetId}:hull-critical", Values(("ship_name", shipName)));

    public bool PublishLogisticsCritical(int sourceCivilizationId, string sourceSpeciesId, int systemId,
        string systemName, string detail, GameplayVoiceRoutingScope scope) => PublishOwned(
        GameplayVoiceEventKind.MajorLogisticsWarning, sourceCivilizationId, sourceSpeciesId, scope,
        $"system:{systemId}:logistics-critical", Values(("system_name", systemName), ("detail", detail)));

    public bool PublishEconomyCritical(int sourceCivilizationId, string sourceSpeciesId, string amount,
        GameplayVoiceRoutingScope scope) => PublishOwned(GameplayVoiceEventKind.CriticalEconomyWarning,
        sourceCivilizationId, sourceSpeciesId, scope, "treasury-critical", Values(("amount", amount)));

    private bool PublishOwned(GameplayVoiceEventKind kind, int sourceCivilizationId, string sourceSpeciesId,
        GameplayVoiceRoutingScope scope, string identity, IReadOnlyDictionary<string, string> variables,
        bool firstOccurrence = false, VoiceEventOverrides? overrides = null)
    {
        if (sourceCivilizationId != scope.PlayerCivilizationId) return false;
        return Emit(kind, sourceCivilizationId, sourceSpeciesId, VoiceAudience.OwnCivilization, null,
            false, scope, identity, variables, firstOccurrence, overrides);
    }

    private bool PublishDirect(GameplayVoiceEventKind kind, int sourceCivilizationId, string sourceSpeciesId,
        int recipientCivilizationId, GameplayVoiceRoutingScope scope, string identity,
        IReadOnlyDictionary<string, string> variables, VoiceEventOverrides? overrides = null) =>
        Emit(kind, sourceCivilizationId, sourceSpeciesId, VoiceAudience.DirectCommunication,
            recipientCivilizationId, false, scope, identity, variables, false, overrides);

    private bool Emit(GameplayVoiceEventKind kind, int sourceCivilizationId, string sourceSpeciesId,
        VoiceAudience audience, int? recipientCivilizationId, bool observerEvidence,
        GameplayVoiceRoutingScope scope, string identity, IReadOnlyDictionary<string, string> variables,
        bool firstOccurrence, VoiceEventOverrides? overrides)
    {
        var key = EventKey(kind);
        var eventId = $"{key}:civ:{sourceCivilizationId}:{identity}:tick:{scope.SimulationTick}";
        var gameplayEvent = new GameplayVoiceEvent(key, sourceCivilizationId, eventId, variables,
            scope.SimulationTick, scope.SimulationDate)
        {
            SourceSpeciesId = sourceSpeciesId ?? string.Empty,
            Audience = audience,
            RecipientCivilizationId = recipientCivilizationId,
            ObserverEvidence = observerEvidence,
            FirstOccurrence = firstOccurrence,
            Overrides = overrides,
        };
        return _router.Emit(gameplayEvent, new VoiceRoutingContext(scope.PlayerCivilizationId, scope.Frequency));
    }

    private static IReadOnlyDictionary<string, string> Values(params (string Key, string Value)[] values)
    {
        var result = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var (key, value) in values) result[key] = value ?? string.Empty;
        return result;
    }

    private static string CombatIdentity(CombatEvent combatEvent) =>
        $"combat:{combatEvent.Type}:actor:{combatEvent.ActorFleetId}:target:{combatEvent.TargetFleetId?.ToString(CultureInfo.InvariantCulture) ?? "none"}:system:{combatEvent.SystemId?.ToString(CultureInfo.InvariantCulture) ?? "none"}";
}
