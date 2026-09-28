using System;
using System.Linq;
using Game.Simulation.Diplomacy;

namespace Game.Presentation;

public enum UiDiplomacyProposalAction
{
    NonAggression,
    AccessRequest,
    PeaceOffer,
    CeasefireOffer,
}

/// <summary>
/// Player-facing Diplomacy adapter. Reads are shaped from the observer-safe view and every
/// mutation is revalidated through ObserverDiplomacyCommandService at click time.
/// </summary>
public partial class Main
{
    private readonly DiplomacyRelationsPresenter _diplomacyRelationsPresenter = new();

    public RelationsPresentationState GetUiRelationsState(int contactIndex, int proposalIndex)
    {
        if (_galaxy is null)
        {
            return new RelationsPresentationState(
                0, 0, 0, 0,
                "Diplomatic contacts are initializing…",
                null, false, null,
                false, false, false,
                false, false, false, false, false);
        }

        var playerId = _galaxy.PlayerCivilizationId;
        var gateway = new ObserverDiplomacyCommandService(_diplomacyState);
        var view = gateway.BuildView(playerId);
        var state = _diplomacyRelationsPresenter.Build(
            view,
            contactIndex,
            proposalIndex,
            targetId => _galaxy.Civilizations.FirstOrDefault(civilization => civilization.Id == targetId)?.Name
                        ?? $"Civilization {targetId}");
        return state.TargetCivilizationId is int identifiedId
            ? state with { SpeciesId = _galaxy.Civilizations.First(civilization => civilization.Id == identifiedId).SpeciesId }
            : state;
    }

    public void UiToggleRelationsPanel()
    {
        var sidebar = GetNodeOrNull<CampaignSidebar>("CampaignSidebar");
        if (sidebar?.IsDrawerOpen == true && sidebar.ActiveSection == "relations")
            sidebar.CloseDrawer();
        else
            sidebar?.ShowSection("relations");
    }

    public string IssueUiDiplomacyProposal(int targetCivilizationId, UiDiplomacyProposalAction action)
    {
        if (_galaxy is null)
            return "Diplomacy is not ready.";

        var playerId = _galaxy.PlayerCivilizationId;
        var tick = DiplomacyCampaignClock.FromSimulationDays(_clock.SimulationDays);
        var gateway = new ObserverDiplomacyCommandService(_diplomacyState);

        var result = action switch
        {
            UiDiplomacyProposalAction.NonAggression => gateway.SendProposal(
                playerId,
                targetCivilizationId,
                DiplomaticProposalKind.Agreement,
                tick,
                "Proposal for a non-aggression agreement.",
                DiplomaticAgreementType.NonAggression),
            UiDiplomacyProposalAction.AccessRequest => gateway.SendProposal(
                playerId,
                targetCivilizationId,
                DiplomaticProposalKind.AccessRequest,
                tick,
                "Request for transit access."),
            UiDiplomacyProposalAction.PeaceOffer => gateway.SendProposal(
                playerId,
                targetCivilizationId,
                DiplomaticProposalKind.PeaceOffer,
                tick,
                "Offer to establish peace."),
            UiDiplomacyProposalAction.CeasefireOffer => gateway.SendProposal(
                playerId,
                targetCivilizationId,
                DiplomaticProposalKind.CeasefireOffer,
                tick,
                "Offer to establish a ceasefire."),
            _ => new ObserverDiplomacyCommandResult(
                false,
                ObserverDiplomacyCommandStatus.InvalidRequest,
                "The diplomatic request is not valid."),
        };

        return result.Message;
    }

    public string IssueUiDiplomacyProposalResponse(long proposalId, bool accept)
    {
        if (_galaxy is null)
            return "Diplomacy is not ready.";

        var gateway = new ObserverDiplomacyCommandService(_diplomacyState);
        var result = gateway.RespondToProposal(
            _galaxy.PlayerCivilizationId,
            proposalId,
            accept,
            DiplomacyCampaignClock.FromSimulationDays(_clock.SimulationDays));
        return result.Message;
    }

    public string IssueUiDiplomacyProposalWithdrawal(long proposalId)
    {
        if (_galaxy is null)
            return "Diplomacy is not ready.";

        var gateway = new ObserverDiplomacyCommandService(_diplomacyState);
        var result = gateway.WithdrawProposal(
            _galaxy.PlayerCivilizationId,
            proposalId,
            DiplomacyCampaignClock.FromSimulationDays(_clock.SimulationDays));
        return result.Message;
    }

    public string IssueUiDiplomacyAccess(int targetCivilizationId, bool grant)
    {
        if (_galaxy is null)
            return "Diplomacy is not ready.";

        var gateway = new ObserverDiplomacyCommandService(_diplomacyState);
        var result = gateway.SetAccessPermission(
            _galaxy.PlayerCivilizationId,
            targetCivilizationId,
            grant ? AccessPermission.Granted : AccessPermission.Denied,
            DiplomacyCampaignClock.FromSimulationDays(_clock.SimulationDays));
        return result.Message;
    }

    public string IssueUiWarDeclaration(int targetCivilizationId)
    {
        if (_galaxy is null) return "Diplomacy is not ready.";
        var gateway = new ObserverDiplomacyCommandService(_diplomacyState);
        var result = gateway.DeclareWar(_galaxy.PlayerCivilizationId, targetCivilizationId,
            DiplomacyCampaignClock.FromSimulationDays(_clock.SimulationDays));
        return result.Message;
    }
}
