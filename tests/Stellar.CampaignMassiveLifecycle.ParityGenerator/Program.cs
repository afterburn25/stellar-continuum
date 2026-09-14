using System.Security.Cryptography;
using System.Numerics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Construction;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;

internal static class Program
{
    static readonly JsonSerializerOptions Json = new() { NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
    sealed record Row(string Name, string Operation, JsonNode Input, JsonNode Before,
        JsonNode After, JsonNode? Result, string? ErrorType, string? ErrorMessage);
    sealed class Hostility : ICombatHostilityView { public bool AreHostile(int a, int b) => a != b; }
    sealed class Sensors(float confidence, bool identify, bool power) : IMassiveCombatSensorView {
        public float Confidence(int observer, long formation) => formation == 1 ? 1 : confidence;
        public bool IdentifiesCohorts(int observer, long formation) => formation == 1 || identify;
        public bool IdentifiesImportantVessels(int observer, long formation) => formation == 1 || identify;
        public bool CanEstimateCombatPower(int observer, long formation) => formation == 1 || power;
    }

    static int Main(string[] args)
    {
        if (args.Length != 2) { Console.Error.WriteLine("Usage: CampaignMassiveCombatOracle <fixture> <source-root>"); return 1; }
        var output = Path.GetFullPath(args[0]); var root = Path.GetFullPath(args[1]);
        try {
            var rows = Build();
            var sources = new[] { "Simulation/Combat/CampaignMassiveCombat.cs", "Simulation/Combat/Massive/MassiveCombatObserver.cs", "Simulation/Combat/Massive/MassiveCombatDoctrine.cs", "Simulation/Combat/Massive/MassiveCombatContracts.cs", "Simulation/Combat/Massive/MassiveCombatEngine.cs", "Simulation/Combat/Massive/MassiveCombatState.cs", "Simulation/Combat/Massive/MassiveCombatEquipment.cs", "Simulation/Combat/Massive/MassiveCombatOutcome.cs", "Simulation/Combat/FleetCombatPower.cs", "Simulation/Combat/CombatContracts.cs", "Simulation/Models/FleetState.cs" };
            var document = new { SchemaVersion = 1, RowCount = rows.Count, SourceFiles = sources.Select(p => new { Path=p, Sha256=Hash(File.ReadAllBytes(Path.Combine(root,p))) }), Rows=rows };
            File.WriteAllText(output, JsonSerializer.Serialize(document, new JsonSerializerOptions(Json){WriteIndented=true})+Environment.NewLine,new UTF8Encoding(false));
            Console.WriteLine($"Campaign massive combat oracle: {rows.Count}/{rows.Count} rows written"); return 0;
        } catch(Exception e) { Console.Error.WriteLine(e); Console.Error.WriteLine($"Working directory: {Environment.CurrentDirectory}\nSource root: {root}\nFixture path: {output}"); return 1; }
    }

    static List<Row> Build()
    {
        var rows=new List<Row>();
        Call(rows,"constructor-null-hostility","Constructor",Galaxy(),g=>new CampaignMassiveCombat(null!));
        Call(rows,"begin-invalid-day","Begin",Galaxy(), g => new CampaignMassiveCombat(new Hostility()).Begin(g,1,1,double.NaN), day: double.NaN);
        Call(rows,"begin-missing-actor","Begin",Galaxy(),g=>new CampaignMassiveCombat(new Hostility()).Begin(g,1,999,4), actor: 999, day: 4);
        var noHostile=Galaxy(); noHostile.Fleets.RemoveAt(1);
        Call(rows,"begin-no-hostile","Begin",noHostile,g=>new CampaignMassiveCombat(new Hostility()).Begin(g,1,1,4),day:4);
        var begin=Galaxy(); Call(rows,"begin-two-vessels","Begin",begin,g=>new CampaignMassiveCombat(new Hostility()).Begin(g,1,1,4.25));
        Call(rows,"begin-separate-equal-loadouts-group","Begin",GroupingGalaxy(0,0),g=>new CampaignMassiveCombat(new Hostility()).Begin(g,1,1,4.25));
        Call(rows,"begin-weapon-scalar-difference-separates","Begin",GroupingGalaxy(0,1),g=>new CampaignMassiveCombat(new Hostility()).Begin(g,1,1,4.25));
        Call(rows,"begin-signed-zero-loadout-identity","Begin",GroupingGalaxy(0,-0.0f),g=>new CampaignMassiveCombat(new Hostility()).Begin(g,1,1,4.25), secondLoadoutNegativeZero:true);
        var nonfiniteBoth=Galaxy(); nonfiniteBoth.Fleets[1].TacticalLoadout=Loadout(float.NaN); nonfiniteBoth.Fleets[1].TacticalVessel!.HullFraction=float.NaN;
        Call(rows,"begin-nonfinite-loadout-precedes-vessel-clone","Begin",nonfiniteBoth,g=>new CampaignMassiveCombat(new Hostility()).Begin(g,1,1,4.25));
        var nonfiniteVessel=Galaxy(); nonfiniteVessel.Fleets[1].TacticalLoadout=Loadout(0); nonfiniteVessel.Fleets[1].TacticalVessel!.HullFraction=float.NaN;
        Call(rows,"begin-nonfinite-vessel-clone","Begin",nonfiniteVessel,g=>new CampaignMassiveCombat(new Hostility()).Begin(g,1,1,4.25));
        var existing=Galaxy(); new CampaignMassiveCombat(new Hostility()).Begin(existing,1,1,4.25);
        Call(rows,"begin-existing-encounter","Begin",existing,g=>new CampaignMassiveCombat(new Hostility()).Begin(g,1,1,5),day:5);
        var zeroHull=Galaxy(); zeroHull.Fleets[1].Combat!.Hull=0;
        Call(rows,"begin-zero-hull-after-prepare","Begin",zeroHull,g=>new CampaignMassiveCombat(new Hostility()).Begin(g,1,1,4),day:4);
        Call(rows,"reconcile-no-encounter","Reconcile",Galaxy(),g=>new CampaignMassiveCombat(new Hostility()).Reconcile(g));
        var active=Galaxy(); var activeRuntime=new CampaignMassiveCombat(new Hostility()); activeRuntime.Begin(active,1,1,4.25);
        Call(rows,"reconcile-active-hostilities","Reconcile",active,g=>new CampaignMassiveCombat(new Hostility()).Reconcile(g));
        var complete=Galaxy(); var runtime=new CampaignMassiveCombat(new Hostility()); runtime.Begin(complete,1,1,4.25);
        var enemy=complete.ActiveCombatEncounter!.Battle.Formations.Single(x=>x.CivilizationId==2); enemy.Cohorts[0].ActiveCount=0; enemy.HullPool=0;
        complete.Fleets[1].EmbarkedPopulationMillions=3.5; complete.Fleets[1].EmbarkedPopulationSpeciesId="terran_baseline"; complete.Fleets[1].DestinationSystemId=9; complete.Fleets[1].PlannedRouteSystemIds.AddRange([7,9]); complete.Fleets[1].DestinationPlanetaryBodyId=77;
        Call(rows,"reconcile-destroyed-population-and-travel","Reconcile",complete,g=>new CampaignMassiveCombat(new Hostility()).Reconcile(g));
        var damaged=Galaxy(); var damagedRuntime=new CampaignMassiveCombat(new Hostility()); damagedRuntime.Begin(damaged,1,1,4.25); var damagedBattle=damaged.ActiveCombatEncounter!.Battle; var friendly=damagedBattle.Formations.Single(x=>x.CivilizationId==1); friendly.ShieldPool=4.5f; friendly.ArmorPool=8.25f; friendly.HullPool=31.75f; friendly.ImportantVessels[0].EngineFraction=.35f; damagedBattle.Formations.Single(x=>x.CivilizationId==2).Surrendered=true;
        Call(rows,"reconcile-damaged-important-vessel","Reconcile",damaged,g=>new CampaignMassiveCombat(new Hostility()).Reconcile(g));
        var nanPool=Galaxy(); var nanPoolRuntime=new CampaignMassiveCombat(new Hostility()); nanPoolRuntime.Begin(nanPool,1,1,4.25); var nanBattle=nanPool.ActiveCombatEncounter!.Battle; nanBattle.Formations.Single(x=>x.CivilizationId==1).ShieldPool=float.NaN; nanBattle.Formations.Single(x=>x.CivilizationId==2).Surrendered=true;
        Call(rows,"reconcile-nan-pool-propagates","Reconcile",nanPool,g=>nanPoolRuntime.Reconcile(g));
        var reconcileClone=Galaxy(); var reconcileCloneRuntime=new CampaignMassiveCombat(new Hostility()); reconcileCloneRuntime.Begin(reconcileClone,1,1,4.25); var cloneBattle=reconcileClone.ActiveCombatEncounter!.Battle; var cloneFriendly=cloneBattle.Formations.Single(x=>x.CivilizationId==1); cloneFriendly.ImportantVessels[0].HullFraction=float.NaN; cloneFriendly.Loadout.Modules[0].Condition=float.NaN; cloneBattle.Formations.Single(x=>x.CivilizationId==2).Surrendered=true;
        Call(rows,"reconcile-vessel-clone-precedes-loadout-clone","Reconcile",reconcileClone,g=>reconcileCloneRuntime.Reconcile(g));
        var reconcileLoadout=Galaxy(); var reconcileLoadoutRuntime=new CampaignMassiveCombat(new Hostility()); reconcileLoadoutRuntime.Begin(reconcileLoadout,1,1,4.25); var loadoutBattle=reconcileLoadout.ActiveCombatEncounter!.Battle; loadoutBattle.Formations.Single(x=>x.CivilizationId==1).Loadout.Modules[0].Condition=float.NaN; loadoutBattle.Formations.Single(x=>x.CivilizationId==2).Surrendered=true;
        Call(rows,"reconcile-nonfinite-loadout-clone","Reconcile",reconcileLoadout,g=>reconcileLoadoutRuntime.Reconcile(g));
        var overflow=Galaxy(); var overflowRuntime=new CampaignMassiveCombat(new Hostility()); overflowRuntime.Begin(overflow,1,1,4.25); var overflowBattle=overflow.ActiveCombatEncounter!.Battle; overflowBattle.Formations.Single(x=>x.CivilizationId==1).ImportantVessels[0].Id=long.MaxValue; overflowBattle.Formations.Single(x=>x.CivilizationId==2).Surrendered=true;
        Call(rows,"reconcile-important-id-overflow","Reconcile",overflow,g=>overflowRuntime.Reconcile(g));
        var chain=Galaxy(); var chainRuntime=new CampaignMassiveCombat(new Hostility()); chainRuntime.Begin(chain,1,1,8.5); chain.ActiveCombatEncounter=CampaignMassiveCombat.Clone(chain.ActiveCombatEncounter!); chainRuntime.Engine.Advance(chain.ActiveCombatEncounter.Battle,.1); chain.ActiveCombatEncounter.Battle.Formations.Single(x=>x.CivilizationId==2).Surrendered=true;
        Call(rows,"begin-clone-step-reconcile","Reconcile",chain,g=>chainRuntime.Reconcile(g));
        var repeated=Galaxy(); var repeatedRuntime=new CampaignMassiveCombat(new Hostility()); repeatedRuntime.Begin(repeated,1,1,3); repeated.ActiveCombatEncounter!.Battle.Formations.Single(x=>x.CivilizationId==2).Surrendered=true;
        Call(rows,"reconcile-repeat-idempotent","ReconcileTwice",repeated,g=>new[]{repeatedRuntime.Reconcile(g),repeatedRuntime.Reconcile(g)});
        var capped=Galaxy(); for(var id=3;id<=131;id++) capped.Fleets.Add(Fleet(id,2,$"Enemy {id}")); var capRuntime=new CampaignMassiveCombat(new Hostility()); capRuntime.Begin(capped,1,1,6); var capEnemy=capped.ActiveCombatEncounter!.Battle.Formations.Single(x=>x.CivilizationId==2); capEnemy.Cohorts[0].ActiveCount=0; capEnemy.HullPool=0;
        Call(rows,"reconcile-destruction-event-cap-128-plus-ended","Reconcile",capped,g=>capRuntime.Reconcile(g));
        var observed=Galaxy(); var observedRuntime=new CampaignMassiveCombat(new Hostility()); observedRuntime.Begin(observed,1,1,4.25);
        Call(rows,"observe-unscanned-contact","Observe",observed,g=>new CampaignMassiveCombat(new Hostility()).Observe(g,1,false), resultIncludeFields:true);
        Call(rows,"observe-scanned-contact","Observe",observed,g=>new CampaignMassiveCombat(new Hostility()).Observe(g,1,true), scanningCapability:true, resultIncludeFields:true);
        var hidden=Galaxy(); var hiddenRuntime=new CampaignMassiveCombat(new Hostility()); hiddenRuntime.Begin(hidden,1,1,4.25); var hiddenBattle=hidden.ActiveCombatEncounter!.Battle; var friend=hiddenBattle.Formations.Single(x=>x.CivilizationId==1); var foe=hiddenBattle.Formations.Single(x=>x.CivilizationId==2); friend.Velocity=new(1.25f,-2.5f); foe.Velocity=new(-3.75f,.5f); hiddenBattle.Events.Add(new(1,1,MassiveCombatEventType.Damage,2,foe.Id,1,friend.Id,71,new(900,901),"secret damage")); hiddenBattle.NextEventSequence=2; hiddenBattle.ActiveSalvos.Add(new(){Id=1,SourceFormationId=foe.Id,TargetFormationId=friend.Id,MissileCount=9,Damage=4,RemainingSeconds=2,InitialFlightSeconds=4,LaunchPosition=new(800,801)}); hiddenBattle.NextSalvoId=2;
        Call(rows,"observer-hidden-attacker-and-incoming-salvo","Observer",hidden,g=>MassiveCombatObserver.BuildSnapshot(g.ActiveCombatEncounter!.Battle,1,new Sensors(.2f,false,false)), enemyConfidence:.2f, identify:false, power:false, resultIncludeFields:true);
        var filtered=Galaxy(); var filteredRuntime=new CampaignMassiveCombat(new Hostility()); filteredRuntime.Begin(filtered,1,1,4.25); var filteredBattle=filtered.ActiveCombatEncounter!.Battle; var filteredFriend=filteredBattle.Formations.Single(x=>x.CivilizationId==1); var filteredFoe=filteredBattle.Formations.Single(x=>x.CivilizationId==2); filteredBattle.Events.Clear(); filteredBattle.Events.Add(new(1,1,MassiveCombatEventType.Damage,2,filteredFoe.Id,1,filteredFriend.Id,1,new(3,4),"visible first")); for(var sequence=2;sequence<=130;sequence++) filteredBattle.Events.Add(new(sequence,1,MassiveCombatEventType.Damage,2,filteredFoe.Id,2,filteredFoe.Id,1,new(5,6),"invisible")); filteredBattle.NextEventSequence=131;
        Call(rows,"observer-filters-before-last-128","Observer",filtered,g=>MassiveCombatObserver.BuildSnapshot(g.ActiveCombatEncounter!.Battle,1,new Sensors(.2f,false,false)), enemyConfidence:.2f, identify:false, power:false, resultIncludeFields:true);
        var scanned=CampaignMassiveCombat.Clone(hidden.ActiveCombatEncounter!); hidden.ActiveCombatEncounter=scanned;
        Call(rows,"observe-identified-scanned-event-and-salvo","Observe",hidden,g=>new CampaignMassiveCombat(new Hostility()).Observe(g,1,true), scanningCapability:true, resultIncludeFields:true);
        var inactive=Galaxy(); var inactiveRuntime=new CampaignMassiveCombat(new Hostility()); inactiveRuntime.Begin(inactive,1,1,4.25); inactive.ActiveCombatEncounter!.Battle.Formations.Single(x=>x.CivilizationId==1).Surrendered=true;
        Call(rows,"observe-inactive-owned-formation-omitted","Observe",inactive,g=>new CampaignMassiveCombat(new Hostility()).Observe(g,1,false), resultIncludeFields:true);
        var tick=Galaxy(); var tickRuntime=new CampaignMassiveCombat(new Hostility()); tickRuntime.Begin(tick,1,1,4.25); tick.ActiveCombatEncounter!.Battle.Tick=9;
        Call(rows,"advance-tick-ten-doctrine-and-scanner","AdvanceWithScannerReceipts",tick,g=>{var receipts=new List<int>(); var events=tickRuntime.Advance(g,.1,id=>{receipts.Add(id);return id==2;}); return new { Events=events, ScannerCalls=receipts };}, elapsedSeconds:.1, scannerCivilizationIds:[2]);
        var evidence=Galaxy(); var evidenceRuntime=new CampaignMassiveCombat(new Hostility()); evidenceRuntime.Begin(evidence,1,1,4.25); var evidenceBattle=evidence.ActiveCombatEncounter!.Battle; var evidenceFriend=evidenceBattle.Formations.Single(x=>x.CivilizationId==1); var evidenceFoe=evidenceBattle.Formations.Single(x=>x.CivilizationId==2); evidenceBattle.Events.Add(new(1,1,MassiveCombatEventType.Damage,1,evidenceFriend.Id,2,evidenceFoe.Id,1,new(),"engagement")); evidenceBattle.NextEventSequence=2;
        Call(rows,"advance-engagement-evidence","Advance",evidence,g=>evidenceRuntime.Advance(g,0)); evidence.ActiveCombatEncounter=CampaignMassiveCombat.Clone(evidence.ActiveCombatEncounter!);
        Call(rows,"advance-restored-new-runtime-no-duplicate-intelligence","Advance",evidence,g=>new CampaignMassiveCombat(new Hostility()).Advance(g,0));
        var invalidAdvance=Galaxy(); var invalidRuntime=new CampaignMassiveCombat(new Hostility()); invalidRuntime.Begin(invalidAdvance,1,1,4.25);
        Call(rows,"advance-invalid-elapsed","Advance",invalidAdvance,g=>invalidRuntime.Advance(g,-1), elapsedSeconds:-1);
        var ended=Galaxy(); var endedRuntime=new CampaignMassiveCombat(new Hostility()); endedRuntime.Begin(ended,1,1,4.25); ended.ActiveCombatEncounter!.Battle.Formations.Single(x=>x.CivilizationId==2).Surrendered=true;
        Call(rows,"advance-completed-reconciled","Advance",ended,g=>endedRuntime.Advance(g,0));
        var emergency=Snapshot(Formation(10,1,new(.05f,.05f),exact:true,warpBlocked:true),Formation(20,2,new(50,0)));
        Call(rows,"doctrine-emergency-near-origin","Doctrine",Galaxy(),g=>MassiveCombatDoctrine.Decide(emergency,1), doctrineSnapshot:emergency, resultIncludeFields:true);
        var escort=Snapshot(Formation(10,1,new(0,0),exact:true,interdicting:true),Formation(11,1,new(1,0),exact:true,shape:MassiveFormationShape.Screen),Formation(20,2,new(20,0)));
        Call(rows,"doctrine-escort-protects-first-interdictor","Doctrine",Galaxy(),g=>MassiveCombatDoctrine.Decide(escort,1), doctrineSnapshot:escort, resultIncludeFields:true);
        var tie=Snapshot(Formation(9,1,new(0,0),exact:false),Formation(10,1,new(0,0),exact:true),Formation(30,2,new(-10,0)),Formation(20,2,new(10,0)));
        Call(rows,"doctrine-distance-tie-and-inexact-own","Doctrine",Galaxy(),g=>MassiveCombatDoctrine.Decide(tie,1), doctrineSnapshot:tie, resultIncludeFields:true);
        return rows;
    }

    static void Call<T>(List<Row> rows,string name,string operation,GalaxyState galaxy,Func<GalaxyState,T> call,
        int civilization=1,int actor=1,double day=4.25,bool secondLoadoutNegativeZero=false,
        bool scanningCapability=false,float enemyConfidence=.65f,bool identify=true,bool power=true,
        double elapsedSeconds=0,int[]? scannerCivilizationIds=null,
        MassiveCombatSnapshot? doctrineSnapshot=null,bool resultIncludeFields=false)
    {
        var before=World(galaxy); JsonNode? result=null; string? type=null,message=null;
        try { result=JsonSerializer.SerializeToNode(call(galaxy),new JsonSerializerOptions(Json){IncludeFields=resultIncludeFields}); } catch(Exception e){type=e.GetType().Name;message=e.Message;}
        var input = operation == "Begin"
            ? JsonSerializer.SerializeToNode(new { Hostility="distinct", CivilizationId=civilization, ActorFleetId=actor, Day=day,
                SecondLoadoutNegativeZero=secondLoadoutNegativeZero },Json)!
            : operation is "Observe" or "Observer" ? JsonSerializer.SerializeToNode(new { Hostility="distinct", ObserverId=1, ScanningCapability=scanningCapability, EnemyConfidence=enemyConfidence, Identify=identify, Power=power },Json)!
            : operation is "Advance" or "AdvanceWithScannerReceipts" ? JsonSerializer.SerializeToNode(new { Hostility="distinct", ElapsedSeconds=elapsedSeconds, ScannerCivilizationIds=scannerCivilizationIds??Array.Empty<int>() },Json)!
            : operation == "Doctrine" ? JsonSerializer.SerializeToNode(new { CivilizationId=civilization, Snapshot=doctrineSnapshot },new JsonSerializerOptions(Json){IncludeFields=true})!
            : JsonSerializer.SerializeToNode(new { Hostility="distinct" },Json)!;
        rows.Add(new(name,operation,input,before,World(galaxy),result,type,message));
    }
    static MassiveObservedFormation Formation(long id,int civilization,Vector2 position,bool exact=false,bool interdicting=false,bool warpBlocked=false,MassiveFormationShape shape=MassiveFormationShape.Line)=>new(id,civilization,$"Formation {id}",position,Vector2.Zero,shape,1,1,null,null,exact,interdicting,warpBlocked,0,null,Array.Empty<MassiveObservedCohort>(),Array.Empty<MassiveObservedVessel>(),0);
    static MassiveCombatSnapshot Snapshot(params MassiveObservedFormation[] formations)=>new(Guid.Empty,0,0,0,formations,Array.Empty<MassiveObservedCombatEvent>(),Array.Empty<MassiveObservedMissileSalvo>());
    static JsonNode World(GalaxyState g)=>JsonSerializer.SerializeToNode(new { g.Seed,g.PlayerCivilizationId,g.Systems,g.Fleets,g.CombatIntelligence,g.ActiveCombatEncounter },new JsonSerializerOptions(Json){IncludeFields=true})!;
    static GalaxyState Galaxy()=>new() { Seed=94, Systems=[new(7,"Anchor",new(0,0),StarArchetype.Standard,true,false,false,false)], PlanetaryBodies=[], Civilizations=[], Fleets=[Fleet(1,1,"Alpha"),Fleet(2,2,"Beta")], Colonies=[], Economies=[], Technologies=[], ConstructionStates=[], ShipyardStates=[], PlayerCivilizationId=1, Knowledge=new CivilizationKnowledgeState() };
    static GalaxyState GroupingGalaxy(float firstDamage,float secondDamage) { var g=Galaxy(); g.Fleets[1].TacticalVessel=null; g.Fleets[1].TacticalLoadout=Loadout(firstDamage); var third=Fleet(3,2,"Beta Two"); third.TacticalVessel=null; third.TacticalLoadout=Loadout(secondDamage); g.Fleets.Add(third); return g; }
    static MassiveCombatLoadout Loadout(float extraDamage) { var p=CombatProfileRegistry.Get(CombatProfileIds.PatrolCorvetteMk1); var x=MassiveCombatLoadouts.FromLegacy(p); x.Weapons[0].DamagePerShot += extraDamage; x.Modules[0].EffectiveRange=extraDamage; return x; }
    static FleetState Fleet(int id,int civ,string name)=>new() { Id=id,CivilizationId=civ,Name=name,Role=FleetRole.Military,Position=new(0,0),CurrentSystemId=7,Combat=CombatProfileRegistry.CreateInitialState(CombatProfileIds.PatrolCorvetteMk1,FleetRole.Military),TacticalVessel=new(){Id=id,Name=name,DesignId=CombatProfileIds.PatrolCorvetteMk1,IsFlagship=id==1},EmbarkedPopulationMillions=0 };
    static string Hash(byte[] bytes)=>Convert.ToHexString(SHA256.HashData(bytes));
}
