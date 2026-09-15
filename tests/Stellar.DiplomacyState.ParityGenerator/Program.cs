using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.Json.Nodes;
using Game.Simulation.Diplomacy;

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
    var sourcePath = Path.Combine(root, "src", "Game", "Simulation",
        "Diplomacy", "DiplomacySystem.cs");
    string Fingerprint() => Convert.ToHexString(SHA256.HashData(
        File.ReadAllBytes(sourcePath)));
    var json = new JsonSerializerOptions
    {
        WriteIndented = true,
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };
    JsonNode? Node(object? value) => JsonSerializer.SerializeToNode(value, json);
    object Error(Exception? error) => error is null ? null! :
        new { Type = error.GetType().Name, error.Message };
    var sourceFingerprint = Fingerprint();
    var rows = new List<object>();

    void Opportunity(string name, FirstContactOpportunity input)
    {
        var owned = JsonSerializer.Serialize(input, json);
        var before = Fingerprint();
        Exception? error = null;
        try { input.Validate(); } catch (Exception caught) { error = caught; }
        var after = Fingerprint();
        rows.Add(new { Name=name, Kind="opportunity", Input=Node(input),
            InputBefore=owned, InputAfter=JsonSerializer.Serialize(input,json),
            BeforeFingerprint=before, AfterFingerprint=after,
            Error=Error(error) });
    }
    FirstContactOpportunity O() => new(1,"contact:a",2,10,3,
        ContactAwareness.Identified,ContactCondition.Active,false,0.5);
    Opportunity("opportunity-valid", O());
    Opportunity("opportunity-negative-observer", O() with {ObserverCivilizationId=-1});
    Opportunity("opportunity-unicode-whitespace-id", O() with {ContactId="\u2003"});
    Opportunity("opportunity-negative-tick", O() with {ObservedAtTick=-1});
    Opportunity("opportunity-nan-confidence", O() with {Confidence=double.NaN});
    Opportunity("opportunity-confidence-high", O() with {Confidence=1.0001});
    Opportunity("opportunity-unknown", O() with {Awareness=ContactAwareness.Unknown});
    Opportunity("opportunity-self", O() with {TargetCivilizationId=1});
    Opportunity("opportunity-identified-null", O() with {TargetCivilizationId=null});
    Opportunity("opportunity-unidentified-exposes-target", O() with {Awareness=ContactAwareness.DetectedUnidentified});
    Opportunity("opportunity-communication-too-early", O() with {Awareness=ContactAwareness.Identified,CommunicationAvailable=true});
    Opportunity("opportunity-awareness-needs-flag", O() with {Awareness=ContactAwareness.CommunicationAvailable,CommunicationAvailable=false});
    Opportunity("opportunity-stale-communication", O() with {Awareness=ContactAwareness.CommunicationAvailable,CommunicationAvailable=true,Condition=ContactCondition.StaleOrLost});

    void Impact(string name, RelationshipImpact input)
    {
        var owned=JsonSerializer.Serialize(input,json);var before=Fingerprint();Exception? error=null;
        try { input.Validate(); } catch(Exception caught){error=caught;}
        var after=Fingerprint();rows.Add(new{Name=name,Kind="impact",Input=Node(input),InputBefore=owned,InputAfter=JsonSerializer.Serialize(input,json),BeforeFingerprint=before,AfterFingerprint=after,Error=Error(error)});
    }
    RelationshipImpact I()=>new(0.1,-0.2,0.3,0.4,0.5,0.6,"reason");
    Impact("impact-valid",I());
    Impact("impact-first-delta-order",I() with {TrustDelta=2,HostilityDelta=double.NaN});
    Impact("impact-nan-delta",I() with {FearDelta=double.NaN});
    Impact("impact-low-delta",I() with {RespectDelta=-1.1});
    Impact("impact-severity",I() with {GrievanceSeverity=double.PositiveInfinity});
    Impact("impact-unicode-whitespace-reason",I() with {Reason="\u3000"});

    DiplomacyStateSnapshot Empty() => new([],[],[],[],[],[],[],[],0,0,0,0);
    object Projection(DiplomacyState state) => new
    {
        Snapshot=Node(state.Snapshot()),
        Observer1=Node(state.BuildViewFor(1)),
        Observer2=Node(state.BuildViewFor(2)),
        Observer3=Node(state.BuildViewFor(3)),
        ContactById=Node(state.GetContact(1,"known")),
        ContactByTarget=Node(state.GetContact(1,2)),
        Relationship12=Node(state.GetRelationship(1,2)),
        Access12=state.GetAccessPermission(1,2),
        Transit12=state.IsTransitAuthorized(1,2),
    };
    void Restore(string name,DiplomacyStateSnapshot input,bool compact=false)
    {
        var owned=JsonSerializer.Serialize(input,json);var before=Fingerprint();DiplomacyState? value=null;Exception? error=null;
        try { value=DiplomacyState.Restore(input); } catch(Exception caught){error=caught;}
        var after=Fingerprint();object? result=null;
        if(value is not null) result=compact?new{SnapshotContacts=value.Snapshot().Contacts.Length,SnapshotProposals=value.Snapshot().Proposals.Length,SnapshotHistory=value.Snapshot().RecentHistory.Length,FirstContact=value.Snapshot().Contacts.FirstOrDefault()?.ContactId,LastContact=value.Snapshot().Contacts.LastOrDefault()?.ContactId}:Projection(value);
        rows.Add(new{Name=name,Kind="restore",Input=Node(input),InputBefore=owned,InputAfter=JsonSerializer.Serialize(input,json),Compact=compact,BeforeFingerprint=before,AfterFingerprint=after,Error=Error(error),Result=result});
    }
    Restore("restore-empty",Empty());
    Restore("restore-null-collections",new(null!,null!,null!,null!,null!,null!,null!,null!,-5,-4,-3,-2));
    var contacts=new[]{
        new DiplomaticContactSnapshot(-1,"skip",2,1,1,null,ContactAwareness.Identified,ContactCondition.Active,false,.5),
        new DiplomaticContactSnapshot(1,"\u3000",2,1,1,null,ContactAwareness.Identified,ContactCondition.Active,false,.5),
        new DiplomaticContactSnapshot(1,"\U00010000",3,-4,-2,8,ContactAwareness.DetectedUnidentified,ContactCondition.StaleOrLost,true,double.NaN),
        new DiplomaticContactSnapshot(1,"\uE000",null,2,4,null,ContactAwareness.DetectedUnidentified,ContactCondition.Active,false,2),
        new DiplomaticContactSnapshot(1,"known",2,1,5,9,ContactAwareness.Identified,ContactCondition.Active,true,.8),
        new DiplomaticContactSnapshot(1,"known-later",2,2,8,10,ContactAwareness.ContactEstablished,ContactCondition.Hostile,false,.9),
        new DiplomaticContactSnapshot(2,"reverse",1,2,8,10,ContactAwareness.Identified,ContactCondition.Active,false,.9),
    };
    var grievances=Enumerable.Range(0,18).Select(i=>new DiplomaticGrievanceSnapshot(i,3,i==2?double.NaN:i/10d,i==3?"\u2003":$"g{i}")).ToArray();
    var relationships=new[]{
        new DiplomaticRelationshipSnapshot(1,2,DiplomaticPoliticalState.Hostile,-1,double.NaN,.4,2,.6,grievances),
        new DiplomaticRelationshipSnapshot(1,3,DiplomaticPoliticalState.AtWar,.1,.2,.3,.4,.5,[]),
        new DiplomaticRelationshipSnapshot(4,4,DiplomaticPoliticalState.Peace,0,0,0,0,0,[]),
    };
    var access=new[]{new DiplomaticAccessSnapshot(1,1,AccessPermission.Granted,1),new DiplomaticAccessSnapshot(1,2,AccessPermission.Granted,-5),new DiplomaticAccessSnapshot(1,3,AccessPermission.Denied,2)};
    var claims=new[]{new TerritorialClaimSnapshot(0,1,1,1,true,[]),new TerritorialClaimSnapshot(7,1,9,-1,true,[2,2,3])};
    var responses=new[]{new TerritorialClaimResponseSnapshot(8,2,TerritorialClaimResponse.Disputed,2),new TerritorialClaimResponseSnapshot(7,2,TerritorialClaimResponse.Recognized,-2),new TerritorialClaimResponseSnapshot(7,3,TerritorialClaimResponse.Disputed,3)};
    var agreements=new[]{new DiplomaticAgreementSnapshot(4,1,2,DiplomaticAgreementType.Trade,DiplomaticAgreementStatus.Active,-2,null,"terms"),new DiplomaticAgreementSnapshot(5,1,3,DiplomaticAgreementType.Peace,DiplomaticAgreementStatus.Active,1,null,null)};
    var proposals=new[]{new DiplomaticProposalSnapshot(6,1,2,DiplomaticProposalKind.TradeOffer,null,DiplomaticProposalStatus.Pending,-1,null,"offer",null),new DiplomaticProposalSnapshot(7,1,3,DiplomaticProposalKind.Demand,null,DiplomaticProposalStatus.Pending,2,null,"hidden",null)};
    var history=new[]{new DiplomaticHistoryEventSnapshot(9,-2,DiplomaticEventKind.ContactObserved,1,2,null,"visible",[1,1,2]),new DiplomaticHistoryEventSnapshot(10,2,DiplomaticEventKind.ContactObserved,3,1,null,"hidden",[3]),new DiplomaticHistoryEventSnapshot(0,2,DiplomaticEventKind.ContactObserved,1,null,null,"skip",[1]),new DiplomaticHistoryEventSnapshot(11,2,DiplomaticEventKind.ContactObserved,1,null,null,"\u3000",[1])};
    Restore("restore-repair-and-privacy",new(contacts,relationships,access,claims,responses,agreements,proposals,history,1,1,1,1));
    var stableContacts=Enumerable.Range(0,40).Select(i=>
        new DiplomaticContactSnapshot(1,$"z{i:D2}",null,i,i,i,
            ContactAwareness.DetectedUnidentified,ContactCondition.Active,
            false,i/100d)).ToArray();
    stableContacts[1]=new(1,"known",2,1,50,101,ContactAwareness.Identified,
        ContactCondition.Active,false,.11);
    stableContacts[35]=new(1,"known",2,35,50,135,ContactAwareness.Identified,
        ContactCondition.Hostile,false,.35);
    Restore("restore-stable-duplicate-contacts",Empty() with
        {Contacts=stableContacts});
    var boundContacts=Enumerable.Range(0,DiplomacyState.MaxContactRecords+2).Select(i=>new DiplomaticContactSnapshot(1,$"c{i:D4}",null,i,i,null,ContactAwareness.DetectedUnidentified,ContactCondition.Active,false,.5)).ToArray();
    var boundProposals=Enumerable.Range(1,DiplomacyState.MaxStoredProposals+2).Select(i=>new DiplomaticProposalSnapshot(i,1,2,DiplomaticProposalKind.Demand,null,DiplomaticProposalStatus.Rejected,i,i,$"p{i}",null)).ToArray();
    var boundHistory=Enumerable.Range(1,DiplomacyState.MaxRecentHistoryEvents+2).Select(i=>new DiplomaticHistoryEventSnapshot(i,i,DiplomaticEventKind.ContactObserved,1,null,null,$"h{i}",[1])).ToArray();
    Restore("restore-bounds",Empty() with {Contacts=boundContacts,Proposals=boundProposals,RecentHistory=boundHistory},true);

    var primitiveInput = new
    {
        First = O() with { ContactId="primitive", Awareness=ContactAwareness.ContactEstablished },
        Second = O() with { ContactId="primitive", ObservedAtTick=12,
            ObservedSystemId=null, Awareness=ContactAwareness.CommunicationAvailable,
            CommunicationAvailable=true, Confidence=.9 },
        Grantor=1, Visitor=2, Permission=AccessPermission.Granted, Tick=12L,
        Claimant=1, SystemId=7, Responder=2,
        ClaimResponse=TerritorialClaimResponse.Disputed,
        ProposalKind=DiplomaticProposalKind.TradeOffer,
        AgreementType=DiplomaticAgreementType.Trade,
        Summary="primitive offer", EventSummary="primitive event",
        Audience=new[]{2,2,-1},
    };
    var primitiveState = new DiplomacyState();
    object? Internal(DiplomacyState state, string method, params object?[] values)
    {
        var selected=typeof(DiplomacyState).GetMethods(BindingFlags.Instance|BindingFlags.NonPublic)
            .Single(value=>value.Name==method);
        try{return selected.Invoke(state,values);}
        catch(TargetInvocationException error){throw error.InnerException!;}
    }
    var primitiveBefore=Fingerprint();Exception? primitiveError=null;
    try
    {
        _=Internal(primitiveState,"UpsertContact",primitiveInput.First);
        _=Internal(primitiveState,"UpsertContact",primitiveInput.Second);
        _=Internal(primitiveState,"Relationship",1,2);
        _=Internal(primitiveState,"SetAccess",primitiveInput.Grantor,primitiveInput.Visitor,
            primitiveInput.Permission,primitiveInput.Tick);
        var createdClaim=Internal(primitiveState,"CreateClaim",primitiveInput.Claimant,
            primitiveInput.SystemId,primitiveInput.Tick)!;
        var claimId=(long)createdClaim.GetType().GetProperty("Id")!.GetValue(createdClaim)!;
        _=Internal(primitiveState,"RespondToClaim",claimId,primitiveInput.Responder,
            primitiveInput.ClaimResponse,primitiveInput.Tick);
        _=Internal(primitiveState,"CreateProposal",1,2,primitiveInput.ProposalKind,
            primitiveInput.AgreementType,primitiveInput.Tick,
            primitiveInput.Summary,null);
        _=Internal(primitiveState,"ActivateAgreement",1,2,primitiveInput.AgreementType,
            primitiveInput.Tick,null);
        _=Internal(primitiveState,"Record",primitiveInput.Tick,DiplomaticEventKind.ContactObserved,
            1,2,primitiveInput.SystemId,primitiveInput.EventSummary,
            primitiveInput.Audience);
    }
    catch(Exception caught){primitiveError=caught;}
    var primitiveAfter=Fingerprint();
    rows.Add(new{Name="controlled-primitive-sequence",Kind="primitives",
        Input=Node(primitiveInput),BeforeFingerprint=primitiveBefore,
        AfterFingerprint=primitiveAfter,Error=Error(primitiveError),
        Result=primitiveError is null?Projection(primitiveState):null});

    object PrimitiveProjection(DiplomacyState state, string watchedContact)
    {
        var value=state.Snapshot();
        return new
        {
            ContactCount=value.Contacts.Length,
            FirstContact=value.Contacts.FirstOrDefault(),
            LastContact=value.Contacts.LastOrDefault(),
            WatchedContact=state.GetContact(1,watchedContact),
            ProposalCount=value.Proposals.Length,
            ProposalIds=value.Proposals.Select(item=>item.ProposalId).ToArray(),
            value.NextProposalId,
        };
    }
    void Primitive(string name,DiplomacyStateSnapshot initial,string operation,
        object input,string watchedContact,Action<DiplomacyState> call)
    {
        var state=DiplomacyState.Restore(initial);
        var envelope=new{Snapshot=initial,Arguments=input};
        var owned=JsonSerializer.Serialize(envelope,json);
        var stateBefore=JsonSerializer.Serialize(state.Snapshot(),json);
        var before=Fingerprint();Exception? error=null;
        try{call(state);}catch(Exception caught){error=caught;}
        var after=Fingerprint();
        var result=new
        {
            State=PrimitiveProjection(state,watchedContact),
            Unchanged=stateBefore==JsonSerializer.Serialize(state.Snapshot(),json),
        };
        rows.Add(new{Name=name,Kind="primitive-boundary",Operation=operation,
            Input=Node(envelope),InputBefore=owned,
            InputAfter=JsonSerializer.Serialize(envelope,json),
            BeforeFingerprint=before,AfterFingerprint=after,Error=Error(error),
            Result=result});
    }

    var fullContacts=Enumerable.Range(0,DiplomacyState.MaxContactRecords)
        .Select(i=>new DiplomaticContactSnapshot(1,$"full-{i:D4}",null,i,i,null,
            ContactAwareness.DetectedUnidentified,ContactCondition.Active,false,.5))
        .ToArray();
    var staleContacts=fullContacts.ToArray();
    staleContacts[0]=staleContacts[0] with
        {Condition=ContactCondition.StaleOrLost,LastObservedTick=0};
    var newContact=O() with {ContactId="new-contact",TargetCivilizationId=2,
        ObservedAtTick=5000,ObservedSystemId=9};
    Primitive("primitive-contact-full-evicts-stale",Empty() with {Contacts=staleContacts},
        "upsert-contact",new{Opportunity=newContact},"new-contact",
        state=>_=Internal(state,"UpsertContact",newContact));
    Primitive("primitive-contact-full-without-stale",Empty() with {Contacts=fullContacts},
        "upsert-contact",new{Opportunity=newContact},"new-contact",
        state=>_=Internal(state,"UpsertContact",newContact));

    var existingContact=new DiplomaticContactSnapshot(1,"stable",2,10,20,7,
        ContactAwareness.Identified,ContactCondition.Active,false,.6);
    var backward=O() with {ContactId="stable",TargetCivilizationId=2,
        ObservedAtTick=19};
    Primitive("primitive-contact-observation-backwards",
        Empty() with {Contacts=[existingContact]},"upsert-contact",
        new{Opportunity=backward},"stable",
        state=>_=Internal(state,"UpsertContact",backward));
    var reassigned=O() with {ContactId="stable",TargetCivilizationId=3,
        ObservedAtTick=21};
    Primitive("primitive-contact-stable-id-reassignment",
        Empty() with {Contacts=[existingContact]},"upsert-contact",
        new{Opportunity=reassigned},"stable",
        state=>_=Internal(state,"UpsertContact",reassigned));

    DiplomaticProposalSnapshot Pending(long id,int proposer,int recipient)=>new(
        id,proposer,recipient,DiplomaticProposalKind.Demand,null,
        DiplomaticProposalStatus.Pending,id,null,$"pending-{id}",null);
    var pairPending=Enumerable.Range(1,DiplomacyState.MaxPendingProposalsPerPair)
        .Select(i=>Pending(i,1,2)).ToArray();
    var proposalArguments=new{Proposer=1,Recipient=2,
        Kind=DiplomaticProposalKind.TradeOffer,
        AgreementType=(DiplomaticAgreementType?)DiplomaticAgreementType.Trade,
        Tick=500L,Summary="new proposal",ExternalTerms=(string?)null};
    void CreateProposal(DiplomacyState state)=>_=Internal(state,"CreateProposal",
        proposalArguments.Proposer,proposalArguments.Recipient,proposalArguments.Kind,
        proposalArguments.AgreementType,proposalArguments.Tick,
        proposalArguments.Summary,proposalArguments.ExternalTerms);
    Primitive("primitive-proposal-pair-pending-cap",
        Empty() with {Proposals=pairPending,NextProposalId=9},"create-proposal",
        proposalArguments,"none",CreateProposal);

    var globalWithResolved=Enumerable.Range(1,DiplomacyState.MaxStoredProposals)
        .Select(i=>i==1
            ? new DiplomaticProposalSnapshot(1,10,11,DiplomaticProposalKind.Demand,
                null,DiplomaticProposalStatus.Rejected,20,1,"old resolved",null)
            : Pending(i,100+i,1000+i)).ToArray();
    Primitive("primitive-proposal-global-evicts-resolved",
        Empty() with {Proposals=globalWithResolved,NextProposalId=129},
        "create-proposal",proposalArguments,"none",CreateProposal);

    var globalPending=Enumerable.Range(1,DiplomacyState.MaxStoredProposals)
        .Select(i=>Pending(i,100+i,1000+i)).ToArray();
    Primitive("primitive-proposal-global-pending-cap",
        Empty() with {Proposals=globalPending,NextProposalId=129},
        "create-proposal",proposalArguments,"none",CreateProposal);

    void Query(string name,DiplomacyStateSnapshot input,string operation)
    {
        var state=DiplomacyState.Restore(input);var before=Fingerprint();object? result=null;Exception? error=null;
        try{result=operation switch{"relationship-self"=>state.GetRelationship(1,1),"missing-granted-transit"=>state.IsTransitAuthorized(4,5),_=>throw new InvalidOperationException("bad fixture operation")};}catch(Exception caught){error=caught;}
        var after=Fingerprint();rows.Add(new{Name=name,Kind="query",Operation=operation,Input=Node(input),BeforeFingerprint=before,AfterFingerprint=after,Error=Error(error),Result=result});
    }
    Query("query-relationship-self",Empty(),"relationship-self");
    Query("query-missing-relationship-granted-transit",Empty() with {AccessPermissions=[new(4,5,AccessPermission.Granted,1)]},"missing-granted-transit");

    var overflowInput=Empty() with {Claims=[new(long.MaxValue,1,7,1,true,[1])]};
    var overflowOwned=JsonSerializer.Serialize(overflowInput,json);
    var overflowBefore=Fingerprint();DiplomacyState? overflowState=null;
    Exception? overflowError=null;
    try{overflowState=DiplomacyState.Restore(overflowInput);}
    catch(Exception caught){overflowError=caught;}
    var overflowAfter=Fingerprint();
    rows.Add(new{Name="restore-max-claim-counter-native-boundary",
        Kind="restore",Input=Node(overflowInput),Compact=false,
        InputBefore=overflowOwned,
        InputAfter=JsonSerializer.Serialize(overflowInput,json),
        BeforeFingerprint=overflowBefore,AfterFingerprint=overflowAfter,
        SourceError=Error(overflowError),
        SourceResult=overflowState is null?null:Node(overflowState.Snapshot()),
        Error=new{Type="OverflowException",
            Message="Diplomacy claim counter overflow."},Result=(object?)null});

    var fixture=new{Source="src/Game/Simulation/Diplomacy/DiplomacySystem.cs:1-386",SourceFingerprint=sourceFingerprint,FinalFingerprint=Fingerprint(),RowCount=rows.Count,Rows=rows};
    Directory.CreateDirectory(Path.GetDirectoryName(output)??throw new InvalidOperationException("Output has no parent."));
    File.WriteAllText(output,JsonSerializer.Serialize(fixture,json));
    Console.WriteLine($"diplomacy state oracle: {rows.Count} rows -> {output}");
}
catch(Exception error)
{
    Console.Error.WriteLine(error.ToString());
    Console.Error.WriteLine($"cwd={Environment.CurrentDirectory}");
    Console.Error.WriteLine($"repoRoot={diagnosticRoot}");
    Console.Error.WriteLine($"fixture={diagnosticFixture}");
    Environment.ExitCode=1;
}
