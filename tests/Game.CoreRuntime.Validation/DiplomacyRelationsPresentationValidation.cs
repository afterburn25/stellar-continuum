using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using Game.Presentation;
using Game.Simulation.Diplomacy;

namespace Game.CoreRuntime.Validation;

internal static class DiplomacyRelationsPresentationValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidateUnidentifiedContactDoesNotResolveIdentity();
        ValidateIdentifiedContactUsesOnlyObserverVisiblePairState();
        Console.WriteLine("PASS: observer-safe Diplomacy relations presentation");
    }

    private static void ValidateUnidentifiedContactDoesNotResolveIdentity()
    {
        var view = new DiplomaticStateView(
            1,
            new[]
            {
                new DiplomaticContactView(
                    "signal-unknown",
                    null,
                    ContactAwareness.DetectedUnidentified,
                    ContactCondition.Active,
                    false,
                    0.42,
                    1200,
                    9),
            },
            Array.Empty<DiplomaticRelationshipView>(),
            Array.Empty<DiplomaticAccessSnapshot>(),
            Array.Empty<TerritorialClaimSnapshot>(),
            Array.Empty<TerritorialClaimResponseSnapshot>(),
            Array.Empty<DiplomaticAgreementSnapshot>(),
            Array.Empty<DiplomaticProposalSnapshot>(),
            Array.Empty<DiplomaticHistoryEventSnapshot>());

        var presenter = new DiplomacyRelationsPresenter();
        var state = presenter.Build(
            view,
            0,
            0,
            _ => throw new InvalidOperationException("Unidentified contact attempted a hidden civilization-name lookup."));

        Require(state.TargetCivilizationId is null, "unidentified contact exposed a target civilization");
        Require(!state.HasVisibleCommunication, "unidentified contact exposed communication");
        Require(!state.CanOfferNonAggression && !state.CanRequestAccess && !state.CanOfferPeace && !state.CanOfferCeasefire &&
            !state.CanSetAccess && !state.CanDeclareWar,
            "unidentified contact exposed diplomatic commands");
        Require(state.Details.Contains("Unidentified contact", StringComparison.Ordinal), "unidentified contact was not described safely");
        Require(state.ContactName.StartsWith("UNIDENTIFIED CONTACT", StringComparison.Ordinal) &&
            state.PoliticalStatus == "Identity unknown" && state.Trust is null &&
            state.RecentEvents.Length == 0,
            "unidentified graphical dossier inferred hidden diplomatic state");
    }

    private static void ValidateIdentifiedContactUsesOnlyObserverVisiblePairState()
    {
        var resolvedIds = new List<int>();
        var visibleHistory = "Visible bilateral event";
        var hiddenHistory = "HIDDEN THIRD PARTY EVENT";

        var view = new DiplomaticStateView(
            1,
            new[]
            {
                new DiplomaticContactView(
                    "civilization:2",
                    2,
                    ContactAwareness.CommunicationAvailable,
                    ContactCondition.Active,
                    true,
                    1.0,
                    5000,
                    12),
            },
            new[]
            {
                new DiplomaticRelationshipView(
                    2,
                    DiplomaticPoliticalState.Peace,
                    0.6,
                    0.1,
                    0.2,
                    0.5,
                    0.4,
                    Array.Empty<DiplomaticGrievanceSnapshot>()),
                new DiplomaticRelationshipView(
                    99,
                    DiplomaticPoliticalState.AtWar,
                    0.0,
                    1.0,
                    1.0,
                    0.0,
                    0.0,
                    Array.Empty<DiplomaticGrievanceSnapshot>()),
            },
            new[]
            {
                new DiplomaticAccessSnapshot(1, 2, AccessPermission.Granted, 4900),
                new DiplomaticAccessSnapshot(2, 1, AccessPermission.Denied, 4901),
                new DiplomaticAccessSnapshot(99, 100, AccessPermission.Granted, 4902),
            },
            Array.Empty<TerritorialClaimSnapshot>(),
            Array.Empty<TerritorialClaimResponseSnapshot>(),
            new[]
            {
                new DiplomaticAgreementSnapshot(
                    4,
                    1,
                    2,
                    DiplomaticAgreementType.NonAggression,
                    DiplomaticAgreementStatus.Active,
                    4000,
                    null,
                    null),
                new DiplomaticAgreementSnapshot(
                    88,
                    99,
                    100,
                    DiplomaticAgreementType.Ceasefire,
                    DiplomaticAgreementStatus.Active,
                    4000,
                    null,
                    null),
            },
            new[]
            {
                new DiplomaticProposalSnapshot(
                    7,
                    2,
                    1,
                    DiplomaticProposalKind.AccessRequest,
                    null,
                    DiplomaticProposalStatus.Pending,
                    4950,
                    null,
                    "Visible incoming access request.",
                    null),
                new DiplomaticProposalSnapshot(
                    99,
                    99,
                    100,
                    DiplomaticProposalKind.Demand,
                    null,
                    DiplomaticProposalStatus.Pending,
                    4951,
                    null,
                    "HIDDEN THIRD PARTY PROPOSAL",
                    null),
            },
            new[]
            {
                new DiplomaticHistoryEventSnapshot(
                    3,
                    4990,
                    DiplomaticEventKind.ProposalSent,
                    2,
                    1,
                    null,
                    visibleHistory,
                    new[] { 1, 2 }),
                new DiplomaticHistoryEventSnapshot(
                    30,
                    4991,
                    DiplomaticEventKind.WarDeclared,
                    99,
                    100,
                    null,
                    hiddenHistory,
                    new[] { 99, 100 }),
            });

        var presenter = new DiplomacyRelationsPresenter();
        var state = presenter.Build(
            view,
            0,
            0,
            id =>
            {
                resolvedIds.Add(id);
                return id == 2 ? "Known Two" : $"Unexpected {id}";
            });

        Require(resolvedIds.Count == 1 && resolvedIds[0] == 2, "relations presenter resolved an unrelated civilization identity");
        Require(state.TargetCivilizationId == 2, "identified target changed");
        Require(state.HasVisibleCommunication, "visible active communication was lost");
        Require(state.ProposalCount == 1 && state.PendingProposalId == 7, "unrelated proposal leaked into selected pair");
        Require(state.CanAcceptProposal && state.CanRejectProposal && !state.CanWithdrawProposal, "incoming proposal role was misclassified");
        Require(!state.CanOfferNonAggression, "active non-aggression agreement did not suppress duplicate offer");
        Require(state.CanRequestAccess, "denied inbound access should remain requestable");
        Require(state.CanSetAccess, "active communication did not expose own access policy control");
        Require(state.CanDeclareWar, "identified non-wartime contact did not expose war declaration availability");
        Require(state.Details.Contains("Known Two", StringComparison.Ordinal), "identified target name was not rendered");
        Require(state.Details.Contains(visibleHistory, StringComparison.Ordinal), "visible bilateral history was omitted");
        Require(!state.Details.Contains(hiddenHistory, StringComparison.Ordinal), "unrelated history leaked into relations details");
        Require(!state.Details.Contains("HIDDEN THIRD PARTY PROPOSAL", StringComparison.Ordinal), "unrelated proposal summary leaked into relations details");
        Require(state.ContactName == "KNOWN TWO" && state.PoliticalStatus == "Peace" &&
            state.Trust == 0.6 && state.Hostility == 0.1 && state.Fear == 0.2 &&
            state.Respect == 0.5 && state.Cooperation == 0.4 &&
            state.AccessSummary.Contains("Denied", StringComparison.Ordinal) &&
            state.AgreementsSummary.Contains("NonAggression", StringComparison.Ordinal) &&
            state.ProposalSummary.Contains("Visible incoming access request", StringComparison.Ordinal) &&
            state.RecentEvents.SequenceEqual(new[] { visibleHistory }) &&
            !state.ProposalSummary.Contains("HIDDEN", StringComparison.Ordinal) &&
            !state.RecentEvents.Contains(hiddenHistory),
            "graphical diplomatic dossier changed or leaked observer-visible pair state");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
