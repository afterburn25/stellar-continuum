using System.Globalization;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Diplomacy;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
var sourceRoot = args.Length > 0 ? args[0] : "<missing>";
var destination = args.Length > 1 ? args[1] : "<missing>";

try
{
    if (args.Length != 2)
        throw new ArgumentException("Expected source root and output fixture path.");
    var authorityPath = Path.GetFullPath(Path.Combine(args[0],
        "src/Game/Simulation/Diplomacy/DiplomacySnapshotInvariantValidator.cs"));
    var authorityFingerprint = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(authorityPath)));

    var options = new JsonSerializerOptions
    {
        WriteIndented = true,
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };
    object? Error(Exception? error) => error is null
        ? null
        : new { Type = error.GetType().Name, error.Message };

    DiplomacyStateSnapshot Valid() => new(
        [new(1, "civilization:2", 2, 10, 20, 3, ContactAwareness.Identified,
            ContactCondition.Active, false, .8)],
        [new(1, 2, DiplomaticPoliticalState.Peace, .5, .1, .2, .6, .4,
            [new(11, 2, .25, "Border concern")])],
        [new(1, 2, AccessPermission.Granted, 20)],
        [new(1, 1, 3, 10, true, [1, 2])],
        [new(1, 2, TerritorialClaimResponse.Recognized, 11)],
        [new(1, 1, 2, DiplomaticAgreementType.Trade, DiplomaticAgreementStatus.Active,
            12, null, "trade:1")],
        [new(1, 1, 2, DiplomaticProposalKind.AccessRequest, null,
            DiplomaticProposalStatus.Pending, 13, null, "Request access", null)],
        [new(1, 10, DiplomaticEventKind.ContactObserved, 1, 2, 3, "Contact observed", [1, 2])],
        2, 2, 2, 2);

    var rows = new List<object>();
    void Case(string name, DiplomacyStateSnapshot snapshot)
    {
        var before = JsonSerializer.Serialize(snapshot, options);
        DiplomacySnapshotValidationResult? result = null;
        Exception? error = null;
        try
        {
            result = DiplomacySnapshotInvariantValidator.Validate(snapshot);
        }
        catch (Exception exception)
        {
            error = exception;
        }
        var after = JsonSerializer.Serialize(snapshot, options);
        rows.Add(new
        {
            Name = name,
            SourceOnlyReason = (string?)null,
            Input = JsonSerializer.SerializeToNode(snapshot, options),
            InputBefore = before,
            InputAfter = after,
            Error = Error(error),
            Result = result,
        });
    }

    void NullSnapshotCase()
    {
        Exception? error = null;
        try
        {
            _ = DiplomacySnapshotInvariantValidator.Validate(null!);
        }
        catch (Exception exception)
        {
            error = exception;
        }
        rows.Add(new
        {
            Name = "null-snapshot",
            SourceOnlyReason = "Native validation entry points use non-null value/reference types.",
            Input = (object?)null,
            InputBefore = "null",
            InputAfter = "null",
            Error = Error(error),
            Result = (object?)null,
        });
    }

    NullSnapshotCase();
    Case("valid-complete", Valid());
    Case("valid-empty", new([], [], [], [], [], [], [], [], 1, 1, 1, 1));

    Case("null-contacts", Valid() with { Contacts = null! });
    Case("null-relationships", Valid() with { Relationships = null! });
    Case("null-access", Valid() with { AccessPermissions = null! });
    Case("null-claims", Valid() with { Claims = null! });
    Case("null-responses", Valid() with { ClaimResponses = null! });
    Case("null-agreements", Valid() with { Agreements = null! });
    Case("null-proposals", Valid() with { Proposals = null! });
    Case("null-history", Valid() with { RecentHistory = null! });
    Case("presence-before-bounds", Valid() with
    {
        Contacts = Enumerable.Repeat(Valid().Contacts[0], DiplomacyState.MaxContactRecords + 1).ToArray(),
        Relationships = null!,
    });

    Case("contact-bound", Valid() with
    {
        Contacts = Enumerable.Repeat(Valid().Contacts[0], DiplomacyState.MaxContactRecords + 1).ToArray(),
    });
    Case("proposal-bound", Valid() with
    {
        Proposals = Enumerable.Repeat(Valid().Proposals[0], DiplomacyState.MaxStoredProposals + 1).ToArray(),
    });
    Case("history-bound", Valid() with
    {
        RecentHistory = Enumerable.Repeat(Valid().RecentHistory[0], DiplomacyState.MaxRecentHistoryEvents + 1).ToArray(),
    });

    void Contact(string name, Func<DiplomaticContactSnapshot, DiplomaticContactSnapshot> change)
    {
        var snapshot = Valid();
        Case(name, snapshot with { Contacts = [change(snapshot.Contacts[0])] });
    }
    Contact("contact-negative-observer", value => value with { ObserverCivilizationId = -1 });
    Contact("contact-blank-id-unicode", value => value with { ContactId = "\u2003" });

    var duplicateContact = Valid();
    Case("contact-duplicate-key", duplicateContact with
    {
        Contacts = [duplicateContact.Contacts[0], duplicateContact.Contacts[0]],
    });
    Contact("contact-self-target", value => value with { TargetCivilizationId = 1 });
    Contact("contact-negative-target", value => value with { TargetCivilizationId = -1 });
    Contact("contact-negative-first-tick", value => value with { FirstObservedTick = -1 });
    Contact("contact-reversed-chronology", value => value with { LastObservedTick = 9 });
    Contact("contact-negative-system", value => value with { LastObservedSystemId = -1 });
    Contact("contact-unknown-awareness-enum", value => value with { Awareness = (ContactAwareness)99 });
    Contact("contact-unknown-condition-enum", value => value with { Condition = (ContactCondition)99 });
    Contact("contact-stored-unknown", value => value with { Awareness = ContactAwareness.Unknown });
    Contact("contact-confidence-nan", value => value with { Confidence = double.NaN });
    Contact("contact-confidence-high", value => value with { Confidence = 1.000001 });
    Contact("contact-confidence-negative", value => value with { Confidence = -.000001 });
    Contact("contact-identified-no-target", value => value with { TargetCivilizationId = null });
    Contact("contact-unidentified-exposes-target", value => value with
    {
        Awareness = ContactAwareness.DetectedUnidentified,
    });
    Contact("contact-communication-no-target", value => value with
    {
        TargetCivilizationId = null,
        Awareness = ContactAwareness.ContactPossible,
        CommunicationAvailable = true,
    });
    Contact("contact-communication-low-awareness", value => value with
    {
        Awareness = ContactAwareness.Identified,
        CommunicationAvailable = true,
    });
    Contact("contact-stale-communication", value => value with
    {
        Awareness = ContactAwareness.ContactPossible,
        Condition = ContactCondition.StaleOrLost,
        CommunicationAvailable = true,
    });

    void Relationship(string name,
        Func<DiplomaticRelationshipSnapshot, DiplomaticRelationshipSnapshot> change)
    {
        var snapshot = Valid();
        Case(name, snapshot with { Relationships = [change(snapshot.Relationships[0])] });
    }
    Relationship("relationship-negative-a", value => value with { CivilizationAId = -1 });
    Relationship("relationship-negative-b", value => value with { CivilizationBId = -1 });
    Relationship("relationship-noncanonical", value => value with { CivilizationAId = 2, CivilizationBId = 1 });
    var duplicateRelationship = Valid();
    Case("relationship-duplicate", duplicateRelationship with
    {
        Relationships = [duplicateRelationship.Relationships[0], duplicateRelationship.Relationships[0]],
    });
    Relationship("relationship-no-contact-basis", value => value with { CivilizationBId = 3 });
    Relationship("relationship-unknown-state", value => value with { PoliticalState = (DiplomaticPoliticalState)99 });
    Relationship("relationship-trust-nan", value => value with { Trust = double.NaN });
    Relationship("relationship-hostility-high", value => value with { Hostility = 1.1 });
    Relationship("relationship-null-grievances", value => value with { Grievances = null! });
    Relationship("relationship-earlier-field-before-null-grievances", value => value with
    {
        CivilizationAId = -1,
        Grievances = null!,
    });
    Relationship("grievance-bound", value => value with
    {
        Grievances = Enumerable.Repeat(value.Grievances[0], 17).ToArray(),
    });
    Relationship("grievance-negative-tick", value => value with
    {
        Grievances = [value.Grievances[0] with { CreatedAtTick = -1 }],
    });
    Relationship("grievance-unrelated-source", value => value with
    {
        Grievances = [value.Grievances[0] with { SourceCivilizationId = 3 }],
    });
    Relationship("grievance-invalid-severity", value => value with
    {
        Grievances = [value.Grievances[0] with { Severity = double.PositiveInfinity }],
    });
    Relationship("grievance-blank-reason", value => value with
    {
        Grievances = [value.Grievances[0] with { Reason = " " }],
    });

    void Access(string name, Func<DiplomaticAccessSnapshot, DiplomaticAccessSnapshot> change)
    {
        var snapshot = Valid();
        Case(name, snapshot with { AccessPermissions = [change(snapshot.AccessPermissions[0])] });
    }
    Access("access-negative-grantor", value => value with { GrantorCivilizationId = -1 });
    Access("access-self", value => value with { VisitorCivilizationId = 1 });
    var duplicateAccess = Valid();
    Case("access-duplicate", duplicateAccess with
    {
        AccessPermissions = [duplicateAccess.AccessPermissions[0], duplicateAccess.AccessPermissions[0]],
    });
    Access("access-unknown-enum", value => value with { Permission = (AccessPermission)99 });
    Access("access-negative-tick", value => value with { UpdatedAtTick = -1 });
    Access("access-no-relationship", value => value with { VisitorCivilizationId = 3 });

    void Claim(string name, Func<TerritorialClaimSnapshot, TerritorialClaimSnapshot> change)
    {
        var snapshot = Valid();
        Case(name, snapshot with { Claims = [change(snapshot.Claims[0])] });
    }
    Claim("claim-zero-id", value => value with { ClaimId = 0 });
    var duplicateClaim = Valid();
    Case("claim-duplicate-id", duplicateClaim with
    {
        Claims = [duplicateClaim.Claims[0], duplicateClaim.Claims[0]],
    });
    Claim("claim-negative-claimant", value => value with { ClaimantCivilizationId = -1 });
    Claim("claim-negative-system", value => value with { SystemId = -1 });
    Claim("claim-negative-asserted-tick", value => value with { AssertedAtTick = -1 });
    Claim("claim-null-audience", value => value with { KnownToCivilizationIds = null! });
    Claim("claim-earlier-field-before-null-audience", value => value with
    {
        ClaimantCivilizationId = -1,
        KnownToCivilizationIds = null!,
    });
    Claim("claim-negative-audience", value => value with { KnownToCivilizationIds = [-1, 1] });
    Claim("claim-duplicate-audience", value => value with { KnownToCivilizationIds = [1, 1] });
    Claim("claim-missing-claimant-audience", value => value with { KnownToCivilizationIds = [2] });
    Claim("claim-unsorted-audience", value => value with { KnownToCivilizationIds = [2, 1] });

    void Response(string name,
        Func<TerritorialClaimResponseSnapshot, TerritorialClaimResponseSnapshot> change)
    {
        var snapshot = Valid();
        Case(name, snapshot with { ClaimResponses = [change(snapshot.ClaimResponses[0])] });
    }
    Response("response-unknown-claim", value => value with { ClaimId = 2 });
    Response("response-negative-civilization", value => value with { RespondingCivilizationId = -1 });
    Response("response-by-claimant", value => value with { RespondingCivilizationId = 1 });
    var unknownAudienceResponse = Valid();
    Case("response-not-in-audience", unknownAudienceResponse with
    {
        Claims = [unknownAudienceResponse.Claims[0] with { KnownToCivilizationIds = [1, 3] }],
    });
    var duplicateResponse = Valid();
    Case("response-duplicate", duplicateResponse with
    {
        ClaimResponses = [duplicateResponse.ClaimResponses[0], duplicateResponse.ClaimResponses[0]],
    });
    Response("response-unknown-enum", value => value with { Response = (TerritorialClaimResponse)99 });
    Response("response-none", value => value with { Response = TerritorialClaimResponse.None });
    Response("response-predates-claim", value => value with { RespondedAtTick = 9 });

    void Agreement(string name,
        Func<DiplomaticAgreementSnapshot, DiplomaticAgreementSnapshot> change)
    {
        var snapshot = Valid();
        Case(name, snapshot with { Agreements = [change(snapshot.Agreements[0])] });
    }
    Agreement("agreement-zero-id", value => value with { AgreementId = 0 });
    var duplicateAgreementId = Valid();
    Case("agreement-duplicate-id", duplicateAgreementId with
    {
        Agreements = [duplicateAgreementId.Agreements[0], duplicateAgreementId.Agreements[0]],
    });
    Agreement("agreement-negative-a", value => value with { CivilizationAId = -1 });
    Agreement("agreement-noncanonical", value => value with { CivilizationAId = 2, CivilizationBId = 1 });
    Agreement("agreement-no-relationship", value => value with { CivilizationBId = 3 });
    Agreement("agreement-unknown-type", value => value with { Type = (DiplomaticAgreementType)99 });
    Agreement("agreement-unknown-status", value => value with { Status = (DiplomaticAgreementStatus)99 });
    Agreement("agreement-negative-start", value => value with { StartedAtTick = -1 });
    Agreement("agreement-active-ended", value => value with { EndedAtTick = 13 });
    Agreement("agreement-terminated-no-end", value => value with
    {
        Status = DiplomaticAgreementStatus.Terminated,
    });
    Agreement("agreement-terminated-reversed", value => value with
    {
        Status = DiplomaticAgreementStatus.Terminated,
        EndedAtTick = 11,
    });
    var duplicateAgreement = Valid();
    Case("agreement-duplicate-active-type", duplicateAgreement with
    {
        Agreements = [duplicateAgreement.Agreements[0], duplicateAgreement.Agreements[0] with { AgreementId = 2 }],
        NextAgreementId = 3,
    });
    Agreement("agreement-trade-missing-terms", value => value with { ExternalTermsReference = " " });
    Agreement("agreement-valid-terminated", value => value with
    {
        Status = DiplomaticAgreementStatus.Terminated,
        EndedAtTick = 13,
    });

    void Proposal(string name, Func<DiplomaticProposalSnapshot, DiplomaticProposalSnapshot> change)
    {
        var snapshot = Valid();
        Case(name, snapshot with { Proposals = [change(snapshot.Proposals[0])] });
    }
    Proposal("proposal-zero-id", value => value with { ProposalId = 0 });
    var duplicateProposalId = Valid();
    Case("proposal-duplicate-id", duplicateProposalId with
    {
        Proposals = [duplicateProposalId.Proposals[0], duplicateProposalId.Proposals[0]],
    });
    Proposal("proposal-negative-proposer", value => value with { ProposerCivilizationId = -1 });
    Proposal("proposal-self", value => value with { RecipientCivilizationId = 1 });
    Proposal("proposal-no-relationship", value => value with { RecipientCivilizationId = 3 });
    Proposal("proposal-unknown-kind", value => value with { Kind = (DiplomaticProposalKind)99 });
    Proposal("proposal-unknown-status", value => value with { Status = (DiplomaticProposalStatus)99 });
    Proposal("proposal-unknown-agreement-type", value => value with
    {
        Kind = DiplomaticProposalKind.Agreement,
        AgreementType = (DiplomaticAgreementType)99,
    });
    Proposal("proposal-agreement-missing-type", value => value with { Kind = DiplomaticProposalKind.Agreement });
    Proposal("proposal-nonagreement-has-type", value => value with { AgreementType = DiplomaticAgreementType.Peace });
    Proposal("proposal-trade-missing-terms", value => value with
    {
        Kind = DiplomaticProposalKind.TradeOffer,
        ExternalTermsReference = " ",
    });
    Proposal("proposal-negative-created", value => value with { CreatedAtTick = -1 });
    Proposal("proposal-blank-summary", value => value with { Summary = "\u2003" });
    Proposal("proposal-pending-resolved", value => value with { ResolvedAtTick = 14 });
    Proposal("proposal-resolved-no-tick", value => value with { Status = DiplomaticProposalStatus.Accepted });
    Proposal("proposal-resolved-reversed", value => value with
    {
        Status = DiplomaticProposalStatus.Accepted,
        ResolvedAtTick = 12,
    });
    Proposal("proposal-valid-resolved", value => value with
    {
        Status = DiplomaticProposalStatus.Accepted,
        ResolvedAtTick = 14,
    });
    var pendingBound = Valid();
    pendingBound = pendingBound with
    {
        Proposals = Enumerable.Range(1, DiplomacyState.MaxPendingProposalsPerPair + 1)
            .Select(id => pendingBound.Proposals[0] with { ProposalId = id }).ToArray(),
        NextProposalId = DiplomacyState.MaxPendingProposalsPerPair + 2,
    };
    Case("proposal-pending-pair-bound", pendingBound);

    void History(string name,
        Func<DiplomaticHistoryEventSnapshot, DiplomaticHistoryEventSnapshot> change)
    {
        var snapshot = Valid();
        Case(name, snapshot with { RecentHistory = [change(snapshot.RecentHistory[0])] });
    }
    History("history-zero-id", value => value with { EventId = 0 });
    var duplicateHistoryId = Valid();
    Case("history-duplicate-id", duplicateHistoryId with
    {
        RecentHistory = [duplicateHistoryId.RecentHistory[0], duplicateHistoryId.RecentHistory[0]],
    });
    var unorderedHistory = Valid();
    Case("history-noncanonical-order", unorderedHistory with
    {
        RecentHistory = [unorderedHistory.RecentHistory[0] with { EventId = 2 }, unorderedHistory.RecentHistory[0]],
        NextEventId = 3,
    });
    History("history-negative-tick", value => value with { Tick = -1 });
    History("history-blank-summary", value => value with { Summary = " " });
    History("history-unknown-kind", value => value with { Kind = (DiplomaticEventKind)99 });
    History("history-negative-primary", value => value with { PrimaryCivilizationId = -1 });
    History("history-negative-secondary", value => value with { SecondaryCivilizationId = -1 });
    History("history-self-secondary", value => value with { SecondaryCivilizationId = 1 });
    History("history-negative-system", value => value with { SystemId = -1 });
    History("history-null-audience", value => value with { KnownToCivilizationIds = null! });
    History("history-earlier-field-before-null-audience", value => value with
    {
        Tick = -1,
        KnownToCivilizationIds = null!,
    });
    History("history-negative-audience", value => value with { KnownToCivilizationIds = [-1, 1] });
    History("history-duplicate-audience", value => value with { KnownToCivilizationIds = [1, 1] });
    History("history-missing-primary-audience", value => value with { KnownToCivilizationIds = [2] });
    History("history-unsorted-audience", value => value with { KnownToCivilizationIds = [2, 1] });

    Case("next-claim-zero", Valid() with { NextClaimId = 0 });
    Case("next-claim-not-above", Valid() with { NextClaimId = 1 });
    Case("next-agreement-not-above", Valid() with { NextAgreementId = 1 });
    Case("next-proposal-not-above", Valid() with { NextProposalId = 1 });
    Case("next-event-not-above", Valid() with { NextEventId = 1 });
    var maximumClaim = Valid();
    Case("next-claim-max-boundary", maximumClaim with
    {
        Claims = [maximumClaim.Claims[0] with { ClaimId = long.MaxValue }],
        ClaimResponses = [maximumClaim.ClaimResponses[0] with { ClaimId = long.MaxValue }],
        NextClaimId = long.MaxValue,
    });

    var document = new
    {
        Authority = "DiplomacySnapshotInvariantValidator.cs",
        AuthorityFingerprint = authorityFingerprint,
        Culture = "InvariantCulture",
        Rows = rows,
    };
    var output = Path.GetFullPath(args[1]);
    Directory.CreateDirectory(Path.GetDirectoryName(output)!);
    File.WriteAllText(output, JsonSerializer.Serialize(document, options));
    Console.WriteLine($"diplomacy snapshot invariant oracle: {rows.Count} rows -> {output}");
    return 0;
}
catch (Exception exception)
{
    Console.Error.WriteLine(exception);
    Console.Error.WriteLine($"CurrentDirectory: {Environment.CurrentDirectory}");
    Console.Error.WriteLine($"SourceRoot: {sourceRoot}");
    Console.Error.WriteLine($"FixturePath: {destination}");
    return 1;
}



