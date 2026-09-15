using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Game.Simulation.AI;
using Game.Simulation.Construction;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Research.Adaptive;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
var diagnosticRoot = args.Length > 0 ? args[0] : "<missing>";
var diagnosticFixture = args.Length > 1 ? args[1] : "<missing>";
try
{
    if (args.Length != 2) throw new ArgumentException("Expected canonical research directory and output fixture path.");
    var root = Path.GetFullPath(args[0]);
    var output = Path.GetFullPath(args[1]);
    string Fingerprint()
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (var path in Directory.GetFiles(root, "*.json").OrderBy(Path.GetFileName, StringComparer.Ordinal))
        { hash.AppendData(Encoding.UTF8.GetBytes(Path.GetFileName(path))); hash.AppendData(File.ReadAllBytes(path)); }
        return Convert.ToHexString(hash.GetHashAndReset());
    }
    object Error(Exception? e) => e is null ? null! : new { Type=e.GetType().Name, e.Message };
    var json = new JsonSerializerOptions { PropertyNamingPolicy=JsonNamingPolicy.CamelCase, Converters={new System.Text.Json.Serialization.JsonStringEnumConverter(JsonNamingPolicy.CamelCase)} };
    CivilizationState Civ(int id, string species=SpeciesCatalog.TerranBaselineId) => new(id,$"C{id}",0,CivilizationArchetype.Adaptive,CivilizationTraits.Balanced,false,CivilizationDevelopmentStage.WarpCapable,SpeciesId:species);
    GalaxyState Galaxy(params CivilizationState[] civs) => new() { Seed=1,Systems=Array.Empty<StarSystemState>(),PlanetaryBodies=Array.Empty<PlanetaryBodyState>(),Civilizations=civs.ToList(),Fleets=new List<FleetState>(),Colonies=new List<ColonyState>(),Economies=Array.Empty<CivilizationEconomyState>(),Technologies=new List<TechnologyState>(),ConstructionStates=new List<ConstructionState>(),ShipyardStates=new List<ShipyardState>(),PlayerCivilizationId=civs.FirstOrDefault()?.Id ?? 0,Knowledge=new CivilizationKnowledgeState() };
    object Projection(AdaptiveResearchCampaignSnapshotCodec codec, AdaptiveResearchCampaignState c) => new { Ids=c.Civilizations.Keys.ToArray(), Starts=c.Starts.Values.ToArray(), Capture=JsonSerializer.SerializeToNode(codec.Capture(c),json), StateRevisions=c.Civilizations.Values.Select(s=>new{s.CivilizationId,s.Revision,s.MaterializedViewRevision,ExpertiseRevision=s.Expertise.Revision}).ToArray(), Funding=c.Civilizations.Keys.Select(id=>new{Id=id,Values=c.GetProjectFunding(id).Values.ToArray()}).ToArray() };
    var reserve=typeof(AdaptiveResearchCampaignState).GetMethod("ReserveProjectMilestones",BindingFlags.Instance|BindingFlags.NonPublic)!;
    var consume=typeof(AdaptiveResearchCampaignState).GetMethod("ConsumeProjectMilestone",BindingFlags.Instance|BindingFlags.NonPublic)!;
    var restoreFunding=typeof(AdaptiveResearchCampaignState).GetMethod("RestoreProjectFunding",BindingFlags.Instance|BindingFlags.NonPublic)!;
    void Reserve(AdaptiveResearchCampaignState c,int id,string node,double auth,double milestones){try{reserve.Invoke(c,new object[]{id,node,auth,milestones});}catch(TargetInvocationException e){throw e.InnerException!;}}
    object Consume(AdaptiveResearchCampaignState c,int id,string node,bool final){var values=new object?[]{id,node,final,0d,0d};try{var found=(bool)consume.Invoke(c,values)!;return new{Found=found,Consumed=(double)values[3]!,Remaining=(double)values[4]!};}catch(TargetInvocationException e){throw e.InnerException!;}}
    void RestoreFunding(AdaptiveResearchCampaignState c,int id,IEnumerable<AdaptiveResearchProjectFundingSnapshot> values){try{restoreFunding.Invoke(c,new object[]{id,values});}catch(TargetInvocationException e){throw e.InnerException!;}}
    var beforeLoad=Fingerprint(); var runtime=AdaptiveResearchStrategicRuntime.LoadFromDirectory(root); var factory=new AdaptiveResearchCampaignFactory(runtime); var codec=new AdaptiveResearchCampaignSnapshotCodec(runtime); var afterLoad=Fingerprint();
    if(beforeLoad!=afterLoad) throw new InvalidOperationException("Loading changed canonical inputs.");
    var rows=new List<object>();
    void FactoryRow(string name, GalaxyState galaxy){var before=Fingerprint();AdaptiveResearchCampaignState? value=null;Exception? error=null;try{value=factory.Create(galaxy);}catch(Exception e){error=e;}var after=Fingerprint();rows.Add(new{Name=name,Kind="factory",Input=galaxy.Civilizations.Select(c=>new{c.Id,c.SpeciesId}).ToArray(),BeforeFingerprint=before,AfterFingerprint=after,Error=Error(error),Result=value is null?null:Projection(codec,value)});}
    FactoryRow("factory-empty",Galaxy());
    FactoryRow("factory-four-sorted",Galaxy(Civ(9,SpeciesCatalog.CryogenicHydrocarbonId),Civ(-2,SpeciesCatalog.PelagicHighPressureId),Civ(5,SpeciesCatalog.CompactHighGravityId),Civ(1)));
    FactoryRow("factory-duplicate",Galaxy(Civ(2),Civ(2,SpeciesCatalog.PelagicHighPressureId)));
    FactoryRow("factory-unknown-species",Galaxy(Civ(3,"unknown")));
    var baseGalaxy=Galaxy(Civ(2),Civ(1,SpeciesCatalog.PelagicHighPressureId));
    var campaign=factory.Create(baseGalaxy); var baseline=codec.Capture(campaign);
    void RestoreRow(string name, AdaptiveResearchCampaignSnapshot input, GalaxyState? galaxy=null){galaxy??=baseGalaxy;var owned=JsonSerializer.Serialize(input,json);var before=Fingerprint();AdaptiveResearchCampaignState? value=null;Exception? error=null;try{value=codec.Restore(galaxy,input);}catch(Exception e){error=e;}var after=Fingerprint();rows.Add(new{Name=name,Kind="restore",Galaxy=galaxy.Civilizations.Select(c=>new{c.Id,c.SpeciesId}).ToArray(),Input=JsonSerializer.SerializeToNode(input,json),InputAfter=JsonSerializer.SerializeToNode(input,json),OwnedBefore=owned,OwnedAfter=JsonSerializer.Serialize(input,json),BeforeFingerprint=before,AfterFingerprint=after,Error=Error(error),Result=value is null?null:Projection(codec,value)});}
    RestoreRow("roundtrip",baseline);
    RestoreRow("schema-zero",baseline with {SchemaVersion=0});
    RestoreRow("schema-three",baseline with {SchemaVersion=3});
    RestoreRow("catalog-mismatch",baseline with {CatalogId="wrong"});
    RestoreRow("galaxy-duplicate",baseline,Galaxy(Civ(1),Civ(1)));
    RestoreRow("cardinality",baseline,Galaxy(Civ(1)));
    RestoreRow("unknown-civilization",baseline with {Civilizations=new[]{baseline.Civilizations[0] with {CivilizationId=99},baseline.Civilizations[1]}});
    RestoreRow("duplicate-civilization",baseline with {Civilizations=new[]{baseline.Civilizations[0],baseline.Civilizations[0]}});
    RestoreRow("identity-metadata",baseline with {Civilizations=new[]{baseline.Civilizations[0] with {SpeciesId="terran_baseline"},baseline.Civilizations[1]}});
    var originalV5=baseline.Civilizations[0].Research;
    var originalV4=originalV5.Research;
    var originalV3=originalV4.Research;
    var originalV2=originalV3.Research;
    var wrongV2=originalV2 with { Core=originalV2.Core with { CivilizationId="civilization:99" } };
    var wrongResearch=originalV5 with { Research=originalV4 with { Research=originalV3 with { Research=wrongV2 } } };
    RestoreRow("state-identity",baseline with {Civilizations=new[]{baseline.Civilizations[0] with {Research=wrongResearch},baseline.Civilizations[1]}});
    var invalidFunding=new[]{new AdaptiveResearchProjectFundingSnapshot("missing",1,0,0)};
    RestoreRow("schema1-ignores-funding",baseline with {SchemaVersion=1,Civilizations=baseline.Civilizations.Select(e=>e with {ProjectFunding=invalidFunding}).ToArray()});
    RestoreRow("inactive-funding",baseline with {Civilizations=new[]{baseline.Civilizations[0] with {ProjectFunding=invalidFunding},baseline.Civilizations[1]}});
    RestoreRow("invalid-funding-values",baseline with {Civilizations=new[]{baseline.Civilizations[0] with {ProjectFunding=new[]{new AdaptiveResearchProjectFundingSnapshot("\u2003",1,0,0)}},baseline.Civilizations[1]}});
    var activeCampaign=factory.Create(Galaxy(Civ(1))); var state=activeCampaign.GetCivilization(1); var view=runtime.Authority.BuildView(state); var candidate=view.VisibleNodes.First(n=>n.State==ResearchMaturity.Investigable&&n.Blockers.Count==0&&n.MinimumLabs is not null); var started=runtime.Authority.StartDirectedResearch(state,candidate.NodeId,candidate.MinimumLabs!.Value,$"species:{SpeciesCatalog.TerranBaselineId}"); if(!started.Accepted) throw new InvalidOperationException(started.Message); Reserve(activeCampaign,1,candidate.NodeId,2.5,9); var funded=codec.Capture(activeCampaign); RestoreRow("funded-roundtrip",funded,Galaxy(Civ(1)));
    var fundingSteps=new List<object>(); Reserve(activeCampaign,1,"second-slot",1,6);
    fundingSteps.Add(new{Op="consume-third",Result=Consume(activeCampaign,1,candidate.NodeId,false),State=Projection(codec,activeCampaign)});
    fundingSteps.Add(new{Op="consume-final-first-slot",Result=Consume(activeCampaign,1,candidate.NodeId,true),State=Projection(codec,activeCampaign)});
    fundingSteps.Add(new{Op="consume-final-second-slot",Result=Consume(activeCampaign,1,"second-slot",true),State=Projection(codec,activeCampaign)});
    fundingSteps.Add(new{Op="consume-missing",Result=Consume(activeCampaign,1,candidate.NodeId,false),State=Projection(codec,activeCampaign)});
    Reserve(activeCampaign,1,"reinsert-a",4,12); Reserve(activeCampaign,1,candidate.NodeId,5,15);
    fundingSteps.Add(new{Op="two-lifo-reinsertions",Result=(object?)null,State=Projection(codec,activeCampaign)});
    rows.Add(new{Name="funding-sequence",Kind="funding",Steps=fundingSteps});
    Exception? duplicateReserve=null;try{Reserve(activeCampaign,1,candidate.NodeId,1,1);}catch(Exception e){duplicateReserve=e;}
    rows.Add(new{Name="reserve-duplicate",Kind="funding",Error=Error(duplicateReserve),Result=Projection(codec,activeCampaign)});
    var partial=factory.Create(Galaxy(Civ(1))); var partialState=partial.GetCivilization(1); var partialView=runtime.Authority.BuildView(partialState); var partialCandidate=partialView.VisibleNodes.First(n=>n.State==ResearchMaturity.Investigable&&n.Blockers.Count==0&&n.MinimumLabs is not null); var partialStart=runtime.Authority.StartDirectedResearch(partialState,partialCandidate.NodeId,partialCandidate.MinimumLabs!.Value,$"species:{SpeciesCatalog.TerranBaselineId}"); if(!partialStart.Accepted)throw new InvalidOperationException(partialStart.Message); Exception? partialError=null;try{RestoreFunding(partial,1,new[]{new AdaptiveResearchProjectFundingSnapshot(partialCandidate.NodeId,3,1,2),new AdaptiveResearchProjectFundingSnapshot("inactive",1,0,0)});}catch(Exception e){partialError=e;} rows.Add(new{Name="partial-funding-restore",Kind="funding",Error=Error(partialError),Result=Projection(codec,partial)});
    RestoreRow("duplicate-funding",funded with {Civilizations=new[]{funded.Civilizations[0] with {ProjectFunding=new[]{funded.Civilizations[0].ProjectFunding![0],funded.Civilizations[0].ProjectFunding![0]}}}},Galaxy(Civ(1)));
    var other=AdaptiveResearchStrategicRuntime.LoadFromDirectory(root);var otherCodec=new AdaptiveResearchCampaignSnapshotCodec(other);Exception? captureError=null;try{_ = otherCodec.Capture(campaign);}catch(Exception e){captureError=e;}rows.Add(new{Name="foreign-runtime-capture",Kind="capture",Error=Error(captureError)});
    var document=new{Authority="AdaptiveResearchCampaignState.cs",Culture="InvariantCulture",CanonicalFingerprint=beforeLoad,Rows=rows};Directory.CreateDirectory(Path.GetDirectoryName(output)!);File.WriteAllText(output,JsonSerializer.Serialize(document,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine($"adaptive research campaign oracle: {rows.Count} rows -> {output}");return 0;
}
catch(Exception e){Console.Error.WriteLine(e);Console.Error.WriteLine($"CurrentDirectory: {Environment.CurrentDirectory}");Console.Error.WriteLine($"ResearchRoot: {diagnosticRoot}");Console.Error.WriteLine($"FixturePath: {diagnosticFixture}");return 1;}
