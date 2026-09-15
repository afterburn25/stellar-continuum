using System.Globalization;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Simulation.Diplomacy;

CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture=CultureInfo.InvariantCulture;
var diagnosticRoot=args.Length>0?args[0]:"<missing>";
var diagnosticFixture=args.Length>1?args[1]:"<missing>";
try
{
    if(args.Length!=2)throw new ArgumentException("Expected repository root and output fixture path.");
    var root=Path.GetFullPath(args[0]);var output=Path.GetFullPath(args[1]);
    var source=Path.Combine(root,"src","Game","Simulation","Diplomacy","DiplomacySystem.cs");
    string Fingerprint()=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(source)));
    var json=new JsonSerializerOptions{WriteIndented=true};
    JsonNode? Node(object? value)=>JsonSerializer.SerializeToNode(value,json);
    object? Error(Exception? value)=>value is null?null:new{Type=value.GetType().Name,value.Message};
    DiplomacyStateSnapshot Empty()=>new([],[],[],[],[],[],[],[],1,1,1,1);
    DiplomaticContactSnapshot Contact(int observer,int target,bool communication=true,
        ContactAwareness awareness=ContactAwareness.CommunicationAvailable,
        ContactCondition condition=ContactCondition.Active,long tick=10,string? id=null)=>
        new(observer,id??$"civilization:{target}",target,1,tick,7,awareness,condition,
            communication,.75);
    DiplomacyStateSnapshot Mutual()=>Empty() with
        {Contacts=[Contact(1,2),Contact(2,1)]};
    DiplomacyStateSnapshot OneWay()=>Empty() with
        {Contacts=[Contact(1,2,false,ContactAwareness.Identified)]};
    object Projection(DiplomacyState state)=>new
    {
        Snapshot=Node(state.Snapshot()),View1=Node(state.BuildViewFor(1)),
        View2=Node(state.BuildViewFor(2)),View3=Node(state.BuildViewFor(3)),
    };
    var rows=new List<object>();
    void Case(string name,DiplomacyStateSnapshot initial,string operation,
        object arguments,Func<DiplomacySimulation,object?> call)
    {
        var state=DiplomacyState.Restore(initial);var simulation=new DiplomacySimulation(state);
        var input=new{Snapshot=initial,Operation=operation,Arguments=arguments};
        var owned=JsonSerializer.Serialize(input,json);var before=Fingerprint();
        object? result=null;Exception? error=null;
        try{result=call(simulation);}catch(Exception caught){error=caught;}
        var after=Fingerprint();
        rows.Add(new{Name=name,Input=Node(input),InputBefore=owned,
            InputAfter=JsonSerializer.Serialize(input,json),BeforeFingerprint=before,
            AfterFingerprint=after,Error=Error(error),Return=Node(result),
            State=Projection(state)});
    }
    object? Void(Action action){action();return null;}

    var unidentified=new FirstContactOpportunity(1,"unknown",null,10,7,
        ContactAwareness.DetectedUnidentified,ContactCondition.Active,false,.005);
    Case("contact-unidentified",Empty(),"contact",new{Opportunity=unidentified},
        sim=>sim.ProcessContactOpportunity(unidentified));
    var established=new FirstContactOpportunity(1,"known",2,11,8,
        ContactAwareness.ContactEstablished,ContactCondition.Active,false,.5);
    Case("contact-established-creates-relationship",Empty(),"contact",
        new{Opportunity=established},sim=>sim.ProcessContactOpportunity(established));
    var communicating=established with {Awareness=ContactAwareness.ContactPossible,
        CommunicationAvailable=true,Confidence=1};
    Case("contact-communication-event",Empty(),"contact",new{Opportunity=communicating},
        sim=>sim.ProcessContactOpportunity(communicating));
    Case("contact-invalid-precedes-mutation",Empty(),"contact",
        new{Opportunity=established with {ObservedAtTick=-1}},
        sim=>sim.ProcessContactOpportunity(established with {ObservedAtTick=-1}));

    Case("lost-valid",OneWay(),"lost",new{Observer=1,ContactId="civilization:2",Tick=12L},
        sim=>Void(()=>sim.MarkContactLost(1,"civilization:2",12)));
    Case("lost-unknown",Empty(),"lost",new{Observer=1,ContactId="missing",Tick=12L},
        sim=>Void(()=>sim.MarkContactLost(1,"missing",12)));
    Case("lost-backwards",OneWay(),"lost",new{Observer=1,ContactId="civilization:2",Tick=9L},
        sim=>Void(()=>sim.MarkContactLost(1,"civilization:2",9)));

    Case("access-granted",Mutual(),"access",new{Grantor=1,Visitor=2,
        Permission=AccessPermission.Granted,Tick=20L},
        sim=>Void(()=>sim.SetAccessPermission(1,2,AccessPermission.Granted,20)));
    Case("access-negative-tick-before-self",Empty(),"access",new{Grantor=1,Visitor=1,
        Permission=AccessPermission.Granted,Tick=-1L},
        sim=>Void(()=>sim.SetAccessPermission(1,1,AccessPermission.Granted,-1)));
    Case("access-self",Empty(),"access",new{Grantor=1,Visitor=1,
        Permission=AccessPermission.Granted,Tick=1L},
        sim=>Void(()=>sim.SetAccessPermission(1,1,AccessPermission.Granted,1)));
    Case("access-needs-mutual",OneWay(),"access",new{Grantor=1,Visitor=2,
        Permission=(AccessPermission)99,Tick=1L},
        sim=>Void(()=>sim.SetAccessPermission(1,2,(AccessPermission)99,1)));
    Case("access-unknown-enum-name",Mutual(),"access",new{Grantor=1,Visitor=2,
        Permission=(AccessPermission)99,Tick=2L},
        sim=>Void(()=>sim.SetAccessPermission(1,2,(AccessPermission)99,2)));

    Case("claim-valid",Empty(),"claim",new{Claimant=1,SystemId=7,Tick=2L},
        sim=>sim.AssertTerritorialClaim(1,7,2));
    Case("claim-negative-claimant-first",Empty(),"claim",new{Claimant=-1,SystemId=-1,Tick=2L},
        sim=>sim.AssertTerritorialClaim(-1,-1,2));
    var claimState=Empty() with {Claims=[new(4,1,7,2,true,[1])],NextClaimId=5};
    Case("claim-duplicate-still-records",claimState,"claim",new{Claimant=1,SystemId=7,Tick=3L},
        sim=>sim.AssertTerritorialClaim(1,7,3));
    Case("communicate-claim",Mutual() with {Claims=[new(1,1,7,1,true,[1])],NextClaimId=2},
        "communicate-claim",new{ClaimId=1L,Recipient=2,Tick=4L},
        sim=>Void(()=>sim.CommunicateTerritorialClaim(1,2,4)));
    Case("communicate-unknown-claim",Mutual(),"communicate-claim",
        new{ClaimId=99L,Recipient=2,Tick=4L},
        sim=>Void(()=>sim.CommunicateTerritorialClaim(99,2,4)));
    Case("communicate-claim-needs-mutual",OneWay() with
        {Claims=[new(1,1,7,1,true,[1])],NextClaimId=2},"communicate-claim",
        new{ClaimId=1L,Recipient=2,Tick=4L},
        sim=>Void(()=>sim.CommunicateTerritorialClaim(1,2,4)));
    var knownClaim=Mutual() with {Claims=[new(1,1,7,1,true,[1,2])],NextClaimId=2};
    Case("respond-claim-none-before-lookup",Empty(),"respond-claim",
        new{ClaimId=99L,Responder=2,Response=TerritorialClaimResponse.None,Tick=4L},
        sim=>Void(()=>sim.RespondToTerritorialClaim(99,2,TerritorialClaimResponse.None,4)));
    Case("respond-claim-success",knownClaim,"respond-claim",new{ClaimId=1L,Responder=2,
        Response=TerritorialClaimResponse.Disputed,Tick=4L},
        sim=>Void(()=>sim.RespondToTerritorialClaim(1,2,TerritorialClaimResponse.Disputed,4)));
    Case("respond-claim-unknown-enum-name",knownClaim,"respond-claim",
        new{ClaimId=1L,Responder=2,Response=(TerritorialClaimResponse)99,Tick=4L},
        sim=>Void(()=>sim.RespondToTerritorialClaim(1,2,(TerritorialClaimResponse)99,4)));
    Case("respond-claim-unknown-to-responder",Mutual() with
        {Claims=[new(1,1,7,1,true,[1])],NextClaimId=2},"respond-claim",
        new{ClaimId=1L,Responder=2,Response=TerritorialClaimResponse.Recognized,Tick=4L},
        sim=>Void(()=>sim.RespondToTerritorialClaim(1,2,TerritorialClaimResponse.Recognized,4)));

    Case("border-warning",Mutual(),"warning",new{Issuer=1,Recipient=2,SystemId=7,Tick=6L},
        sim=>Void(()=>sim.IssueBorderWarning(1,2,7,6)));
    Case("trespass-one-way-private",OneWay(),"trespass",
        new{Territorial=1,Intruder=2,SystemId=7,Tick=6L},
        sim=>Void(()=>sim.RecordTrespass(1,2,7,6)));
    Case("trespass-hidden",Empty(),"trespass",
        new{Territorial=1,Intruder=2,SystemId=7,Tick=6L},
        sim=>Void(()=>sim.RecordTrespass(1,2,7,6)));
    Case("trespass-reciprocal-visible",Mutual(),"trespass",
        new{Territorial=1,Intruder=2,SystemId=7,Tick=6L},
        sim=>Void(()=>sim.RecordTrespass(1,2,7,6)));
    Case("trespass-authorized",Mutual() with {AccessPermissions=[new(1,2,AccessPermission.Granted,1)]},
        "trespass",new{Territorial=1,Intruder=2,SystemId=7,Tick=6L},
        sim=>Void(()=>sim.RecordTrespass(1,2,7,6)));

    Case("proposal-demand",Mutual(),"send",new{Proposer=1,Recipient=2,
        Kind=DiplomaticProposalKind.Demand,Tick=8L,Summary="demand",AgreementType=(DiplomaticAgreementType?)null,Terms=(string?)null},
        sim=>sim.SendProposal(1,2,DiplomaticProposalKind.Demand,8,"demand"));
    Case("proposal-unicode-whitespace-summary",Mutual(),"send",new{Proposer=1,Recipient=2,
        Kind=DiplomaticProposalKind.Demand,Tick=8L,Summary="\u3000",AgreementType=(DiplomaticAgreementType?)null,Terms=(string?)null},
        sim=>sim.SendProposal(1,2,DiplomaticProposalKind.Demand,8,"\u3000"));
    Case("proposal-mutual-before-agreement-type",OneWay(),"send",new{Proposer=1,Recipient=2,
        Kind=DiplomaticProposalKind.Agreement,Tick=8L,Summary="agreement",AgreementType=(DiplomaticAgreementType?)null,Terms=(string?)null},
        sim=>sim.SendProposal(1,2,DiplomaticProposalKind.Agreement,8,"agreement"));
    Case("proposal-agreement-needs-type",Mutual(),"send",new{Proposer=1,Recipient=2,
        Kind=DiplomaticProposalKind.Agreement,Tick=8L,Summary="agreement",AgreementType=(DiplomaticAgreementType?)null,Terms=(string?)null},
        sim=>sim.SendProposal(1,2,DiplomaticProposalKind.Agreement,8,"agreement"));
    Case("proposal-trade-needs-terms",Mutual(),"send",new{Proposer=1,Recipient=2,
        Kind=DiplomaticProposalKind.TradeOffer,Tick=8L,Summary="trade",AgreementType=(DiplomaticAgreementType?)null,Terms=(string?)null},
        sim=>sim.SendProposal(1,2,DiplomaticProposalKind.TradeOffer,8,"trade"));

    DiplomaticProposalSnapshot Proposal(DiplomaticProposalKind kind,
        DiplomaticAgreementType? type=null,DiplomaticProposalStatus status=DiplomaticProposalStatus.Pending)=>
        new(1,1,2,kind,type,status,8,status==DiplomaticProposalStatus.Pending?null:9,"proposal","terms");
    Case("proposal-accept-access-effects-first",Mutual() with {Proposals=[Proposal(DiplomaticProposalKind.AccessRequest)],NextProposalId=2},
        "respond-proposal",new{ProposalId=1L,Responder=2,Accept=true,Tick=10L},
        sim=>Void(()=>sim.RespondToProposal(1,2,true,10)));
    Case("proposal-accept-demand",Mutual() with {Proposals=[Proposal(DiplomaticProposalKind.Demand)],NextProposalId=2},
        "respond-proposal",new{ProposalId=1L,Responder=2,Accept=true,Tick=10L},
        sim=>Void(()=>sim.RespondToProposal(1,2,true,10)));
    Case("proposal-accept-trade",Mutual() with {Proposals=[Proposal(DiplomaticProposalKind.TradeOffer)],NextProposalId=2},
        "respond-proposal",new{ProposalId=1L,Responder=2,Accept=true,Tick=10L},
        sim=>Void(()=>sim.RespondToProposal(1,2,true,10)));
    Case("proposal-accept-peace",Mutual() with {Relationships=[new(1,2,DiplomaticPoliticalState.AtWar,0,0,0,0,0,[])],Proposals=[Proposal(DiplomaticProposalKind.PeaceOffer)],NextProposalId=2},
        "respond-proposal",new{ProposalId=1L,Responder=2,Accept=true,Tick=10L},
        sim=>Void(()=>sim.RespondToProposal(1,2,true,10)));
    Case("proposal-accept-ceasefire",Mutual() with {Relationships=[new(1,2,DiplomaticPoliticalState.AtWar,0,0,0,0,0,[])],Proposals=[Proposal(DiplomaticProposalKind.CeasefireOffer)],NextProposalId=2},
        "respond-proposal",new{ProposalId=1L,Responder=2,Accept=true,Tick=10L},
        sim=>Void(()=>sim.RespondToProposal(1,2,true,10)));
    Case("proposal-accept-agreement-access",Mutual() with {Proposals=[Proposal(DiplomaticProposalKind.Agreement,DiplomaticAgreementType.Access)],NextProposalId=2},
        "respond-proposal",new{ProposalId=1L,Responder=2,Accept=true,Tick=10L},
        sim=>Void(()=>sim.RespondToProposal(1,2,true,10)));
    Case("proposal-accept-agreement-unknown-enum-name",Mutual() with {Proposals=[Proposal(DiplomaticProposalKind.Agreement,(DiplomaticAgreementType)99)],NextProposalId=2},
        "respond-proposal",new{ProposalId=1L,Responder=2,Accept=true,Tick=10L},
        sim=>Void(()=>sim.RespondToProposal(1,2,true,10)));
    Case("proposal-accept-agreement-missing-type",Mutual() with {Proposals=[Proposal(DiplomaticProposalKind.Agreement)],NextProposalId=2},
        "respond-proposal",new{ProposalId=1L,Responder=2,Accept=true,Tick=10L},
        sim=>Void(()=>sim.RespondToProposal(1,2,true,10)));
    Case("proposal-nonaggression-at-war",Mutual() with {Relationships=[new(1,2,DiplomaticPoliticalState.AtWar,0,0,0,0,0,[])],Proposals=[Proposal(DiplomaticProposalKind.Agreement,DiplomaticAgreementType.NonAggression)],NextProposalId=2},
        "respond-proposal",new{ProposalId=1L,Responder=2,Accept=true,Tick=10L},
        sim=>Void(()=>sim.RespondToProposal(1,2,true,10)));
    Case("proposal-reject",Mutual() with {Proposals=[Proposal(DiplomaticProposalKind.Demand)],NextProposalId=2},
        "respond-proposal",new{ProposalId=1L,Responder=2,Accept=false,Tick=10L},
        sim=>Void(()=>sim.RespondToProposal(1,2,false,10)));
    Case("proposal-wrong-responder",Mutual() with {Proposals=[Proposal(DiplomaticProposalKind.Demand)],NextProposalId=2},
        "respond-proposal",new{ProposalId=1L,Responder=1,Accept=true,Tick=10L},
        sim=>Void(()=>sim.RespondToProposal(1,1,true,10)));
    Case("proposal-already-resolved",Mutual() with {Proposals=[Proposal(DiplomaticProposalKind.Demand,status:DiplomaticProposalStatus.Rejected)],NextProposalId=2},
        "respond-proposal",new{ProposalId=1L,Responder=2,Accept=true,Tick=10L},
        sim=>Void(()=>sim.RespondToProposal(1,2,true,10)));
    Case("proposal-invalid-kind-default",Mutual() with {Proposals=[Proposal((DiplomaticProposalKind)99)],NextProposalId=2},
        "respond-proposal",new{ProposalId=1L,Responder=2,Accept=true,Tick=10L},
        sim=>Void(()=>sim.RespondToProposal(1,2,true,10)));
    Case("proposal-withdraw",Mutual() with {Proposals=[Proposal(DiplomaticProposalKind.Demand)],NextProposalId=2},
        "withdraw",new{ProposalId=1L,Proposer=1,Tick=10L},
        sim=>Void(()=>sim.WithdrawProposal(1,1,10)));
    Case("proposal-withdraw-wrong-proposer",Mutual() with {Proposals=[Proposal(DiplomaticProposalKind.Demand)],NextProposalId=2},
        "withdraw",new{ProposalId=1L,Proposer=2,Tick=10L},
        sim=>Void(()=>sim.WithdrawProposal(1,2,10)));
    Case("proposal-expire-backwards",Mutual() with {Proposals=[Proposal(DiplomaticProposalKind.Demand)],NextProposalId=2},
        "expire",new{ProposalId=1L,Tick=7L},sim=>Void(()=>sim.ExpireProposal(1,7)));
    Case("proposal-expire-resolved-noop",Mutual() with {Proposals=[Proposal(DiplomaticProposalKind.Demand,status:DiplomaticProposalStatus.Accepted)],NextProposalId=2},
        "expire",new{ProposalId=1L,Tick=11L},sim=>Void(()=>sim.ExpireProposal(1,11)));

    var impact=new RelationshipImpact(.2,.3,.4,.5,.6,.8,"observed incident");
    Case("relationship-impact",OneWay(),"impact",new{Observer=1,Target=2,Impact=impact,Tick=12L},
        sim=>Void(()=>sim.ApplyRelationshipImpact(1,2,impact,12)));
    var invalidImpact=impact with {TrustDelta=2};
    Case("relationship-impact-validation-before-knowledge",Empty(),"impact",
        new{Observer=1,Target=2,Impact=invalidImpact,Tick=12L},
        sim=>Void(()=>sim.ApplyRelationshipImpact(1,2,invalidImpact,12)));
    Case("hostile-one-way",OneWay(),"hostile",new{A=1,B=2,Tick=13L,Reason="threat"},
        sim=>Void(()=>sim.SetHostile(1,2,13,"threat")));
    Case("hostile-whitespace-before-knowledge",Empty(),"hostile",new{A=1,B=2,Tick=13L,Reason="\u2003"},
        sim=>Void(()=>sim.SetHostile(1,2,13,"\u2003")));
    Case("hostile-hidden",Empty(),"hostile",new{A=1,B=2,Tick=13L,Reason="threat"},
        sim=>Void(()=>sim.SetHostile(1,2,13,"threat")));

    var warState=OneWay() with
    {
        Relationships=[new(1,2,DiplomaticPoliticalState.Peace,0,0,0,0,0,[])],
        Agreements=[new(4,1,2,DiplomaticAgreementType.Trade,DiplomaticAgreementStatus.Active,2,null,"trade"),
            new(2,1,2,DiplomaticAgreementType.Access,DiplomaticAgreementStatus.Active,1,null,null)],
        NextAgreementId=5,
    };
    Case("war-one-way-termination-order",warState,"war",new{Declarer=1,Target=2,Tick=20L},
        sim=>Void(()=>sim.DeclareWar(1,2,20)));
    var reciprocalWar=warState with {Contacts=[Contact(1,2),Contact(2,1)]};
    Case("war-reciprocal-audience",reciprocalWar,"war",new{Declarer=1,Target=2,Tick=20L},
        sim=>Void(()=>sim.DeclareWar(1,2,20)));
    var alreadyWar=warState with {Relationships=[new(1,2,DiplomaticPoliticalState.AtWar,0,0,0,0,0,[])]};
    Case("war-already-noop",alreadyWar,"war",new{Declarer=1,Target=2,Tick=20L},
        sim=>Void(()=>sim.DeclareWar(1,2,20)));

    var fixture=new{Source="src/Game/Simulation/Diplomacy/DiplomacySystem.cs:387-553",
        SourceFingerprint=Fingerprint(),FinalFingerprint=Fingerprint(),RowCount=rows.Count,Rows=rows};
    Directory.CreateDirectory(Path.GetDirectoryName(output)??throw new InvalidOperationException("Output has no parent."));
    File.WriteAllText(output,JsonSerializer.Serialize(fixture,json));
    Console.WriteLine($"diplomacy simulation oracle: {rows.Count} rows -> {output}");
}
catch(Exception error)
{
    Console.Error.WriteLine(error.ToString());
    Console.Error.WriteLine($"cwd={Environment.CurrentDirectory}");
    Console.Error.WriteLine($"repoRoot={diagnosticRoot}");
    Console.Error.WriteLine($"fixture={diagnosticFixture}");
    Environment.ExitCode=1;
}
