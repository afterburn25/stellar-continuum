using Game.Presentation;
using Game.Simulation.Diplomacy;

static class Program
{
    static int Main()
    {
        var tests = new (string, Action)[]
        {
            ("unknown contacts remain anonymous", UnknownContactsRemainAnonymous),
            ("unknown contacts expose no relationships", UnknownContactsExposeNoRelationships),
            ("selected proposal flags follow observer direction", ProposalDirectionIsObserverSafe),
            ("observer command lifecycle remains visible and directional", ObserverCommandLifecycle),
            ("multiple contact filters and proposal privacy", MultiContactModelIsolation),
        };
        foreach (var (name, test) in tests)
        {
            test(); Console.WriteLine($"PASS {name}");
        }
        Console.WriteLine($"{tests.Length} diplomacy workspace checks passed");
        return 0;
    }

    static void UnknownContactsRemainAnonymous()
    {
        var state = View(new DiplomaticContactView("contact-7", null, ContactAwareness.ContactEstablished,
            ContactCondition.Active, false, .42, 4, 12));
        var result = new DiplomacyRelationsPresenter().Build(state, 0, 0, _ => throw new Exception("hidden name requested"));
        Require(result.ContactName == "UNIDENTIFIED CONTACT contact-7", result.ContactName);
        Require(result.TargetCivilizationId is null && result.Trust is null, "hidden target data leaked");
    }

    static void UnknownContactsExposeNoRelationships()
    {
        var state = View(new DiplomaticContactView("contact-8", null, ContactAwareness.DetectedUnidentified,
            ContactCondition.Active, false, .11, 2, null), new DiplomaticRelationshipView(99,
            DiplomaticPoliticalState.AtWar, 1, 1, 1, 1, 1, Array.Empty<DiplomaticGrievanceSnapshot>()));
        var result = new DiplomacyRelationsPresenter().Build(state, 0, 0, _ => "SECRET");
        Require(result.Trust is null && result.PoliticalStatus != "AtWar", "unpaired relationship leaked");
    }

    static void ProposalDirectionIsObserverSafe()
    {
        var proposal = new DiplomaticProposalSnapshot(1, 2, 7, DiplomaticProposalKind.Agreement,
            DiplomaticAgreementType.NonAggression, DiplomaticProposalStatus.Pending, 1, null, "request", null);
        var state = View(new DiplomaticContactView("known", 2, ContactAwareness.Identified,
            ContactCondition.Active, true, 1, 3, null), proposals: new[] { proposal });
        var result = new DiplomacyRelationsPresenter().Build(state, 0, 0, _ => "Known");
        Require(result.PendingProposalId == 1 && result.CanAcceptProposal, "incoming proposal not exposed as accept");
        Require(!result.CanWithdrawProposal, "incoming proposal exposed as withdraw");
    }

    static void ObserverCommandLifecycle()
    {
        var state = new DiplomacyState();
        var simulation = new DiplomacySimulation(state);
        simulation.ProcessContactOpportunity(new FirstContactOpportunity(7, "7-8", 8, 1, 10, ContactAwareness.ContactEstablished, ContactCondition.Active, false, .9));
        simulation.ProcessContactOpportunity(new FirstContactOpportunity(8, "8-7", 7, 1, 10, ContactAwareness.ContactEstablished, ContactCondition.Active, false, .9));
        var first = new ObserverDiplomacyCommandService(state);
        var second = new ObserverDiplomacyCommandService(state);
        Require(first.EstablishCommunication(7, 8, 2).Accepted, "communication setup failed");
        var withdrawn = first.SendProposal(7, 8, DiplomaticProposalKind.Agreement, 3, "withdraw me", DiplomaticAgreementType.NonAggression);
        Require(withdrawn.Accepted && first.WithdrawProposal(7, withdrawn.ProposalId!.Value, 4).Accepted, "outgoing withdrawal failed");
        var incoming = first.SendProposal(7, 8, DiplomaticProposalKind.Agreement, 5, "accept me", DiplomaticAgreementType.Access);
        Require(incoming.Accepted && second.RespondToProposal(8, incoming.ProposalId!.Value, true, 6).Accepted, "incoming acceptance failed");
        Require(first.BuildView(7).Agreements.Any(a => a.Status == DiplomaticAgreementStatus.Active), "agreement missing from observer view");
        Require(state.GetAccessPermission(7, 8) == AccessPermission.Granted && state.GetAccessPermission(8, 7) == AccessPermission.Granted, "access agreement was not directional pair state");
        Require(first.SetAccessPermission(7, 8, AccessPermission.Denied, 7).Accepted, "access command failed");
        Require(first.DeclareWar(7, 8, 8).Accepted, "war declaration failed");
        var ceasefire = first.SendProposal(7, 8, DiplomaticProposalKind.CeasefireOffer, 9, "ceasefire");
        Require(ceasefire.Accepted && second.RespondToProposal(8, ceasefire.ProposalId!.Value, true, 10).Accepted, "ceasefire lifecycle failed");
        var peace = first.SendProposal(7, 8, DiplomaticProposalKind.PeaceOffer, 11, "peace");
        Require(peace.Accepted && second.RespondToProposal(8, peace.ProposalId!.Value, true, 12).Accepted, "peace lifecycle failed");
        Require(first.BuildView(7).RecentEvents.Count > 0, "diplomatic history was not observer-visible");
        Require(!new ObserverDiplomacyCommandService(state).BuildView(99).Agreements.Any(), "unrelated observer saw agreements");
        var model = DiplomacyWorkspacePresenter.Build(first.BuildView(7), 0, 0, _ => "KNOWN");
        Require(model.Contacts.Single().SourceIndex == 0 && model.Contacts.Single().PendingProposalCount == 0, "contact source or pending count was not scoped");
        Require(DiplomacyWorkspacePresenter.FilterContacts(model, DiplomacyContactFilter.CommunicationAvailable).Count == 1, "communication filter lost active contact");
        Require(DiplomacyWorkspacePresenter.FilterContacts(model, DiplomacyContactFilter.Neutral).All(c => c.Status is "Peace" or "NO FORMAL RELATIONSHIP" or "Unknown"), "neutral filter included a hostile political state");
    }


    static void MultiContactModelIsolation()
    {
        var unknown = new DiplomaticContactView("local-unknown", null, ContactAwareness.DetectedUnidentified,
            ContactCondition.Active, false, .4, 3, null);
        var view = View(unknown) with
        {
            Contacts = new[] { unknown,
                new DiplomaticContactView("known-one", 8, ContactAwareness.CommunicationAvailable, ContactCondition.Active, true, 1, 4, 10),
                new DiplomaticContactView("known-two", 9, ContactAwareness.ContactEstablished, ContactCondition.StaleOrLost, false, .6, 4, 12) },
            Relationships = new[] {
                new DiplomaticRelationshipView(8, DiplomaticPoliticalState.Peace, .5, 0, .1, .2, .7, Array.Empty<DiplomaticGrievanceSnapshot>()),
                new DiplomaticRelationshipView(9, DiplomaticPoliticalState.AtWar, 0, .9, .1, .2, .1, Array.Empty<DiplomaticGrievanceSnapshot>()) },
            Proposals = new[] {
                new DiplomaticProposalSnapshot(1, 9, 7, DiplomaticProposalKind.Agreement, DiplomaticAgreementType.NonAggression, DiplomaticProposalStatus.Pending, 4, null, "incoming", null),
                new DiplomaticProposalSnapshot(2, 7, 9, DiplomaticProposalKind.AccessRequest, null, DiplomaticProposalStatus.Pending, 4, null, "outgoing", null) },
        };
        string Name(int id) => id is 8 or 9 ? "Known " + id : throw new Exception("Hidden identity requested");
        var one = DiplomacyWorkspacePresenter.Build(view, 1, 0, Name);
        Require(one.Contacts[0].CivilizationId is null && !one.Contacts[0].Identified && one.Contacts[0].Cooperation is null, "unknown row leaked hidden identity/metrics");
        var pending = DiplomacyWorkspacePresenter.FilterContacts(one, DiplomacyContactFilter.PendingProposal);
        Require(pending.Count == 1 && pending[0].SourceIndex == 2 && pending[0].PendingProposalCount == 2, "pending filter leaked across counterpart");
        Require(DiplomacyWorkspacePresenter.FilterContacts(one, DiplomacyContactFilter.CommunicationAvailable).Single().SourceIndex == 1, "stale contact appeared as communicable");
        Require(DiplomacyWorkspacePresenter.FilterContacts(one, DiplomacyContactFilter.Cooperative).Single().CivilizationId == 8, "cooperation filter did not use visible metric");
        var two = DiplomacyWorkspacePresenter.Build(view, 2, 0, Name);
        Require(two.Proposals[0].CanAccept && two.Proposals[0].CanReject && !two.Proposals[0].CanWithdraw, "incoming flags borrowed another proposal");
        Require(!two.Proposals[1].CanAccept && !two.Proposals[1].CanReject && two.Proposals[1].CanWithdraw, "outgoing flags borrowed incoming proposal");
        var hidden = DiplomacyWorkspacePresenter.Build(view with { Contacts = new[] { unknown } }, 0, 0, _ => throw new Exception("Unknown name resolver called"));
        Require(hidden.Selected.Trust is null && hidden.Proposals.Count == 0 && hidden.Agreements.Count == 0, "anonymous selection exposed diplomatic state");
    }

    static DiplomaticStateView View(DiplomaticContactView contact,
        DiplomaticRelationshipView? relationship = null,
        IReadOnlyList<DiplomaticProposalSnapshot>? proposals = null) =>
        new(7, new[] { contact }, relationship is null ? Array.Empty<DiplomaticRelationshipView>() : new[] { relationship },
            Array.Empty<DiplomaticAccessSnapshot>(), Array.Empty<TerritorialClaimSnapshot>(), Array.Empty<TerritorialClaimResponseSnapshot>(),
            Array.Empty<DiplomaticAgreementSnapshot>(), proposals ?? Array.Empty<DiplomaticProposalSnapshot>(), Array.Empty<DiplomaticHistoryEventSnapshot>());

    static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
}
