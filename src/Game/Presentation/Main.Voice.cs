using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Game.Diagnostics;
using Game.Presentation.Audio.Voice;
using Game.Simulation.Colonization;
using Game.Simulation.Combat;
using Game.Simulation.Construction;
using Game.Simulation.Diplomacy;
using Game.Simulation.Economy;
using Game.Simulation.Exploration;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Time;

namespace Game.Presentation;

public partial class Main
{
    public VoicePlaybackController? UiVoice { get; private set; }
    public bool UiHasVoiceMilestone(string key) => _voiceEvents?.HasEmitted(key) == true;

    private VoiceEventRouter? _voiceEvents;
    private long _reconnaissanceRequiredClickRevision;
    private GameplayVoiceEventBridge? _gameplayVoice;
    private readonly Dictionary<int, bool> _voiceFleetTransit = new();
    private readonly HashSet<long> _voiceProposals = new();
    private readonly HashSet<long> _voiceDiplomacyEvents = new();
    private readonly HashSet<int> _voiceCriticalHulls = new();
    private readonly HashSet<int> _voiceCriticalLogisticsSystems = new();
    private readonly HashSet<string> _voiceMatureResearch = new(StringComparer.Ordinal);
    private bool _voiceBaseline;
    private bool _voiceOpening = true;
    private bool _voiceHasInterstellarLaunch;
    private bool _voiceHasExtrasolarArrival;
    private bool _voiceHasWarDeclaration;
    private bool _voiceHasFirstContact;
    private double _voiceRefresh;

    protected void InitializeVoicePresentation()
    {
        UiVoice = new VoicePlaybackController { Name = "VoicePlaybackController" };
        AddChild(UiVoice);
        try
        {
            _voiceEvents = CreateGameplayVoiceRouter();
            _gameplayVoice = new GameplayVoiceEventBridge(_voiceEvents);
        }
        catch (Exception error)
        {
            SupportLogger.Log("voice-fallback", "Dialogue catalogue unavailable: " + error.Message);
        }
        BaselineVoiceResearch();
        BaselineFirstOccurrenceState();
    }

    private void ResetVoicePresentation()
    {
        UiVoice?.ResetCampaign();
        _voiceEvents?.Reset();
        _voiceFleetTransit.Clear();
        _voiceProposals.Clear();
        _voiceDiplomacyEvents.Clear();
        _voiceCriticalHulls.Clear();
        _voiceCriticalLogisticsSystems.Clear();
        _voiceBaseline = false;
        _voiceOpening = true;
        _voiceRefresh = 0;
        BaselineVoiceResearch();
        BaselineFirstOccurrenceState();
    }

    protected void RefreshVoicePresentation(double delta)
    {
        if (_galaxy is null || UiVoice is null) return;
        if (_voiceOpening && !UiIsMenuOpen)
        {
            var player = PlayerCivilization;
            var scope = VoiceScope;
            _voiceEvents?.Emit(new GameplayVoiceEvent("opening", player.Id, "opening:" + _galaxy.Seed,
                new Dictionary<string, string> { ["detail"] = string.Empty }, scope.SimulationTick,
                scope.SimulationDate)
            {
                SourceSpeciesId = player.SpeciesId,
                FirstOccurrence = true,
            }, new VoiceRoutingContext(player.Id, UiVoice.Settings.EffectiveFrequency));
            _voiceOpening = false;
        }

        _voiceRefresh += delta;
        if (_voiceRefresh < .5) return;
        _voiceRefresh = 0;
        ObserveVoiceMilestones();
    }

    private void PublishReconnaissanceRequiredCue()
    {
        if (_galaxy is null || UiVoice is null) return;
        var player = PlayerCivilization; var scope = VoiceScope;
        _voiceEvents?.Emit(new GameplayVoiceEvent("exploration.system.reconnaissance_required", player.Id,
            $"reconnaissance-required:{scope.SimulationTick}:{++_reconnaissanceRequiredClickRevision}", new Dictionary<string, string>(), scope.SimulationTick, scope.SimulationDate)
        { SourceSpeciesId = player.SpeciesId }, new VoiceRoutingContext(player.Id, UiVoice.Settings.EffectiveFrequency));
    }

    private GameplayVoiceRoutingScope VoiceScope => new(
        _galaxy.PlayerCivilizationId,
        UiVoice?.Settings.EffectiveFrequency ?? VoiceFrequency.Normal,
        DiplomacyCampaignClock.FromSimulationDays(_clock.SimulationDays),
        CampaignCalendar.FormatDate(_clock.SimulationDays));

    private void ObserveVoiceMilestones()
    {
        if (_galaxy is null || UiVoice is null) return;
        var player = PlayerCivilization;
        var scope = VoiceScope;
        var fleets = _galaxy.Fleets
            .Where(fleet => fleet.IsActive && fleet.CivilizationId == player.Id)
            .ToArray();

        foreach (var fleet in fleets)
        {
            var transit = fleet.CurrentSystemId is null && fleet.DestinationSystemId.HasValue;
            if (_voiceBaseline && _voiceFleetTransit.TryGetValue(fleet.Id, out var previous))
            {
                if (!previous && transit)
                {
                    var first = !_voiceHasInterstellarLaunch;
                    _gameplayVoice?.PublishShipDeparture(fleet, player.SpeciesId, scope, first);
                    _voiceHasInterstellarLaunch = true;
                }
                else if (previous && !transit && fleet.CurrentSystemId is int arrivalSystemId)
                {
                    var firstExtrasolar = arrivalSystemId != player.HomeSystemId && !_voiceHasExtrasolarArrival;
                    _gameplayVoice?.PublishSystemReached(fleet, arrivalSystemId,
                        KnownSystemVoiceName(arrivalSystemId), player.SpeciesId, scope, firstExtrasolar);
                    if (arrivalSystemId != player.HomeSystemId) _voiceHasExtrasolarArrival = true;
                }
            }
            _voiceFleetTransit[fleet.Id] = transit;
        }
        foreach (var stale in _voiceFleetTransit.Keys.Where(id => fleets.All(fleet => fleet.Id != id)).ToArray())
            _voiceFleetTransit.Remove(stale);

        var combat = _coreSimulation.GetOwnCombatFleetStatus(_galaxy, player.Id);
        foreach (var fleet in combat.Fleets)
        {
            if (fleet.HullIntegrityRatio > .25)
            {
                _voiceCriticalHulls.Remove(fleet.FleetId);
                continue;
            }
            if (_voiceCriticalHulls.Add(fleet.FleetId) && _voiceBaseline)
                _gameplayVoice?.PublishHullCritical(player.Id, player.SpeciesId, fleet.FleetId,
                    fleet.FleetName, scope);
        }
        _voiceCriticalHulls.RemoveWhere(id => combat.Fleets.All(fleet => fleet.FleetId != id));

        ObserveDiplomaticVoice(player.Id, player.SpeciesId, scope);
        ObserveLogisticsVoice(player.Id, player.SpeciesId, scope);
        if (_voiceBaseline) RouteEconomyVoice();
        _voiceBaseline = true;
    }

    private void ObserveDiplomaticVoice(int playerId, string playerSpeciesId, GameplayVoiceRoutingScope scope)
    {
        // This view contains only proposals and history the player is authorized to inspect.
        var view = new ObserverDiplomacyCommandService(_diplomacyState).BuildView(playerId);
        foreach (var proposal in view.Proposals.Where(proposal => proposal.RecipientCivilizationId == playerId))
        {
            if (!_voiceProposals.Add(proposal.ProposalId) || !_voiceBaseline) continue;
            var source = _galaxy.Civilizations.FirstOrDefault(civilization =>
                civilization.Id == proposal.ProposerCivilizationId);
            if (source is not null)
                _gameplayVoice?.PublishAlienTransmission(proposal, source.SpeciesId, scope);
        }
        _voiceProposals.RemoveWhere(id => view.Proposals.All(proposal => proposal.ProposalId != id));

        foreach (var diplomaticEvent in view.RecentEvents)
        {
            if (!_voiceDiplomacyEvents.Add(diplomaticEvent.EventId)) continue;
            if (diplomaticEvent.PrimaryCivilizationId != playerId &&
                diplomaticEvent.SecondaryCivilizationId != playerId) continue;
            if (!_voiceBaseline)
            {
                if (diplomaticEvent.Kind == DiplomaticEventKind.WarDeclared) _voiceHasWarDeclaration = true;
                continue;
            }

            if (diplomaticEvent.Kind == DiplomaticEventKind.WarDeclared)
            {
                var enemyId = diplomaticEvent.PrimaryCivilizationId == playerId
                    ? diplomaticEvent.SecondaryCivilizationId
                    : diplomaticEvent.PrimaryCivilizationId;
                var enemyName = enemyId is int id
                    ? _galaxy.Civilizations.FirstOrDefault(civilization => civilization.Id == id)?.Name
                    : null;
                if (string.IsNullOrWhiteSpace(enemyName)) continue;
                _gameplayVoice?.PublishWarDeclared(diplomaticEvent, playerId, playerSpeciesId,
                    enemyName, scope, !_voiceHasWarDeclaration);
                _voiceHasWarDeclaration = true;
            }
            else if (diplomaticEvent.Kind is DiplomaticEventKind.ProposalRejected or
                     DiplomaticEventKind.AgreementActivated or DiplomaticEventKind.BorderWarningIssued)
            {
                _gameplayVoice?.PublishDiplomaticTransition(diplomaticEvent, playerId,
                    playerSpeciesId, scope);
            }
        }
        _voiceDiplomacyEvents.RemoveWhere(id => view.RecentEvents.All(value => value.EventId != id));
    }

    private void ObserveLogisticsVoice(int playerId, string playerSpeciesId, GameplayVoiceRoutingScope scope)
    {
        var logistics = new PrototypeEconomyLogisticsView().GetSnapshot(_galaxy, playerId);
        var criticalSystems = logistics.Colonies
            .Where(colony => colony.Condition == SupplyCondition.Critical)
            .GroupBy(colony => colony.SystemId)
            .ToArray();
        foreach (var system in criticalSystems)
        {
            if (!_voiceCriticalLogisticsSystems.Add(system.Key) || !_voiceBaseline) continue;
            var name = _galaxy.Systems.FirstOrDefault(candidate => candidate.Id == system.Key)?.Name
                ?? $"system {system.Key + 1:000}";
            var shortfall = system.Sum(colony => colony.ImportedSupportRequiredPerDay);
            var detail = $"Local support is short by {shortfall:0.##} units per day.";
            _gameplayVoice?.PublishLogisticsCritical(playerId, playerSpeciesId, system.Key, name, detail, scope);
        }
        _voiceCriticalLogisticsSystems.RemoveWhere(id => criticalSystems.All(system => system.Key != id));
    }

    private void BaselineFirstOccurrenceState()
    {
        if (_galaxy is null) return;
        var player = PlayerCivilization;
        var hasReconnoiteredExtrasolarSystem = _galaxy.Knowledge.GetKnownSystems(player.Id).Any(systemId =>
            systemId != player.HomeSystemId &&
            _galaxy.Knowledge.GetSystemSurveyLevel(player.Id, systemId) >=
            Game.Simulation.Knowledge.SystemSurveyLevel.PartiallySurveyed);
        _voiceHasInterstellarLaunch = _galaxy.Fleets.Any(fleet => fleet.CivilizationId == player.Id &&
            ((fleet.CurrentSystemId is int current && current != player.HomeSystemId) ||
             (fleet.DestinationSystemId is int destination && destination != player.HomeSystemId))) ||
            _galaxy.Colonies.Any(colony => colony.CivilizationId == player.Id && colony.SystemId != player.HomeSystemId) ||
            hasReconnoiteredExtrasolarSystem;
        _voiceHasExtrasolarArrival = _galaxy.Fleets.Any(fleet => fleet.CivilizationId == player.Id &&
            fleet.CurrentSystemId is int current && current != player.HomeSystemId) ||
            _galaxy.Colonies.Any(colony => colony.CivilizationId == player.Id && colony.SystemId != player.HomeSystemId) ||
            hasReconnoiteredExtrasolarSystem;
        _voiceHasFirstContact = _galaxy.Civilizations.Any(civilization => civilization.Id != player.Id &&
            _galaxy.Knowledge.IsCivilizationKnown(player.Id, civilization.Id));
        _voiceHasWarDeclaration = new ObserverDiplomacyCommandService(_diplomacyState).BuildView(player.Id)
            .RecentEvents.Any(value => value.Kind == DiplomaticEventKind.WarDeclared &&
                (value.PrimaryCivilizationId == player.Id || value.SecondaryCivilizationId == player.Id));
    }

    private void BaselineVoiceResearch()
    {
        _voiceMatureResearch.Clear();
        if (_adaptiveResearch is null || _galaxy is null) return;
        foreach (var state in _adaptiveResearch.GetCivilization(_galaxy.PlayerCivilizationId).NodeStates.Values)
            if (state.CountsAsEstablishedKnowledge) _voiceMatureResearch.Add(state.NodeId);
    }

    private void RouteAdaptiveResearchVoice(string nodeId)
    {
        if (_adaptiveResearch is null || !_adaptiveResearch.GetCivilization(_galaxy.PlayerCivilizationId)
                .TryGetNodeState(nodeId, out var state) || !state.CountsAsEstablishedKnowledge ||
            !_voiceMatureResearch.Add(nodeId)) return;
        var player = PlayerCivilization;
        var catalog = _adaptiveResearch.Runtime.Authority.Catalog;
        var node = catalog.Nodes.TryGetValue(nodeId, out var definition) ? definition : null;
        var name = node?.Name ?? "Current research program";
        var major = node?.DeclaredCapabilities.Any(capability => capability is
            "ftl_prototype" or "experimental_interstellar_transit" or "reliable_ftl" or
            "interstellar_transit" or "extended_ftl_range" or "extended_interstellar_transit" or
            "infrastructure_ftl_transit" or "interstellar_gateway_network") == true;
        _gameplayVoice?.PublishAdaptiveResearch(player.Id, player.SpeciesId, nodeId, name, major, VoiceScope);
    }

    private void RouteResearchVoice(ResearchEvent researchEvent)
    {
        var technology = TechnologyRegistry.Get(researchEvent.TechnologyId);
        _gameplayVoice?.PublishResearch(researchEvent, technology, PlayerCivilization.SpeciesId, VoiceScope);
    }

    private void RouteConstructionVoice(ConstructionEvent constructionEvent)
    {
        var project = ConstructionRegistry.Get(constructionEvent.ProjectId);
        _gameplayVoice?.PublishConstruction(constructionEvent, project, PlayerCivilization.SpeciesId, VoiceScope);
    }

    private void RouteShipCompletedVoice(ShipbuildingEvent shipEvent)
    {
        var fleet = _galaxy.Fleets.FirstOrDefault(candidate => candidate.Id == shipEvent.FleetId &&
            candidate.CivilizationId == shipEvent.CivilizationId);
        if (fleet is null || !ShipDesignRegistry.TryGet(shipEvent.DesignId, out var design) || design is null) return;
        _gameplayVoice?.PublishShipCompleted(shipEvent, fleet, design, PlayerCivilization.SpeciesId, VoiceScope);
    }

    private void RouteExplorationVoice(ExplorationEvent explorationEvent)
    {
        if (explorationEvent.CivilizationId != _galaxy.PlayerCivilizationId) return;
        if (explorationEvent.Type is not (ExplorationEventType.SystemSurveyed or
            ExplorationEventType.AnomalySignatureDetected or
            ExplorationEventType.AnomalySurveyed or ExplorationEventType.ActivitySignatureDetected or
            ExplorationEventType.FirstContact)) return;

        var observerView = _explorationReadModel.Build(_galaxy, _galaxy.PlayerCivilizationId);
        var knownSystem = observerView.KnownSystems.FirstOrDefault(system => system.SystemId == explorationEvent.SystemId);
        var systemName = knownSystem?.CatalogName ?? PublicCatalogSystemName(explorationEvent.SystemId);
        var bodyName = explorationEvent.PlanetaryBodyId is int bodyId
            ? knownSystem?.PlanetaryBodies.FirstOrDefault(body => body.BodyId == bodyId)?.Name
            : null;
        var targetName = explorationEvent.TargetCivilizationId is int civilizationId &&
                         _galaxy.Knowledge.IsCivilizationKnown(_galaxy.PlayerCivilizationId, civilizationId)
            ? _galaxy.Civilizations.FirstOrDefault(civilization => civilization.Id == civilizationId)?.Name
            : null;
        var first = explorationEvent.Type switch
        {
            ExplorationEventType.FirstContact => !_voiceHasFirstContact,
            _ => false,
        };
        _gameplayVoice?.PublishExploration(explorationEvent, PlayerCivilization.SpeciesId, VoiceScope,
            systemName, bodyName, targetName, first);
        if (explorationEvent.Type == ExplorationEventType.FirstContact) _voiceHasFirstContact = true;
    }

    private string KnownSystemVoiceName(int systemId)
    {
        var known = _explorationReadModel.Build(_galaxy, _galaxy.PlayerCivilizationId)
            .KnownSystems.FirstOrDefault(system => system.SystemId == systemId);
        return known?.CatalogName ?? PublicCatalogSystemName(systemId);
    }

    /// <summary>The star catalog is public before survey; this intentionally exposes no
    /// worlds, occupants, or other observer-only facts.</summary>
    private string PublicCatalogSystemName(int systemId) => _galaxy.Systems
        .FirstOrDefault(system => system.Id == systemId)?.Name ?? "Unknown system";

    private void RouteColonizationVoice(ColonizationEvent colonizationEvent)
    {
        var colony = _galaxy.Colonies.FirstOrDefault(candidate => candidate.Id == colonizationEvent.ColonyId &&
            candidate.CivilizationId == colonizationEvent.CivilizationId);
        if (colony is null) return;
        var planetName = _galaxy.PlanetaryBodies.FirstOrDefault(body => body.Id == colony.PlanetaryBodyId)?.Name
            ?? colony.Name;
        var firstExtrasolar = _galaxy.Colonies.Count(candidate => candidate.CivilizationId == colony.CivilizationId &&
            candidate.SystemId != PlayerCivilization.HomeSystemId) == 1;
        _gameplayVoice?.PublishColonyFounded(colonizationEvent, colony, planetName,
            PlayerCivilization.SpeciesId, VoiceScope, firstExtrasolar);
    }

    private void RouteCombatVoice(CombatEvent combatEvent)
    {
        var playerId = _galaxy.PlayerCivilizationId;
        var systemName = combatEvent.SystemId is int systemId
            ? KnownSystemVoiceName(systemId)
            : "deep space";
        var speciesId = PlayerCivilization.SpeciesId;
        var scope = VoiceScope;

        switch (combatEvent.Type)
        {
            case CombatEventType.EngagementStarted:
            {
                var fleetId = combatEvent.TargetCivilizationId == playerId
                    ? combatEvent.TargetFleetId
                    : combatEvent.ActorCivilizationId == playerId ? combatEvent.ActorFleetId : null;
                var fleetName = fleetId is int id ? OwnFleetName(id) : null;
                if (!string.IsNullOrWhiteSpace(fleetName))
                    _gameplayVoice?.PublishFleetAttacked(combatEvent, playerId, speciesId,
                        fleetName, systemName, scope);
                break;
            }
            case CombatEventType.FleetRetreatInitiated when combatEvent.ActorCivilizationId == playerId:
            {
                var fleetName = OwnFleetName(combatEvent.ActorFleetId);
                if (!string.IsNullOrWhiteSpace(fleetName))
                    _gameplayVoice?.PublishFleetRetreat(combatEvent, playerId, speciesId,
                        fleetName, systemName, scope);
                break;
            }
            case CombatEventType.FleetDestroyed when combatEvent.TargetCivilizationId == playerId &&
                                                     combatEvent.TargetFleetId is int fleetId:
            {
                var fleetName = OwnFleetName(fleetId);
                if (!string.IsNullOrWhiteSpace(fleetName))
                    _gameplayVoice?.PublishFleetDestroyed(combatEvent, playerId, speciesId,
                        fleetName, systemName, scope);
                break;
            }
            case CombatEventType.EngagementEnded when combatEvent.ActorCivilizationId == playerId ||
                                                       combatEvent.TargetCivilizationId == playerId:
                _gameplayVoice?.PublishEngagementConcluded(combatEvent, playerId, speciesId,
                    systemName, scope);
                break;
        }
    }

    private string? OwnFleetName(int fleetId) => _galaxy.Fleets
        .FirstOrDefault(fleet => fleet.Id == fleetId && fleet.CivilizationId == _galaxy.PlayerCivilizationId)?.Name;

    private void RouteEconomyVoice()
    {
        var player = PlayerCivilization;
        var health = TreasuryHealth.Assess(PlayerEconomy.Credits, PlayerEconomy.LastCreditsPerSecond,
            PlayerEconomy.OperatingArrears);
        if (health.State is not (TreasuryHealthState.Arrears or TreasuryHealthState.Depleted) &&
            !(health.State == TreasuryHealthState.Deficit && health.RunwayDays <= 30)) return;
        var amount = PlayerEconomy.Credits.ToString("0.##", CultureInfo.InvariantCulture);
        _gameplayVoice?.PublishEconomyCritical(player.Id, player.SpeciesId, amount, VoiceScope);
    }
}
