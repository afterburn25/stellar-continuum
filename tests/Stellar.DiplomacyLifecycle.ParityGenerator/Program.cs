using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using Game.Simulation.Diplomacy;

CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture=CultureInfo.InvariantCulture;
var diagnosticRoot=args.Length>0?args[0]:"<missing>";
var diagnosticFixture=args.Length>1?args[1]:"<missing>";
try
{
    if(args.Length!=2)throw new ArgumentException("Expected repository root and output fixture path.");
    var root=Path.GetFullPath(args[0]);var output=Path.GetFullPath(args[1]);
    var sourceNames=new[]{"DiplomacyCampaignClock.cs","DiplomaticContactAgingService.cs",
        "DiplomaticProposalLifecycleService.cs","DiplomaticCommunicationService.cs",
        "DiplomaticAgreementTerminationService.cs","DiplomacyCampaignMaintenanceScheduler.cs"};
    var sourcePaths=sourceNames.Select(name=>Path.Combine(root,"src","Game","Simulation","Diplomacy",name)).ToArray();
    string Fingerprint()
    {
        using var hash=IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach(var path in sourcePaths)
        {
            var name=Encoding.UTF8.GetBytes(Path.GetFileName(path));hash.AppendData(name);
            hash.AppendData([0]);hash.AppendData(File.ReadAllBytes(path));hash.AppendData([0]);
        }
        return Convert.ToHexString(hash.GetHashAndReset());
    }
    var json=new JsonSerializerOptions{WriteIndented=true,
        NumberHandling=JsonNumberHandling.AllowNamedFloatingPointLiterals};
    JsonNode? Node(object? value)=>JsonSerializer.SerializeToNode(value,json);
    object? Error(Exception? value)=>value is null?null:new{Type=value.GetType().Name,value.Message};
    DiplomacyStateSnapshot Empty()=>new([],[],[],[],[],[],[],[],1,1,1,1);
    DiplomaticContactSnapshot Contact(int observer,int target,long last=10,
        bool communication=false,ContactCondition condition=ContactCondition.Active,
        ContactAwareness awareness=ContactAwareness.Identified,string? id=null)=>new(
            observer,id??$"civilization:{target}",target,1,last,7,awareness,condition,
            communication,.75);
    DiplomacyStateSnapshot Mutual()=>Empty() with
        {Contacts=[Contact(1,2,communication:true,awareness:ContactAwareness.CommunicationAvailable),
            Contact(2,1,communication:true,awareness:ContactAwareness.CommunicationAvailable)]};
    object State(DiplomacyState state)=>new{Snapshot=Node(state.Snapshot()),
        View1=Node(state.BuildViewFor(1)),View2=Node(state.BuildViewFor(2))};
    var rows=new List<object>();
    void Scalar(string name,string operation,object input,Func<object?> call)
    {
        var owned=JsonSerializer.Serialize(input,json);var before=Fingerprint();
        object? result=null;Exception? error=null;
        try{result=call();}catch(Exception caught){error=caught;}
        var after=Fingerprint();rows.Add(new{Name=name,Kind="scalar",Operation=operation,
            Input=Node(input),InputBefore=owned,InputAfter=JsonSerializer.Serialize(input,json),
            BeforeFingerprint=before,AfterFingerprint=after,Error=Error(error),Result=Node(result)});
    }
    Scalar("clock-zero","from-days",new{Days=0d},()=>DiplomacyCampaignClock.FromSimulationDays(0));
    Scalar("clock-floor","from-days",new{Days=1.2349},()=>DiplomacyCampaignClock.FromSimulationDays(1.2349));
    Scalar("clock-negative","from-days",new{Days=-1d},()=>DiplomacyCampaignClock.FromSimulationDays(-1));
    Scalar("clock-nan","from-days",new{Days=double.NaN},()=>DiplomacyCampaignClock.FromSimulationDays(double.NaN));
    Scalar("clock-positive-infinity","from-days",new{Days=double.PositiveInfinity},()=>DiplomacyCampaignClock.FromSimulationDays(double.PositiveInfinity));
    Scalar("clock-saturates","from-days",new{Days=1e30},()=>DiplomacyCampaignClock.FromSimulationDays(1e30));
    Scalar("whole-day-one","whole-days",new{Days=1L},()=>DiplomacyCampaignClock.TicksForWholeDays(1));
    Scalar("whole-day-zero","whole-days",new{Days=0L},()=>DiplomacyCampaignClock.TicksForWholeDays(0));
    Scalar("whole-day-saturates","whole-days",new{Days=long.MaxValue},()=>DiplomacyCampaignClock.TicksForWholeDays(long.MaxValue));

    void Policy(string name,DiplomacyCampaignMaintenancePolicy policy)
    {
        var input=new{Policy=policy};Scalar(name,"policy",input,()=>{policy.Validate();return null;});
    }
    Policy("policy-valid",new(1,2,3));
    Policy("policy-interval-first",new(0,0,0));
    Policy("policy-stale-second",new(1,0,0));
    Policy("policy-lifetime-third",new(1,2,0));
    Scalar("policy-default","policy-default",new{},()=>DiplomacyCampaignMaintenancePolicy.EarlyReleaseDefault);

    void Stateful(string name,string kind,DiplomacyStateSnapshot initial,object arguments,
        Func<DiplomacyState,object?> call)
    {
        var state=DiplomacyState.Restore(initial);var input=new{Snapshot=initial,Arguments=arguments};
        var owned=JsonSerializer.Serialize(input,json);var before=Fingerprint();
        object? result=null;Exception? error=null;
        try{result=call(state);}catch(Exception caught){error=caught;}
        var after=Fingerprint();rows.Add(new{Name=name,Kind=kind,Input=Node(input),
            InputBefore=owned,InputAfter=JsonSerializer.Serialize(input,json),
            BeforeFingerprint=before,AfterFingerprint=after,Error=Error(error),
            Result=Node(result),State=State(state)});
    }
    object ReviewAging(DiplomacyState state,long now,long stale)=>
        new DiplomaticContactAgingService(state).Review(now,stale);
    Stateful("aging-empty","aging",Empty(),new{Now=20L,Stale=10L},s=>ReviewAging(s,20,10));
    Stateful("aging-stales-threshold","aging",Empty() with
        {Contacts=[Contact(1,2,last:10)]},new{Now=20L,Stale=10L},s=>ReviewAging(s,20,10));
    Stateful("aging-communication-stays-current","aging",Empty() with
        {Contacts=[Contact(1,2,last:1,communication:true,awareness:ContactAwareness.CommunicationAvailable)]},
        new{Now=20L,Stale=10L},s=>ReviewAging(s,20,10));
    Stateful("aging-invalid-stale","aging",Empty(),new{Now=20L,Stale=0L},s=>ReviewAging(s,20,0));
    Stateful("aging-partial-before-future","aging",Empty() with {Contacts=[
        Contact(1,2,last:1,id:"a-old"),Contact(1,3,last:30,id:"z-future")]},
        new{Now=20L,Stale=10L},s=>ReviewAging(s,20,10));

    object ReviewProposals(DiplomacyState state,long now,long lifetime,
        (DiplomaticProposalKind Kind,long Lifetime)[] overrides)
    {
        var map=overrides.ToDictionary(pair=>pair.Kind,pair=>pair.Lifetime);
        return new DiplomaticProposalLifecycleService(state).Review(now,lifetime,map);
    }
    DiplomaticProposalSnapshot Proposal(long id,long created,
        DiplomaticProposalKind kind=DiplomaticProposalKind.Demand,
        DiplomaticProposalStatus status=DiplomaticProposalStatus.Pending)=>new(
            id,1,2,kind,null,status,created,null,$"proposal-{id}",null);
    Stateful("proposal-review-expires","proposal-lifecycle",Mutual() with
        {Proposals=[Proposal(1,1),Proposal(2,18),Proposal(3,1,status:DiplomaticProposalStatus.Rejected)],NextProposalId=4},
        new{Now=20L,Lifetime=10L,Overrides=Array.Empty<object>()},
        s=>ReviewProposals(s,20,10,[]));
    Stateful("proposal-review-override","proposal-lifecycle",Mutual() with
        {Proposals=[Proposal(1,10,DiplomaticProposalKind.TradeOffer)],NextProposalId=2},
        new{Now=20L,Lifetime=50L,Overrides=new[]{new{Kind=DiplomaticProposalKind.TradeOffer,Lifetime=5L}}},
        s=>ReviewProposals(s,20,50,[(DiplomaticProposalKind.TradeOffer,5)]));
    Stateful("proposal-invalid-override-before-snapshot","proposal-lifecycle",Mutual(),
        new{Now=20L,Lifetime=10L,Overrides=new[]{new{Kind=DiplomaticProposalKind.Demand,Lifetime=0L}}},
        s=>ReviewProposals(s,20,10,[(DiplomaticProposalKind.Demand,0)]));
    Stateful("proposal-partial-before-future","proposal-lifecycle",Mutual() with
        {Proposals=[Proposal(1,1),Proposal(2,30)],NextProposalId=3},
        new{Now=20L,Lifetime=10L,Overrides=Array.Empty<object>()},
        s=>ReviewProposals(s,20,10,[]));

    object? Communicate(DiplomacyState state,int a,int b,long tick)
    {new DiplomaticCommunicationService(state).EstablishMutualCommunication(a,b,tick);return null;}
    var identifiedBoth=Empty() with {Contacts=[Contact(1,2),Contact(2,1)]};
    Stateful("communication-success","communication",identifiedBoth,new{A=1,B=2,Tick=20L},s=>Communicate(s,1,2,20));
    Stateful("communication-self-first","communication",Empty(),new{A=1,B=1,Tick=-1L},s=>Communicate(s,1,1,-1));
    Stateful("communication-negative-a","communication",Empty(),new{A=-1,B=-2,Tick=-1L},s=>Communicate(s,-1,-2,-1));
    Stateful("communication-missing-second-no-partial","communication",Empty() with
        {Contacts=[Contact(1,2)]},new{A=1,B=2,Tick=20L},s=>Communicate(s,1,2,20));
    Stateful("communication-stale","communication",Empty() with
        {Contacts=[Contact(1,2,condition:ContactCondition.StaleOrLost),Contact(2,1)]},
        new{A=1,B=2,Tick=20L},s=>Communicate(s,1,2,20));
    Stateful("communication-before-observation","communication",Empty() with
        {Contacts=[Contact(1,2,last:30),Contact(2,1,last:10)]},
        new{A=1,B=2,Tick=20L},s=>Communicate(s,1,2,20));
    Stateful("communication-equal-latest-stable-view-order","communication",Empty() with
        {Contacts=[Contact(1,2,last:10,condition:ContactCondition.StaleOrLost,id:"z-stale"),
            Contact(1,2,last:10,id:"a-active"),Contact(2,1,last:10)]},
        new{A=1,B=2,Tick=20L},s=>Communicate(s,1,2,20));

    object Terminate(DiplomacyState state,long id,int requester,long tick,string reason)=>
        new DiplomaticAgreementTerminationService(state).Terminate(id,requester,tick,reason);
    DiplomaticAgreementSnapshot Agreement(long id,DiplomaticAgreementType type,
        DiplomaticAgreementStatus status=DiplomaticAgreementStatus.Active,long started=5)=>
        new(id,1,2,type,status,started,status==DiplomaticAgreementStatus.Terminated?8:null,null);
    Stateful("terminate-access","termination",Mutual() with
        {Agreements=[Agreement(1,DiplomaticAgreementType.Access)],AccessPermissions=[
            new(1,2,AccessPermission.Granted,5),new(2,1,AccessPermission.Granted,5)],NextAgreementId=2},
        new{AgreementId=1L,Requester=2,Tick=10L,Reason="  finished  "},s=>Terminate(s,1,2,10,"  finished  "));
    Stateful("terminate-ceasefire-resumes-hostility","termination",Mutual() with
        {Agreements=[Agreement(1,DiplomaticAgreementType.Ceasefire)],Relationships=[
            new(1,2,DiplomaticPoliticalState.Ceasefire,0,0,0,0,0,[])],NextAgreementId=2},
        new{AgreementId=1L,Requester=1,Tick=10L,Reason="expired"},s=>Terminate(s,1,1,10,"expired"));
    Stateful("terminate-ceasefire-at-war-stays-war","termination",Mutual() with
        {Agreements=[Agreement(1,DiplomaticAgreementType.Ceasefire)],Relationships=[
            new(1,2,DiplomaticPoliticalState.AtWar,0,0,0,0,0,[])],NextAgreementId=2},
        new{AgreementId=1L,Requester=1,Tick=10L,Reason="war resumed"},
        s=>Terminate(s,1,1,10,"war resumed"));
    Stateful("terminate-idempotent","termination",Mutual() with
        {Agreements=[Agreement(1,DiplomaticAgreementType.Trade,DiplomaticAgreementStatus.Terminated)],NextAgreementId=2},
        new{AgreementId=1L,Requester=1,Tick=10L,Reason="again"},s=>Terminate(s,1,1,10,"again"));
    Stateful("terminate-reason-before-lookup","termination",Empty(),
        new{AgreementId=99L,Requester=1,Tick=10L,Reason="\u3000"},s=>Terminate(s,99,1,10,"\u3000"));
    Stateful("terminate-nonparticipant","termination",Mutual() with
        {Agreements=[Agreement(1,DiplomaticAgreementType.Trade)],NextAgreementId=2},
        new{AgreementId=1L,Requester=3,Tick=10L,Reason="stop"},s=>Terminate(s,1,3,10,"stop"));
    Stateful("terminate-before-activation","termination",Mutual() with
        {Agreements=[Agreement(1,DiplomaticAgreementType.Trade,started:20)],NextAgreementId=2},
        new{AgreementId=1L,Requester=1,Tick=10L,Reason="stop"},s=>Terminate(s,1,1,10,"stop"));

    void Scheduler(string name,DiplomacyStateSnapshot initial,
        DiplomacyCampaignMaintenancePolicy policy,object[] commands)
    {
        var state=DiplomacyState.Restore(initial);
        var scheduler=new DiplomacyCampaignMaintenanceScheduler(state,policy);
        var input=new{Snapshot=initial,Policy=policy,Commands=commands};
        var owned=JsonSerializer.Serialize(input,json);var before=Fingerprint();
        var steps=new List<object>();
        foreach(var command in commands)
        {
            var node=JsonSerializer.SerializeToElement(command,json);
            var op=node.GetProperty("Op").GetString()!;object? result=null;Exception? error=null;
            var stepBefore=Fingerprint();
            try
            {
                if(op=="Reset")scheduler.Reset(node.GetProperty("Now").GetInt64(),node.GetProperty("Immediate").GetBoolean());
                else result=scheduler.ReviewIfDue(node.GetProperty("Now").GetInt64());
            }
            catch(Exception caught){error=caught;}
            var stepAfter=Fingerprint();
            steps.Add(new{Op=op,Error=Error(error),Result=Node(result),scheduler.NextReviewTick,
                scheduler.LastReviewTick,BeforeFingerprint=stepBefore,
                AfterFingerprint=stepAfter,State=State(state)});
        }
        var after=Fingerprint();rows.Add(new{Name=name,Kind="scheduler",Input=Node(input),
            InputBefore=owned,InputAfter=JsonSerializer.Serialize(input,json),
            BeforeFingerprint=before,AfterFingerprint=after,Steps=steps});
    }
    Scheduler("scheduler-lazy-idempotent",Empty(),new(10,20,30),[
        new{Op="Review",Now=5L,Immediate=false},new{Op="Review",Now=5L,Immediate=false},
        new{Op="Review",Now=14L,Immediate=false},new{Op="Review",Now=15L,Immediate=false}]);
    Scheduler("scheduler-delayed-reset-saturates",Empty(),new(10,20,30),[
        new{Op="Reset",Now=long.MaxValue-5,Immediate=false},
        new{Op="Review",Now=long.MaxValue,Immediate=false}]);
    Scheduler("scheduler-partial-failure-keeps-cadence",Empty() with
        {Contacts=[Contact(1,2,last:1,id:"a-old")],Proposals=[Proposal(1,30)],NextProposalId=2},
        new(10,10,10),[new{Op="Reset",Now=20L,Immediate=true},
            new{Op="Review",Now=20L,Immediate=false}]);

    var fixture=new{Sources=sourceNames,SourceFingerprint=Fingerprint(),FinalFingerprint=Fingerprint(),
        RowCount=rows.Count,Rows=rows};
    Directory.CreateDirectory(Path.GetDirectoryName(output)??throw new InvalidOperationException("Output has no parent."));
    File.WriteAllText(output,JsonSerializer.Serialize(fixture,json));
    Console.WriteLine($"diplomacy lifecycle oracle: {rows.Count} rows -> {output}");
}
catch(Exception error)
{
    Console.Error.WriteLine(error.ToString());Console.Error.WriteLine($"cwd={Environment.CurrentDirectory}");
    Console.Error.WriteLine($"repoRoot={diagnosticRoot}");Console.Error.WriteLine($"fixture={diagnosticFixture}");
    Environment.ExitCode=1;
}
