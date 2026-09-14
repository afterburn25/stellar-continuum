using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Campaign;
using Game.Simulation;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Construction;
using Game.Simulation.Diplomacy;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Research.Adaptive;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;

record Row(string Name, string Policy, string Route, object Input, object Before,
    object After, object Result, string? IntegratedRow = null);

static class Program
{
    static readonly JsonSerializerOptions Json = new() { WriteIndented = true };
    static readonly JsonSerializerOptions SnapshotJson = new() { PropertyNamingPolicy=JsonNamingPolicy.CamelCase,Converters={new JsonStringEnumConverter(JsonNamingPolicy.CamelCase)} };
    static AdaptiveResearchCampaignFactory Factory = null!;
    static AdaptiveResearchCampaignSnapshotCodec Codec = null!;
    sealed class Hostility : ICombatHostilityView { public bool AreHostile(int a, int b) => a != b; }

    sealed class HeadlessSourceHost
    {
        public readonly SimulationClock Strategic = new();
        public readonly MassiveCombatClock Tactical = new();
        public readonly GalaxyState World;
        readonly AdaptiveResearchCampaignState research;
        readonly AdaptiveResearchCampaignSimulation researchSimulation = new();
        readonly GalaxySimulationStepCoordinator core;
        readonly DiplomacyState diplomacy = new();
        readonly DiplomacyCampaignRuntimeCoordinator diplomacyRuntime;
        readonly CampaignMassiveCombat combat = new(new Hostility());
        SimulationClock.SpeedLevel preCombat = SimulationClock.SpeedLevel.Normal;
        double tacticalSpeedBeforeMenu = 1;
        bool tacticalOwnsPause;
        bool tacticalMenuPauseOwned;
        public double TacticalResumeSpeed { get; private set; } = 1;
        public bool MenuOpen { get; set; }

        public HeadlessSourceHost(GalaxyState world, bool restored = false)
        {
            World = world;
            research = Factory.Create(world);
            var diplomacyCommands = new DiplomacySimulation(diplomacy);
            diplomacyCommands.ProcessContactOpportunity(new FirstContactOpportunity(1,"gate097-contact",2,0,7,ContactAwareness.ContactEstablished,ContactCondition.Active,true,.9));
            diplomacyCommands.DeclareWar(1,2,0);
            var constructionCapabilities = new AdaptiveResearchConstructionCapabilityView(research);
            var shipbuildingCapabilities = new AdaptiveResearchShipbuildingCapabilityView(research);
            diplomacyRuntime = new DiplomacyCampaignRuntimeCoordinator(diplomacy);
            diplomacyRuntime.Reset(0, reviewImmediately: true);
            core = new GalaxySimulationStepCoordinator(
                construction: new ConstructionSimulation(constructionCapabilities),
                shipbuilding: new ShipbuildingSimulation(shipbuildingCapabilities),
                strategicAi: new CivilizationStrategicRuntimeCoordinator(
                    director: new CivilizationStrategicDirector(new CivilizationStrategicInputBuilder(shipbuildingCapabilities: shipbuildingCapabilities)),
                    knowledgeProvider: new DiplomacyStrategicKnowledgeProvider(diplomacy)),
                combatRuntime: diplomacyRuntime.CreateCombatCommandRuntime(), advanceLegacyResearch: false);
            if (restored && world.ActiveCombatEncounter is { Reconciled: false }) Tactical.SetSpeed(0);
        }
        public CombatOrderResult Begin(int fleet)
        {
            var result = combat.Begin(World, World.PlayerCivilizationId, fleet, Strategic.SimulationDays);
            if (!result.Accepted) return result;
            preCombat = Strategic.Speed; Strategic.SetSpeed(SimulationClock.SpeedLevel.Paused);
            tacticalOwnsPause = true; Tactical.SetSpeed(1); return result;
        }
        public void SetTacticalSpeed(double speed)
        {
            if (World.ActiveCombatEncounter is not { Reconciled: false } || !MassiveCombatClock.AllowedSpeeds.Contains(speed)) return;
            Tactical.SetSpeed(speed); if (speed > 0) TacticalResumeSpeed = speed;
        }
        public void PauseForMenu()
        {
            if (World.ActiveCombatEncounter is not { Reconciled: false } || tacticalMenuPauseOwned) return;
            tacticalSpeedBeforeMenu = Tactical.SpeedMultiplier; tacticalMenuPauseOwned = true; Tactical.SetSpeed(0);
        }
        public void ResumeAfterMenu()
        {
            if (World.ActiveCombatEncounter is not { Reconciled: false } || !tacticalMenuPauseOwned) return;
            tacticalMenuPauseOwned = false; Tactical.SetSpeed(tacticalSpeedBeforeMenu);
        }
        public object Frame(double delta, bool developer)
        {
            if (World.ActiveCombatEncounter is { Reconciled: false })
            {
                if (!tacticalOwnsPause || Strategic.Speed != SimulationClock.SpeedLevel.Paused)
                {
                    preCombat = Strategic.Speed == SimulationClock.SpeedLevel.Paused ? Strategic.ResumeSpeed : Strategic.Speed;
                    Strategic.SetSpeed(SimulationClock.SpeedLevel.Paused); tacticalOwnsPause = true;
                }
                var real = Math.Max(0, double.IsFinite(delta) ? delta : 0);
                var accepted = MenuOpen ? 0 : Tactical.AcceptFrame(real);
                var events = accepted > 0 ? combat.Advance(World, accepted, _ => false) : combat.Reconcile(World);
                var completed = World.ActiveCombatEncounter is { Reconciled: true };
                if (completed) { Strategic.SetSpeed(preCombat); tacticalOwnsPause = false; }
                return new { Route="Tactical", Steps=Array.Empty<double>(), EndDays=Array.Empty<double>(), TacticalAccepted=accepted,
                    TacticalEvents=events.Count, TacticalCompleted=completed, ReadyForSaveCapture=false };
            }
            var start = Strategic.SimulationDays;
            var steps = developer ? PlayableDemoScenario.AdvanceFrame(Strategic, delta) : [Strategic.Advance(delta)];
            var end = start;
            foreach (var days in steps)
            {
                end += days;
                var step = core.Advance(World, days);
                if (days > 0) foreach (var civilization in World.Civilizations.OrderBy(x => x.Id))
                    FleetCombatPower.RecordSensorContacts(World, civilization.Id, end, HasScanner(civilization.Id));
                researchSimulation.Advance(World, research, days, end);
                diplomacyRuntime.Process(step.ExplorationEvents, step.CombatEvents, end);
            }
            return new { Route="Strategic", Steps=steps, EndDays=steps.Select((_,i)=>start+steps.Take(i+1).Sum()).ToArray(),
                TacticalAccepted=0d, TacticalEvents=0, TacticalCompleted=false, ReadyForSaveCapture=true };
        }
        bool HasScanner(int id)=>research.Civilizations.TryGetValue(id,out var state)&&(state.HasCapability("tech:quantum_sensors")||state.HasCapability("tech:distributed_sensor_network"));
        public object State() => new { Clock=Clock(Strategic), TacticalSpeed=Tactical.SpeedMultiplier,
            TacticalResumeSpeed, Active=World.ActiveCombatEncounter is { Reconciled:false },
            Encounter=EncounterProjection(World.ActiveCombatEncounter),
            Campaign=new { World=WorldProjection(World), Research=JsonSerializer.SerializeToElement(Codec.Capture(research),SnapshotJson), Diplomacy=diplomacy.Snapshot() } };
    }

    static int Main(string[] args)
    {
        if (args.Length != 2) { Console.Error.WriteLine("Usage: CampaignFrameOracle <fixture> <source-root>"); return 1; }
        try
        {
            var researchRuntime=AdaptiveResearchStrategicRuntime.LoadFromDirectory(Path.GetFullPath(Path.Combine(args[1],"../../data/research/v1")));
            Factory=new AdaptiveResearchCampaignFactory(researchRuntime); Codec=new AdaptiveResearchCampaignSnapshotCodec(researchRuntime);
            var rows = Build();
            var files = new[]{"Presentation/IntegratedMain.cs","Presentation/Main.CoreIntegration.cs","Presentation/Main.MassiveCombat.cs","Simulation/SimulationClock.cs","Campaign/PlayableDemoScenario.cs","Simulation/Combat/CampaignMassiveCombat.cs","Simulation/Combat/Massive/MassiveCombatClock.cs"};
            var doc = new { SchemaVersion=1, RowCount=rows.Count,
                Boundary=new { Adapter="reconstructed-headless-source-host", GodotMainInvoked=false,
                    StrategicAdvanceOrder="clock,each-cumulative-integrated-step,save-readiness",
                    TacticalAdvanceOrder="pause,clock-admission,advance-or-reconcile,restore", StrategicMenuPause="upstream" },
                SourceFiles=files.Select(p=>new { Path=p,Sha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(args[1],p)))) }), Rows=rows };
            File.WriteAllText(args[0],JsonSerializer.Serialize(doc,Json)+Environment.NewLine,new UTF8Encoding(false));
            Console.WriteLine("Campaign frame oracle: 12 source sequence rows and 2 native contract rows written"); return 0;
        }
        catch(Exception e) { Console.Error.WriteLine(e); return 1; }
    }

    static List<Row> Build()
    {
        var rows=new List<Row>();
        Strategic(rows,"player-regular","Player",1,1,"fresh-positive-numeric-order");
        Strategic(rows,"player-paused-zero","Player",10,0,"fresh-zero", paused:true);
        Strategic(rows,"developer-four-quarter-substeps","Developer",1,1,"fresh-positive-numeric-order",demo:true);
        Strategic(rows,"developer-paused-no-substeps","Developer",1,0,null,paused:true,demo:true);

        var battle=new HeadlessSourceHost(Galaxy()); battle.Strategic.SetSpeed(SimulationClock.SpeedLevel.Fast);
        Sequence(rows,"battle-takes-strategic-pause",battle,"Player",new { Delta=.1,Action="BeginThenFrame" },()=>{var order=battle.Begin(1);return new { Order=order,Frame=battle.Frame(.1,false)};});
        var loadedWorld=Galaxy(); new CampaignMassiveCombat(new Hostility()).Begin(loadedWorld,1,1,0);
        var loaded=new HeadlessSourceHost(loadedWorld,true);
        Sequence(rows,"loaded-existing-battle-tactical-paused",loaded,"Player",new { Delta=.2,Action="RestoredFrame" },()=>loaded.Frame(.2,false));
        var menu=new HeadlessSourceHost(Galaxy()); menu.Begin(1); menu.SetTacticalSpeed(2);
        Sequence(rows,"menu-pause-zero-admission",menu,"Player",new { Delta=.2,Action="PauseForMenu" },()=>{menu.PauseForMenu();menu.MenuOpen=true;return menu.Frame(.2,false);});
        Sequence(rows,"menu-resume-restores-speed",menu,"Player",new { Delta=.2,Action="ResumeAfterMenu" },()=>{menu.MenuOpen=false;menu.ResumeAfterMenu();return menu.Frame(.2,false);});
        var complete=new HeadlessSourceHost(Galaxy()); complete.Strategic.SetSpeed(SimulationClock.SpeedLevel.VeryFast); complete.Begin(1);
        complete.World.ActiveCombatEncounter!.Battle.Formations.Single(f=>f.CivilizationId==2).Surrendered=true;
        Sequence(rows,"completed-battle-restores-speed-consumes-frame",complete,"Player",new { Delta=0d,Action="ForceSurrenderThenFrame" },()=>complete.Frame(0,false));
        var existing=new HeadlessSourceHost(Galaxy()); existing.Begin(1);
        Sequence(rows,"begin-existing-battle-rejected",existing,"Player",new { FleetId=1,Action="BeginNewOnly" },()=>existing.Begin(1));
        Strategic(rows,"save-ready-only-after-complete-frame","Player",1,0,"fresh-positive-numeric-order");
        Strategic(rows,"cumulative-substep-end-days","Developer",1,0,"fresh-positive-numeric-order",demo:true);
        rows.Add(new("move-owner-retains-borrowed-diplomacy","Player","Ownership",new{},new{},new{},new { StableOwnerRequired=true,CanonicalWorldCount=1 }));
        rows.Add(new("failed-step-no-completed-frame","Player","StrategicFailure",new { Delta=1d },new{},new{},new { ReadyForSaveCapture=false,CompletedStrategicResults=0 },"core-failure-missing-economy"));
        return rows;
    }
    static void Strategic(List<Row> rows,string name,string policy,double delta,double restored,string? integrated,bool paused=false,bool demo=false)
    {
        var host=new HeadlessSourceHost(Galaxy()); if(demo)host.Strategic.SetSpeed(SimulationClock.SpeedLevel.Demo); if(paused)host.Strategic.SetSpeed(SimulationClock.SpeedLevel.Paused); host.Strategic.Restore(restored);
        var before=Node(host.State()); var result=Node(host.Frame(delta,policy=="Developer")); rows.Add(new(name,policy,"Strategic",Node(new { Delta=delta }),before,Node(host.State()),result,integrated));
    }
    static void Sequence(List<Row> rows,string name,HeadlessSourceHost host,string policy,object input,Func<object> call)
    { var before=Node(host.State());var result=Node(call());rows.Add(new(name,policy,"Tactical",Node(input),before,Node(host.State()),result)); }
    static object Clock(SimulationClock c)=>new { Speed=(int)c.Speed,ResumeSpeed=(int)c.ResumeSpeed,c.SimulationDays,c.EffectiveMultiplier,c.RequestedMultiplier,c.BacklogDays };
    static CivilizationState Civilization(int id)=>new(id,$"C{id}",7,CivilizationArchetype.Adaptive,CivilizationTraits.Balanced,id==1,CivilizationDevelopmentStage.WarpCapable,false,SpeciesId:SpeciesCatalog.TerranBaselineId);
    static GalaxyState Galaxy(){var civs=new[]{Civilization(1),Civilization(2)};return new(){Seed=97,Systems=[new(7,"Anchor",Vector2.Zero,StarArchetype.Standard,true,false,false,false)],PlanetaryBodies=[],Civilizations=civs,Fleets=[Fleet(1,1,"Alpha"),Fleet(2,2,"Beta")],Colonies=[],Economies=[new(){CivilizationId=1,Credits=100,Industry=100},new(){CivilizationId=2,Credits=100,Industry=100}],Technologies=new TechnologySeeder().Seed(civs),ConstructionStates=new ConstructionSeeder().Seed(civs),ShipyardStates=new ShipyardSeeder().Seed(civs),PlayerCivilizationId=1,Knowledge=new CivilizationKnowledgeState()};}
    static FleetState Fleet(int id,int civ,string name)=>new(){Id=id,CivilizationId=civ,Name=name,Role=FleetRole.Military,Position=Vector2.Zero,CurrentSystemId=7,Combat=CombatProfileRegistry.CreateInitialState(CombatProfileIds.PatrolCorvetteMk1,FleetRole.Military),TacticalVessel=new(){Id=id,Name=name,DesignId=CombatProfileIds.PatrolCorvetteMk1,IsFlagship=id==1}};
    static object KnowledgeProjection(GalaxyState world)=>new{CoreObservers=Array.Empty<int>(),Observers=world.Civilizations.Select(c=>new{CivilizationId=c.Id,HasCoreAccess=world.Knowledge.HasGalacticCoreAccess(c.Id),CoreDiscovered=world.Knowledge.IsGalacticCoreDiscovered(c.Id),KnownSystems=world.Knowledge.GetKnownSystems(c.Id),KnownCivilizations=world.Knowledge.GetKnownCivilizations(c.Id),Survey=Array.Empty<object>()})};
    static object WorldProjection(GalaxyState world)=>new{world.Seed,world.Systems,Bodies=world.PlanetaryBodies,world.Civilizations,world.Fleets,world.Colonies,world.Economies,world.Technologies,Construction=world.ConstructionStates,Shipyards=world.ShipyardStates,world.PlayerCivilizationId,Knowledge=KnowledgeProjection(world),Core=(object?)null,UsedConstrainedHomeFallback=false,world.CombatIntelligence,SystemPositions=world.Systems.Select(v=>new{v.Id,X=v.Position.X,Y=v.Position.Y}),FleetPositions=world.Fleets.Select(v=>new{v.Id,X=v.Position.X,Y=v.Position.Y,LocalStartX=v.LocalTransitStart.X,LocalStartY=v.LocalTransitStart.Y,LocalPositionX=v.LocalTransitPosition.X,LocalPositionY=v.LocalTransitPosition.Y,LocalTargetX=v.LocalTransitTarget.X,LocalTargetY=v.LocalTransitTarget.Y})};
    static object? EncounterProjection(CampaignMassiveEncounter? encounter)=>encounter is null?null:new{encounter.SystemId,encounter.StartedDay,encounter.Reconciled,Battle=new{encounter.Battle.Tick,encounter.Battle.SimulatedSeconds,encounter.Battle.PendingSeconds,Formations=encounter.Battle.Formations.Select(f=>new{f.Id,f.CivilizationId,f.FleetId,f.ShieldPool,f.ArmorPool,f.HullPool,f.DestroyedShips,f.Escaped,f.Surrendered})},Vessels=encounter.Vessels};
    static JsonElement Node(object value)=>JsonSerializer.SerializeToElement(value,Json);
}
