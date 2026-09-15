using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using Game.Simulation.Diplomacy;
using Game.Simulation.Generation;
using Game.Simulation.Models;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
var diagnosticRoot = args.Length > 0 ? args[0] : "<missing>";
var diagnosticFixture = args.Length > 1 ? args[1] : "<missing>";

try
{
    if (args.Length != 2)
        throw new ArgumentException("Expected repository root and output fixture path.");
    var root = Path.GetFullPath(args[0]);
    var output = Path.GetFullPath(args[1]);
    var sourceNames = new[]
    {
        "ObserverDiplomacyCommandService.cs",
        "ObserverDiplomacyClaimCommands.cs",
        "ObserverDiplomacyActionAvailability.cs",
        "CampaignDiplomacyTerritorialClaimCommandService.cs",
        "CampaignDiplomacyBorderWarningCommandService.cs",
    };
    var sourcePaths = sourceNames.Select(name => Path.Combine(
        root, "src", "Game", "Simulation", "Diplomacy", name)).ToArray();
    string Fingerprint()
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (var path in sourcePaths)
        {
            hash.AppendData(Encoding.UTF8.GetBytes(Path.GetFileName(path)));
            hash.AppendData([0]);
            hash.AppendData(File.ReadAllBytes(path));
            hash.AppendData([0]);
        }
        return Convert.ToHexString(hash.GetHashAndReset());
    }

    var json = new JsonSerializerOptions
    {
        WriteIndented = true,
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };
    JsonNode? Node(object? value) => JsonSerializer.SerializeToNode(value, json);
    object? Error(Exception? value) => value is null ? null : new
    {
        Type = value.GetType().Name,
        value.Message,
    };
    DiplomacyStateSnapshot Empty() => new([], [], [], [], [], [], [], [], 1, 1, 1, 1);
    DiplomaticContactSnapshot Contact(
        int observer, int target, long tick = 10, bool communication = true,
        ContactAwareness awareness = ContactAwareness.CommunicationAvailable,
        ContactCondition condition = ContactCondition.Active, string? id = null) =>
        new(observer, id ?? $"civilization:{target}", target, 1, tick, 7,
            awareness, condition, communication, .75);
    DiplomacyStateSnapshot Mutual() => Empty() with
    {
        Contacts = [Contact(1, 2), Contact(2, 1)],
    };
    DiplomacyStateSnapshot IdentifiedBoth() => Empty() with
    {
        Contacts =
        [
            Contact(1, 2, communication: false, awareness: ContactAwareness.Identified),
            Contact(2, 1, communication: false, awareness: ContactAwareness.Identified),
        ],
    };
    object Projection(DiplomacyState state) => new
    {
        Snapshot = Node(state.Snapshot()),
        View1 = Node(state.BuildViewFor(1)),
        View2 = Node(state.BuildViewFor(2)),
        View3 = Node(state.BuildViewFor(3)),
        View4 = Node(state.BuildViewFor(4)),
    };

    var rows = new List<object>();
    void Command(
        string name, DiplomacyStateSnapshot initial, string operation,
        object arguments, Func<ObserverDiplomacyCommandService, object?> invoke)
    {
        var state = DiplomacyState.Restore(initial);
        var service = new ObserverDiplomacyCommandService(state);
        var input = new { Snapshot = initial, Operation = operation, Arguments = arguments };
        var owned = JsonSerializer.Serialize(input, json);
        var before = Fingerprint();
        object? result = null;
        Exception? error = null;
        try
        {
            result = invoke(service);
        }
        catch (Exception caught)
        {
            error = caught;
        }
        rows.Add(new
        {
            Name = name,
            Kind = "Command",
            Input = Node(input),
            InputBefore = owned,
            InputAfter = JsonSerializer.Serialize(input, json),
            BeforeFingerprint = before,
            AfterFingerprint = Fingerprint(),
            Error = Error(error),
            Result = Node(result),
            State = Projection(state),
        });
    }

    Command("view-valid", Empty(), "BuildView", new { Observer = 1 },
        service => service.BuildView(1));
    Command("view-negative", Empty(), "BuildView", new { Observer = -1 },
        service => service.BuildView(-1));
    Command("communication-success", IdentifiedBoth(), "EstablishCommunication",
        new { Observer = 1, Target = 2, Tick = 11L },
        service => service.EstablishCommunication(1, 2, 11));
    Command("communication-reciprocal-hidden", Empty() with
    {
        Contacts = [Contact(1, 2, communication: false, awareness: ContactAwareness.Identified)],
    }, "EstablishCommunication", new { Observer = 1, Target = 2, Tick = 11L },
        service => service.EstablishCommunication(1, 2, 11));
    Command("communication-stale", Empty() with
    {
        Contacts = [Contact(1, 2, communication: false, awareness: ContactAwareness.Identified,
            condition: ContactCondition.StaleOrLost)],
    }, "EstablishCommunication", new { Observer = 1, Target = 2, Tick = 11L },
        service => service.EstablishCommunication(1, 2, 11));
    Command("communication-already", Mutual(), "EstablishCommunication",
        new { Observer = 1, Target = 2, Tick = 11L },
        service => service.EstablishCommunication(1, 2, 11));
    Command("communication-invalid-self", Empty(), "EstablishCommunication",
        new { Observer = 1, Target = 1, Tick = 1L },
        service => service.EstablishCommunication(1, 1, 1));

    Command("proposal-send", Mutual(), "SendProposal", new
    {
        Observer = 1, Target = 2, Kind = DiplomaticProposalKind.Demand, Tick = 12L,
        Summary = "Visible demand.", AgreementType = (DiplomaticAgreementType?)null,
        ExternalTermsReference = (string?)null,
    }, service => service.SendProposal(1, 2, DiplomaticProposalKind.Demand, 12,
        "Visible demand."));
    Command("proposal-unicode-blank", Mutual(), "SendProposal", new
    {
        Observer = 1, Target = 2, Kind = DiplomaticProposalKind.Demand, Tick = 12L,
        Summary = "\u2003", AgreementType = (DiplomaticAgreementType?)null,
        ExternalTermsReference = (string?)null,
    }, service => service.SendProposal(1, 2, DiplomaticProposalKind.Demand, 12, "\u2003"));
    Command("proposal-no-channel", IdentifiedBoth(), "SendProposal", new
    {
        Observer = 1, Target = 2, Kind = DiplomaticProposalKind.Demand, Tick = 12L,
        Summary = "Hidden channel.", AgreementType = (DiplomaticAgreementType?)null,
        ExternalTermsReference = (string?)null,
    }, service => service.SendProposal(1, 2, DiplomaticProposalKind.Demand, 12,
        "Hidden channel."));
    Command("proposal-unknown-kind", Mutual(), "SendProposal", new
    {
        Observer = 1, Target = 2, Kind = (DiplomaticProposalKind)99, Tick = 12L,
        Summary = "Unknown kind.", AgreementType = (DiplomaticAgreementType?)null,
        ExternalTermsReference = (string?)null,
    }, service => service.SendProposal(1, 2, (DiplomaticProposalKind)99, 12,
        "Unknown kind."));
    Command("proposal-invalid-agreement-shape", Mutual(), "SendProposal", new
    {
        Observer = 1, Target = 2, Kind = DiplomaticProposalKind.Agreement, Tick = 12L,
        Summary = "Missing type.", AgreementType = (DiplomaticAgreementType?)null,
        ExternalTermsReference = (string?)null,
    }, service => service.SendProposal(1, 2, DiplomaticProposalKind.Agreement, 12,
        "Missing type."));

    var accessProposal = new DiplomaticProposalSnapshot(5, 1, 2,
        DiplomaticProposalKind.AccessRequest, null, DiplomaticProposalStatus.Pending,
        12, null, "Access request.", null);
    var pending = Mutual() with { Proposals = [accessProposal], NextProposalId = 6 };
    Command("proposal-accept", pending, "RespondToProposal",
        new { Observer = 2, ProposalId = 5L, Accept = true, Tick = 13L },
        service => service.RespondToProposal(2, 5, true, 13));
    Command("proposal-wrong-responder", pending, "RespondToProposal",
        new { Observer = 1, ProposalId = 5L, Accept = true, Tick = 13L },
        service => service.RespondToProposal(1, 5, true, 13));
    Command("proposal-hidden-id", pending, "RespondToProposal",
        new { Observer = 3, ProposalId = 5L, Accept = false, Tick = 13L },
        service => service.RespondToProposal(3, 5, false, 13));
    Command("proposal-withdraw", pending, "WithdrawProposal",
        new { Observer = 1, ProposalId = 5L, Tick = 13L },
        service => service.WithdrawProposal(1, 5, 13));
    Command("proposal-withdraw-wrong", pending, "WithdrawProposal",
        new { Observer = 2, ProposalId = 5L, Tick = 13L },
        service => service.WithdrawProposal(2, 5, 13));

    Command("access-granted", Mutual(), "SetAccessPermission",
        new { Observer = 1, Target = 2, Permission = AccessPermission.Granted, Tick = 14L },
        service => service.SetAccessPermission(1, 2, AccessPermission.Granted, 14));
    Command("access-no-channel", IdentifiedBoth(), "SetAccessPermission",
        new { Observer = 1, Target = 2, Permission = AccessPermission.Denied, Tick = 14L },
        service => service.SetAccessPermission(1, 2, AccessPermission.Denied, 14));
    Command("access-unknown-enum", Mutual(), "SetAccessPermission",
        new { Observer = 1, Target = 2, Permission = (AccessPermission)99, Tick = 14L },
        service => service.SetAccessPermission(1, 2, (AccessPermission)99, 14));
    Command("war-visible-stale", Empty() with
    {
        Contacts = [Contact(1, 2, condition: ContactCondition.StaleOrLost,
            communication: false, awareness: ContactAwareness.Identified)],
    }, "DeclareWar", new { Observer = 1, Target = 2, Tick = 15L },
        service => service.DeclareWar(1, 2, 15));
    Command("war-hidden", Empty(), "DeclareWar",
        new { Observer = 1, Target = 2, Tick = 15L },
        service => service.DeclareWar(1, 2, 15));

    var accessAgreement = new DiplomaticAgreementSnapshot(7, 1, 2,
        DiplomaticAgreementType.Access, DiplomaticAgreementStatus.Active, 10, null, null);
    var activeAgreement = Mutual() with
    {
        Agreements = [accessAgreement],
        AccessPermissions =
        [
            new(1, 2, AccessPermission.Granted, 10),
            new(2, 1, AccessPermission.Granted, 10),
        ],
        NextAgreementId = 8,
    };
    Command("agreement-terminate-access", activeAgreement, "TerminateAgreement",
        new { Observer = 1, AgreementId = 7L, Tick = 16L, Reason = "  policy changed  " },
        service => service.TerminateAgreement(1, 7, 16, "  policy changed  "));
    Command("agreement-hidden", activeAgreement, "TerminateAgreement",
        new { Observer = 3, AgreementId = 7L, Tick = 16L, Reason = "probe" },
        service => service.TerminateAgreement(3, 7, 16, "probe"));
    Command("agreement-blank-reason", activeAgreement, "TerminateAgreement",
        new { Observer = 1, AgreementId = 7L, Tick = 16L, Reason = "\u00a0" },
        service => service.TerminateAgreement(1, 7, 16, "\u00a0"));
    Command("agreement-idempotent-no-channel", Empty() with
    {
        Agreements = [accessAgreement with
        {
            Status = DiplomaticAgreementStatus.Terminated,
            EndedAtTick = 11,
        }],
        NextAgreementId = 8,
    }, "TerminateAgreement", new
    {
        Observer = 1, AgreementId = 7L, Tick = 16L, Reason = "retry",
    }, service => service.TerminateAgreement(1, 7, 16, "retry"));

    var visibleClaim = new TerritorialClaimSnapshot(9, 1, 77, 2, true, [1]);
    var claimState = Mutual() with { Claims = [visibleClaim], NextClaimId = 10 };
    Command("claim-communicate", claimState, "CommunicateTerritorialClaim",
        new { Observer = 1, ClaimId = 9L, Recipient = 2, Tick = 17L },
        service => service.CommunicateTerritorialClaim(1, 9, 2, 17));
    Command("claim-communicate-retry", claimState with
    {
        Claims = [visibleClaim with { KnownToCivilizationIds = [1, 2] }],
    }, "CommunicateTerritorialClaim",
        new { Observer = 1, ClaimId = 9L, Recipient = 2, Tick = 17L },
        service => service.CommunicateTerritorialClaim(1, 9, 2, 17));
    Command("claim-hidden", claimState, "CommunicateTerritorialClaim",
        new { Observer = 3, ClaimId = 9L, Recipient = 2, Tick = 17L },
        service => service.CommunicateTerritorialClaim(3, 9, 2, 17));
    var knownClaim = claimState with
    {
        Claims = [visibleClaim with { KnownToCivilizationIds = [1, 2] }],
    };
    Command("claim-recognize", knownClaim, "RespondToTerritorialClaim",
        new { Observer = 2, ClaimId = 9L, Response = TerritorialClaimResponse.Recognized, Tick = 18L },
        service => service.RespondToTerritorialClaim(2, 9,
            TerritorialClaimResponse.Recognized, 18));
    Command("claim-own-response", knownClaim, "RespondToTerritorialClaim",
        new { Observer = 1, ClaimId = 9L, Response = TerritorialClaimResponse.Disputed, Tick = 18L },
        service => service.RespondToTerritorialClaim(1, 9,
            TerritorialClaimResponse.Disputed, 18));
    Command("claim-same-response-retry", knownClaim with
    {
        ClaimResponses = [new(9, 2, TerritorialClaimResponse.Recognized, 17)],
    }, "RespondToTerritorialClaim",
        new { Observer = 2, ClaimId = 9L, Response = TerritorialClaimResponse.Recognized, Tick = 18L },
        service => service.RespondToTerritorialClaim(2, 9,
            TerritorialClaimResponse.Recognized, 18));

    void Availability(string name, DiplomacyStateSnapshot initial, int observer)
    {
        var state = DiplomacyState.Restore(initial);
        var view = state.BuildViewFor(observer);
        var input = new { Snapshot = initial, Observer = observer, View = view };
        var owned = JsonSerializer.Serialize(input, json);
        var before = Fingerprint();
        IReadOnlyList<ObserverDiplomacyActionAvailability>? result = null;
        Exception? error = null;
        try
        {
            result = ObserverDiplomacyActionAvailabilityBuilder.Build(view);
        }
        catch (Exception caught)
        {
            error = caught;
        }
        rows.Add(new
        {
            Name = name,
            Kind = "Availability",
            Input = Node(input),
            InputBefore = owned,
            InputAfter = JsonSerializer.Serialize(input, json),
            BeforeFingerprint = before,
            AfterFingerprint = Fingerprint(),
            Error = Error(error),
            Result = Node(result),
            State = Projection(state),
        });
    }

    Availability("availability-empty", Empty(), 1);
    Availability("availability-latest-tie-ordinal", Mutual() with
    {
        Contacts =
        [
            Contact(1, 2, tick: 20, id: "z-contact"),
            Contact(1, 2, tick: 20, id: "a-contact"),
            Contact(1, 3, tick: 19, communication: false,
                awareness: ContactAwareness.Identified,
                condition: ContactCondition.StaleOrLost),
            new(1, "unidentified", null, 1, 30, 8,
                ContactAwareness.DetectedUnidentified, ContactCondition.Active, false, .4),
        ],
        Relationships = [new(1, 2, DiplomaticPoliticalState.AtWar,
            .1, .8, .2, .3, .1, [])],
        Proposals =
        [
            new(1, 2, 1, DiplomaticProposalKind.Demand, null,
                DiplomaticProposalStatus.Pending, 15, null, "incoming", null),
            new(2, 1, 2, DiplomaticProposalKind.Demand, null,
                DiplomaticProposalStatus.Pending, 16, null, "outgoing", null),
        ],
        Agreements = [new(1, 1, 2, DiplomaticAgreementType.Cooperation,
            DiplomaticAgreementStatus.Active, 10, null, null)],
        NextProposalId = 3,
        NextAgreementId = 2,
    }, 1);

    var galaxy = new GalaxyGenerator().Generate(
        0x434F_4D4D_414E_4432L,
        new GalaxyGenerationSettings
        {
            SystemCount = 48,
            PreWarpCivilizationCount = 4,
            AncientCivilizationCount = 0,
            Radius = 620.0f,
        });
    var campaignCivilizations = galaxy.Civilizations
        .OrderBy(civilization => civilization.Id)
        .Take(3)
        .ToArray();
    if (campaignCivilizations.Length != 3)
        throw new InvalidOperationException("Campaign fixture requires three civilizations.");
    var first = campaignCivilizations[0];
    var second = campaignCivilizations[1];
    var hiddenSystems = galaxy.Systems
        .Where(system => !galaxy.Knowledge.IsSystemKnown(first.Id, system.Id))
        .Take(2)
        .ToArray();
    if (hiddenSystems.Length != 2)
        throw new InvalidOperationException("Campaign fixture requires two initially hidden systems.");

    object World() => new
    {
        CivilizationIds = galaxy.Civilizations.Select(value => value.Id).ToArray(),
        SystemIds = galaxy.Systems.Select(value => value.Id).ToArray(),
        Colonies = galaxy.Colonies.Select(value => new
        {
            value.CivilizationId,
            value.SystemId,
        }).ToArray(),
        KnownSystems = galaxy.Civilizations.Select(value => new
        {
            CivilizationId = value.Id,
            SystemIds = galaxy.Knowledge.GetKnownSystems(value.Id).ToArray(),
        }).ToArray(),
    };

    void Campaign(
        string name, string operation, DiplomacyStateSnapshot initial,
        object arguments, Func<DiplomacyState, object?> invoke)
    {
        var state = DiplomacyState.Restore(initial);
        var world = World();
        var input = new
        {
            Snapshot = initial,
            World = world,
            Operation = operation,
            Arguments = arguments,
        };
        var owned = JsonSerializer.Serialize(input, json);
        var before = Fingerprint();
        object? result = null;
        Exception? error = null;
        try
        {
            result = invoke(state);
        }
        catch (Exception caught)
        {
            error = caught;
        }
        rows.Add(new
        {
            Name = name,
            Kind = "Campaign",
            Input = Node(input),
            InputBefore = owned,
            InputAfter = JsonSerializer.Serialize(input, json),
            BeforeFingerprint = before,
            AfterFingerprint = Fingerprint(),
            Error = Error(error),
            Result = Node(result),
            State = Projection(state),
        });
    }

    Campaign("campaign-claim-invalid-observer", "AssertTerritorialClaim", Empty(),
        new { Observer = -1, SystemId = hiddenSystems[0].Id, Tick = 1L }, state =>
        {
            var runtime = new DiplomacyCampaignRuntimeCoordinator(state);
            return new CampaignDiplomacyTerritorialClaimCommandService(galaxy, runtime)
                .AssertTerritorialClaim(-1, hiddenSystems[0].Id, 1);
        });
    Campaign("campaign-claim-unknown-existing", "AssertTerritorialClaim", Empty(),
        new { Observer = first.Id, SystemId = hiddenSystems[0].Id, Tick = 1L }, state =>
        {
            var runtime = new DiplomacyCampaignRuntimeCoordinator(state);
            return new CampaignDiplomacyTerritorialClaimCommandService(galaxy, runtime)
                .AssertTerritorialClaim(first.Id, hiddenSystems[0].Id, 1);
        });
    Campaign("campaign-claim-nonexistent", "AssertTerritorialClaim", Empty(),
        new { Observer = first.Id, SystemId = int.MaxValue, Tick = 1L }, state =>
        {
            var runtime = new DiplomacyCampaignRuntimeCoordinator(state);
            return new CampaignDiplomacyTerritorialClaimCommandService(galaxy, runtime)
                .AssertTerritorialClaim(first.Id, int.MaxValue, 1);
        });

    galaxy.Knowledge.RevealSystem(first.Id, hiddenSystems[0].Id);
    Campaign("campaign-claim-success", "AssertTerritorialClaim", Empty(),
        new { Observer = first.Id, SystemId = hiddenSystems[0].Id, Tick = 2L }, state =>
        {
            var runtime = new DiplomacyCampaignRuntimeCoordinator(state);
            return new CampaignDiplomacyTerritorialClaimCommandService(galaxy, runtime)
                .AssertTerritorialClaim(first.Id, hiddenSystems[0].Id, 2);
        });
    var campaignClaim = new TerritorialClaimSnapshot(
        1, first.Id, hiddenSystems[0].Id, 2, true, [first.Id]);
    Campaign("campaign-claim-idempotent", "AssertTerritorialClaim", Empty() with
    {
        Claims = [campaignClaim],
        NextClaimId = 2,
    }, new { Observer = first.Id, SystemId = hiddenSystems[0].Id, Tick = 3L }, state =>
    {
        var runtime = new DiplomacyCampaignRuntimeCoordinator(state);
        return new CampaignDiplomacyTerritorialClaimCommandService(galaxy, runtime)
            .AssertTerritorialClaim(first.Id, hiddenSystems[0].Id, 3);
    });

    var campaignMutual = Empty() with
    {
        Contacts =
        [
            Contact(first.Id, second.Id),
            Contact(second.Id, first.Id),
        ],
    };
    Campaign("campaign-warning-channel-first", "IssueBorderWarning", Empty(),
        new
        {
            Issuer = first.Id,
            Recipient = second.Id,
            SystemId = hiddenSystems[1].Id,
            Tick = 4L,
        }, state =>
        {
            var runtime = new DiplomacyCampaignRuntimeCoordinator(state);
            return new CampaignDiplomacyBorderWarningCommandService(galaxy, runtime)
                .IssueBorderWarning(first.Id, second.Id, hiddenSystems[1].Id, 4);
        });
    Campaign("campaign-warning-unknown-existing", "IssueBorderWarning", campaignMutual,
        new
        {
            Issuer = first.Id,
            Recipient = second.Id,
            SystemId = hiddenSystems[1].Id,
            Tick = 4L,
        }, state =>
        {
            var runtime = new DiplomacyCampaignRuntimeCoordinator(state);
            return new CampaignDiplomacyBorderWarningCommandService(galaxy, runtime)
                .IssueBorderWarning(first.Id, second.Id, hiddenSystems[1].Id, 4);
        });

    galaxy.Knowledge.RevealSystem(first.Id, hiddenSystems[1].Id);
    Campaign("campaign-warning-no-territorial-basis", "IssueBorderWarning", campaignMutual,
        new
        {
            Issuer = first.Id,
            Recipient = second.Id,
            SystemId = hiddenSystems[1].Id,
            Tick = 5L,
        }, state =>
        {
            var runtime = new DiplomacyCampaignRuntimeCoordinator(state);
            return new CampaignDiplomacyBorderWarningCommandService(galaxy, runtime)
                .IssueBorderWarning(first.Id, second.Id, hiddenSystems[1].Id, 5);
        });
    var warningClaim = new TerritorialClaimSnapshot(
        1, first.Id, hiddenSystems[1].Id, 5, true, [first.Id]);
    var warningState = campaignMutual with
    {
        Claims = [warningClaim],
        NextClaimId = 2,
    };
    Campaign("campaign-warning-claim-basis", "IssueBorderWarning", warningState,
        new
        {
            Issuer = first.Id,
            Recipient = second.Id,
            SystemId = hiddenSystems[1].Id,
            Tick = 6L,
        }, state =>
        {
            var runtime = new DiplomacyCampaignRuntimeCoordinator(state);
            return new CampaignDiplomacyBorderWarningCommandService(galaxy, runtime)
                .IssueBorderWarning(first.Id, second.Id, hiddenSystems[1].Id, 6);
        });
    Campaign("campaign-warning-same-tick-retry", "IssueBorderWarning", warningState with
    {
        RecentHistory = [new(1, 6, DiplomaticEventKind.BorderWarningIssued,
            first.Id, second.Id, hiddenSystems[1].Id,
            $"Civilization {first.Id} issued a border warning to civilization {second.Id} over system {hiddenSystems[1].Id}.",
            [first.Id, second.Id])],
        NextEventId = 2,
    }, new
    {
        Issuer = first.Id,
        Recipient = second.Id,
        SystemId = hiddenSystems[1].Id,
        Tick = 6L,
    }, state =>
    {
        var runtime = new DiplomacyCampaignRuntimeCoordinator(state);
        return new CampaignDiplomacyBorderWarningCommandService(galaxy, runtime)
            .IssueBorderWarning(first.Id, second.Id, hiddenSystems[1].Id, 6);
    });
    Campaign("campaign-warning-granted-access", "IssueBorderWarning", warningState with
    {
        AccessPermissions = [new(first.Id, second.Id, AccessPermission.Granted, 6)],
    }, new
    {
        Issuer = first.Id,
        Recipient = second.Id,
        SystemId = hiddenSystems[1].Id,
        Tick = 7L,
    }, state =>
    {
        var runtime = new DiplomacyCampaignRuntimeCoordinator(state);
        return new CampaignDiplomacyBorderWarningCommandService(galaxy, runtime)
            .IssueBorderWarning(first.Id, second.Id, hiddenSystems[1].Id, 7);
    });
    var colonySystemId = first.HomeSystemId;
    galaxy.Knowledge.RevealSystem(first.Id, colonySystemId);
    Campaign("campaign-warning-colony-basis", "IssueBorderWarning", campaignMutual,
        new
        {
            Issuer = first.Id,
            Recipient = second.Id,
            SystemId = colonySystemId,
            Tick = 8L,
        }, state =>
        {
            var runtime = new DiplomacyCampaignRuntimeCoordinator(state);
            return new CampaignDiplomacyBorderWarningCommandService(galaxy, runtime)
                .IssueBorderWarning(first.Id, second.Id, colonySystemId, 8);
        });

    var finalFingerprint = Fingerprint();
    var document = new
    {
        SchemaVersion = 1,
        SourceFiles = sourceNames,
        SourceFingerprint = finalFingerprint,
        Rows = rows,
        Scope = new
        {
            TotalRows = rows.Count,
            CampaignRows = 12,
        },
    };
    Directory.CreateDirectory(Path.GetDirectoryName(output)!);
    File.WriteAllText(output, JsonSerializer.Serialize(document, json));
    Console.WriteLine($"Wrote {rows.Count} actual-source observer command rows to {output}");
    return 0;
}
catch (Exception exception)
{
    Console.Error.WriteLine(exception);
    Console.Error.WriteLine($"CurrentDirectory: {Environment.CurrentDirectory}");
    Console.Error.WriteLine($"SourceRoot: {diagnosticRoot}");
    Console.Error.WriteLine($"FixturePath: {diagnosticFixture}");
    return 1;
}
