using System;
using System.Collections.Generic;
using System.Linq;
using Godot;
using Game.Simulation.Diplomacy;
using Game.Simulation.Time;

namespace Game.Presentation;

/// <summary>Observer-safe adapters for the full Relations workspace. No UI-side diplomacy rules.</summary>
public partial class Main
{
    public bool UiIsDiplomacyOpen => GetNodeOrNull<RelationsPanel>("RelationsPanel")?.IsOpen == true;
    public string UiDiplomacyDate => CampaignCalendar.FormatDate(_clock.SimulationDays);
    public DiplomaticStateView GetUiDiplomacyView() =>
        new ObserverDiplomacyCommandService(_diplomacyState).BuildView(_galaxy?.PlayerCivilizationId ?? 0);

    public string UiKnownDiplomaticName(int targetId) =>
        GetUiDiplomacyView().Contacts.Any(c => c.TargetCivilizationId == targetId)
            ? _galaxy.Civilizations.FirstOrDefault(c => c.Id == targetId)?.Name ?? "Identified civilization"
            : "Unknown contact";

    public string UiKnownDiplomaticSystemName(int systemId)
    {
        var view = GetUiDiplomacyView();
        if (!view.Contacts.Any(c => c.LastObservedSystemId == systemId) &&
            !_galaxy.Knowledge.GetKnownSystems(view.ObserverCivilizationId).Contains(systemId))
            return "Unknown system";
        return _explorationReadModel.Build(_galaxy, view.ObserverCivilizationId).KnownSystems
            .FirstOrDefault(s => s.SystemId == systemId)?.CatalogName ?? "Unidentified system";
    }

    public void UiFocusDiplomaticObservation(int systemId)
    {
        var view = GetUiDiplomacyView();
        if (!view.Contacts.Any(c => c.LastObservedSystemId == systemId)) return;
        UiSelectSystem(systemId, "Last observed contact position. This does not reveal current foreign activity.");
    }

    public string? UiKnownDiplomaticSpeciesName(int civilizationId)
    {
        if (!GetUiDiplomacyView().Contacts.Any(c => c.TargetCivilizationId == civilizationId)) return null;
        var species = _galaxy.Civilizations.FirstOrDefault(c => c.Id == civilizationId)?.SpeciesId;
        return species is not null && Game.Simulation.Species.SpeciesCatalog.TryGet(species, out var definition)
            ? definition!.DisplayName : null;
    }

    public void UiOpenDiplomaticContact(int civilizationId) =>
        GetNodeOrNull<RelationsPanel>("RelationsPanel")?.SelectCivilization(civilizationId);

    public string IssueUiDiplomacyWorkspaceAction(int target, string action)
    {
        if (_galaxy is null) return "Diplomacy is not ready.";
        return action switch
        {
            "communication" => new ObserverDiplomacyCommandService(_diplomacyState).EstablishCommunication(
                _galaxy.PlayerCivilizationId, target, DiplomacyCampaignClock.FromSimulationDays(_clock.SimulationDays)).Message,
            "nonaggression" => IssueUiDiplomacyProposal(target, UiDiplomacyProposalAction.NonAggression),
            "access" => IssueUiDiplomacyProposal(target, UiDiplomacyProposalAction.AccessRequest),
            "peace" => IssueUiDiplomacyProposal(target, UiDiplomacyProposalAction.PeaceOffer),
            "ceasefire" => IssueUiDiplomacyProposal(target, UiDiplomacyProposalAction.CeasefireOffer),
            "grant" => IssueUiDiplomacyAccess(target, true),
            "deny" => IssueUiDiplomacyAccess(target, false),
            "war" => IssueUiWarDeclaration(target),
            _ => "This diplomatic action is not supported.",
        };
    }

    // This hook is observer-scoped. Future news consumers must not treat it as public intelligence.
    public event Action<ObserverDiplomaticBulletin>? DiplomaticBulletin;
    private DiplomacyState? _bulletinState;
    private readonly HashSet<long> _bulletinSeen = new();
    private double _bulletinTimer;
    protected void RefreshDiplomacyWorkspaceEvents(double delta)
    {
        if (_galaxy is null) return;
        _bulletinTimer += delta;
        if (_bulletinTimer < .5) return;
        _bulletinTimer = 0;
        var view = GetUiDiplomacyView();
        if (!ReferenceEquals(_bulletinState, _diplomacyState))
        {
            _bulletinState = _diplomacyState;
            _bulletinSeen.Clear();
            foreach (var e in view.RecentEvents) _bulletinSeen.Add(e.EventId);
            return;
        }
        foreach (var e in view.RecentEvents)
        {
            if (!_bulletinSeen.Add(e.EventId)) continue;
            if (e.Kind is not (DiplomaticEventKind.ContactEstablished or DiplomaticEventKind.CommunicationAvailable
                or DiplomaticEventKind.ProposalSent or DiplomaticEventKind.ProposalAccepted
                or DiplomaticEventKind.ProposalRejected or DiplomaticEventKind.AgreementActivated
                or DiplomaticEventKind.AgreementTerminated or DiplomaticEventKind.WarDeclared)) continue;
            int? counterpart = e.PrimaryCivilizationId == view.ObserverCivilizationId ? e.SecondaryCivilizationId :
                e.SecondaryCivilizationId == view.ObserverCivilizationId ? e.PrimaryCivilizationId : null;
            var knownCounterpart = counterpart is int id && view.Contacts.Any(c => c.TargetCivilizationId == id) ? counterpart : null;
            _playerNotifications.Publish("Diplomacy", CampaignCalendar.FormatDate(e.Tick / 1000.0), e.Summary, knownCounterpart);
            DiplomaticBulletin?.Invoke(new(view.ObserverCivilizationId, e.EventId, e.Kind, e.Tick, e.Summary, knownCounterpart));
        }
        _bulletinSeen.RemoveWhere(id => view.RecentEvents.All(e => e.EventId != id));
    }

}

public sealed record ObserverDiplomaticBulletin(int ObserverId, long EventId, DiplomaticEventKind Kind, long Tick, string Summary, int? CounterpartId);
