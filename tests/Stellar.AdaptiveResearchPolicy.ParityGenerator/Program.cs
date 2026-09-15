using System.Globalization;
using System.Text;
using System.Text.Json;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 2) throw new ArgumentException("Expected canonical research directory and output fixture path.");
var canonical = Path.GetFullPath(args[0]);
var output = Path.GetFullPath(args[1]);
var json = new JsonSerializerOptions { WriteIndented = false, IncludeFields = true, NumberHandling = System.Text.Json.Serialization.JsonNumberHandling.AllowNamedFloatingPointLiterals };
JsonElement Freeze(object value) => JsonSerializer.SerializeToElement(value, json);
object Error(Exception e) => new { Type = e.GetType().Name, e.Message };
object Success(string name, object value, string before, string after) => new { Name = name, InputFingerprintBefore = before, InputFingerprintAfter = after, Result = (JsonElement?)Freeze(value), Error = (JsonElement?)null };
object Failure(string name, Exception error, string before, string after) => new { Name = name, InputFingerprintBefore = before, InputFingerprintAfter = after, Result = (JsonElement?)null, Error = (JsonElement?)Freeze(Error(error)) };
string InputFingerprint(string root) {
    const ulong offset = 14695981039346656037UL;
    const ulong prime = 1099511628211UL;
    var hash = offset;
    void Add(byte value) { hash ^= value; unchecked { hash *= prime; } }
    foreach (var path in Directory.EnumerateFiles(root).OrderBy(Path.GetFileName, StringComparer.Ordinal)) {
        foreach (var value in Encoding.UTF8.GetBytes(Path.GetFileName(path))) Add(value);
        Add(0);
        foreach (var value in File.ReadAllBytes(path)) Add(value);
        Add(255);
    }
    return hash.ToString("X16", CultureInfo.InvariantCulture);
}
AdaptiveResearchCatalog Catalog(string path) => AdaptiveResearchCatalogLoader.LoadFromDirectory(path);
object FacilityProjection(AdaptiveResearchFacilityCatalog f, AdaptiveResearchCatalog c) => new {
    Capabilities = f.FacilityCapabilityIds.ToArray(),
    Institutions = f.Institutions.Select(x => new { x.Key, x.Value.Id, x.Value.EffectiveLabUnits, Capabilities = x.Value.FacilityCapabilities }).ToArray(),
    Requirements = c.Nodes.Keys.SelectMany(node => Enum.GetValues<ResearchMaturity>(), (node, stage) => new { Node = node, Stage = stage, Value = f.GetStageRequirement(node, stage) }).Where(x => x.Value is not null).Select(x => new { x.Node, x.Stage, AllOf = x.Value!.AllOf, AnyOf = x.Value.AnyOf }).ToArray(),
    StageQueries = c.Nodes.Keys.SelectMany(node => Enum.GetValues<ResearchMaturity>(), (node, stage) => new { Node = node, Stage = stage, Present = f.GetStageRequirement(node, stage) is not null }).Concat(Enum.GetValues<ResearchMaturity>().Select(stage => new { Node = "unknown_policy_node", Stage = stage, Present = f.GetStageRequirement("unknown_policy_node", stage) is not null })).Append(new { Node = c.Nodes.Keys.First(), Stage = (ResearchMaturity)0, Present = f.GetStageRequirement(c.Nodes.Keys.First(), (ResearchMaturity)0) is not null }).ToArray(),
    InstitutionQueries = new[] { new { Known = f.Institutions.First().Key, KnownFound = f.Institutions.ContainsKey(f.Institutions.First().Key), UnknownFound = f.Institutions.ContainsKey("unknown_institution") } }
};
object PolicyProjection(AdaptiveResearchProgressPolicy p, AdaptiveResearchCatalog c) => new {
    Stages = p.StageBands.Select(x => new { Stage = x.Key, x.Value.StartFraction, x.Value.EndFraction, WorkFraction = x.Value.WorkFraction }).ToArray(),
    Readiness = p.ReadinessBands,
    StageWork = c.Nodes.Values.Select(n => new { n.Id, Experimental = p.GetStageWork(n, ResearchMaturity.Experimental), Demonstrated = p.GetStageWork(n, ResearchMaturity.Demonstrated), Engineering = p.GetStageWork(n, ResearchMaturity.Engineering) }).ToArray(),
    Efficiency = new[] { double.NegativeInfinity, -1d, 0d, 19.999998d, 19.999999d, 20d, 39.999999d, 40d, 59.999999d, 60d, 79.999999d, 80d, 100d, 101d, double.PositiveInfinity, double.NaN }.Select(x => new { Input = x, Result = p.GetReadinessEfficiency(x) }).ToArray()
};
object FacilityRecord(string name, string path, AdaptiveResearchCatalog catalog) {
    var before = InputFingerprint(path);
    AdaptiveResearchFacilityCatalog? loaded = null;
    Exception? failure = null;
    try { loaded = AdaptiveResearchFacilityCatalog.LoadFromDirectory(path, catalog); }
    catch (Exception e) { failure = e; }
    var after = InputFingerprint(path);
    return failure is null ? Success(name, FacilityProjection(loaded!, catalog), before, after) : Failure(name, failure, before, after);
}
object PolicyRecord(string name, string path, AdaptiveResearchCatalog catalog) {
    var before = InputFingerprint(path);
    AdaptiveResearchProgressPolicy? loaded = null;
    Exception? failure = null;
    try { loaded = AdaptiveResearchProgressPolicy.LoadFromDirectory(path, catalog); }
    catch (Exception e) { failure = e; }
    var after = InputFingerprint(path);
    return failure is null ? Success(name, PolicyProjection(loaded!, catalog), before, after) : Failure(name, failure, before, after);
}
object StageBandRecord(string name, string path, AdaptiveResearchProgressPolicy policy, ResearchMaturity maturity) {
    var before = InputFingerprint(path);
    ResearchStageWorkBand? result = null;
    Exception? failure = null;
    try { result = policy.GetStageBand(maturity); }
    catch (Exception e) { failure = e; }
    var after = InputFingerprint(path);
    return failure is null ? Success(name, result!, before, after) : Failure(name, failure, before, after);
}

var records = new List<object>();
var catalog = Catalog(canonical);
AdaptiveResearchFacilityCatalog canonicalFacilities;
var canonicalFacilitiesBefore = InputFingerprint(canonical);
try { canonicalFacilities = AdaptiveResearchFacilityCatalog.LoadFromDirectory(canonical, catalog); }
catch (Exception e) { var after = InputFingerprint(canonical); records.Add(Failure("canonical-facilities", e, canonicalFacilitiesBefore, after)); throw; }
var canonicalFacilitiesAfter = InputFingerprint(canonical);
records.Add(Success("canonical-facilities", FacilityProjection(canonicalFacilities, catalog), canonicalFacilitiesBefore, canonicalFacilitiesAfter));
AdaptiveResearchProgressPolicy canonicalPolicy;
var canonicalPolicyBefore = InputFingerprint(canonical);
try { canonicalPolicy = AdaptiveResearchProgressPolicy.LoadFromDirectory(canonical, catalog); }
catch (Exception e) { var after = InputFingerprint(canonical); records.Add(Failure("canonical-progress", e, canonicalPolicyBefore, after)); throw; }
var canonicalPolicyAfter = InputFingerprint(canonical);
records.Add(Success("canonical-progress", PolicyProjection(canonicalPolicy, catalog), canonicalPolicyBefore, canonicalPolicyAfter));

string NewRoot() {
    for (var attempt = 0; attempt < 100; attempt++) {
        var path = Path.Combine(Path.GetTempPath(), "stellar-research-policy-oracle-043-" + Guid.NewGuid().ToString("N"));
        if (Directory.Exists(path)) continue;
        Directory.CreateDirectory(path);
        return Path.GetFullPath(path);
    }
    throw new IOException("Could not create owned oracle scratch directory.");
}
void WithCopy(Action<string> action) {
    var root = NewRoot();
    try { DirectoryCopy(canonical, root); action(root); }
    finally { if (Directory.Exists(root) && Path.GetFullPath(root) == root) Directory.Delete(root, true); }
}
void FacilityCase(string name, Action<string> mutate) => WithCopy(root => { mutate(root); var localCatalog = Catalog(root); records.Add(FacilityRecord(name, root, localCatalog)); });
void PolicyCase(string name, Action<string> mutate) => WithCopy(root => { mutate(root); var localCatalog = Catalog(root); records.Add(PolicyRecord(name, root, localCatalog)); });
void Replace(string root, string file, string from, string to) { var path=Path.Combine(root,file); var text=File.ReadAllText(path); if (!text.Contains(from, StringComparison.Ordinal)) throw new InvalidOperationException("mutation target missing: " + from); File.WriteAllText(path,text.Replace(from,to,StringComparison.Ordinal)); }

FacilityCase("facility-missing-listed-file", root => Replace(root,"research_facility_index.json","research_facility_model.json","missing_facility.json"));
PolicyCase("progress-first-band-not-zero", root => Replace(root,"project_readiness_model.json","\"min\":0","\"min\":1"));
FacilityCase("facility-duplicate-declared-capability", root => Replace(root,"biochemical_research_facilities.json","{\"id\":\"alternative_biochemistry_experimentation\"","{\"id\":\"general_experimentation\""));
FacilityCase("facility-unknown-institution-capability", root => Replace(root,"research_facility_model.json","[\"general_experimentation\"]","[\"not_declared\"]"));
FacilityCase("facility-nonpositive-labs", root => Replace(root,"research_facility_model.json","\"effective_lab_units\":1,","\"effective_lab_units\":0,"));
FacilityCase("facility-unknown-node", root => Replace(root,"research_facility_model.json","\"prototype_warp_drive\"","\"unknown_policy_node\""));
FacilityCase("facility-unsupported-stage", root => Replace(root,"research_facility_model.json","\"experimental\":{\"all_of\":[\"high_energy_experimentation\"","\"mature\":{\"all_of\":[\"high_energy_experimentation\""));
FacilityCase("facility-unknown-required-capability", root => Replace(root,"research_facility_model.json","\"large_scale_prototyping\"]}","\"not_declared\"]}"));
FacilityCase("facility-unprovided-required-capability", root => { Replace(root,"biochemical_research_facilities.json","\"facility_capabilities\": [","\"facility_capabilities\": [{\"id\":\"orphan_capability\"},"); Replace(root,"research_facility_model.json","\"large_scale_prototyping\"]}","\"orphan_capability\"]}"); });
PolicyCase("progress-stage-gap", root => Replace(root,"maturation_model.json","\"typical_rp_fraction_start\": 0.45","\"typical_rp_fraction_start\": 0.46"));
PolicyCase("progress-stage-overlap", root => Replace(root,"maturation_model.json","\"typical_rp_fraction_start\": 0.45","\"typical_rp_fraction_start\": 0.44"));
PolicyCase("progress-stage-end-overflow", root => Replace(root,"maturation_model.json","\"typical_rp_fraction_end\": 1.0","\"typical_rp_fraction_end\": 1.1"));
PolicyCase("progress-readiness-duplicate-threshold", root => Replace(root,"project_readiness_model.json","\"min\":20","\"min\":0"));
PolicyCase("progress-readiness-insufficient-coverage", root => Replace(root,"project_readiness_model.json","\"max\":100","\"max\":99"));
PolicyCase("progress-readiness-transition-gap", root => Replace(root,"project_readiness_model.json","\"min\":20","\"min\":21"));
PolicyCase("progress-readiness-bad-efficiency", root => Replace(root,"project_readiness_model.json","\"efficiency\":0.35","\"efficiency\":0"));
FacilityCase("facility-empty-capability-id", root => { foreach (var file in new[] { "research_facility_model.json", "biochemical_research_facilities.json" }) Replace(root,file,"\"general_experimentation\"","\"\""); });
FacilityCase("facility-empty-institution-id", root => Replace(root,"research_facility_model.json","\"id\":\"general_research_laboratory\"","\"id\":\"\""));
FacilityCase("facility-duplicate-institution", root => Replace(root,"biochemical_research_facilities.json","\"id\":\"alternative_biochemistry_institute\"","\"id\":\"general_research_laboratory\""));
FacilityCase("facility-duplicate-stage-requirement", root => Replace(root,"biochemical_research_facilities.json","\"stage_requirements\": {","\"stage_requirements\": {" + Environment.NewLine + "    \"prototype_warp_drive\":{\"experimental\":{\"all_of\":[\"high_energy_experimentation\",\"field_physics_experimentation\",\"precision_measurement\"],\"any_of\":[]}},"));
FacilityCase("facility-negative-fractional-labs", root => Replace(root,"research_facility_model.json","\"effective_lab_units\":1,","\"effective_lab_units\":-0.125,"));
FacilityCase("facility-negative-scientific-labs", root => Replace(root,"research_facility_model.json","\"effective_lab_units\":1,","\"effective_lab_units\":-1e-7,"));
FacilityCase("facility-json-syntax-boundary", root => File.AppendAllText(Path.Combine(root,"research_facility_model.json"), "{"));
FacilityCase("facility-type-boundary", root => Replace(root,"research_facility_model.json","\"effective_lab_units\":1,","\"effective_lab_units\":\"bad\","));
records.Add(StageBandRecord("progress-get-stage-band-mature", canonical, canonicalPolicy, ResearchMaturity.Mature));
records.Add(StageBandRecord("progress-get-stage-band-archived", canonical, canonicalPolicy, ResearchMaturity.Archived));

File.WriteAllText(output, JsonSerializer.Serialize(new { Schema = "stellar-adaptive-research-policy-oracle-v2", Records = records, Metadata = new { Source = "actual retained C# AdaptiveResearchFacilityCatalog and AdaptiveResearchProgressPolicy", Culture = "InvariantCulture", ProductionRecords = records.Count } }, json) + Environment.NewLine);

static void DirectoryCopy(string source, string destination) { foreach (var file in Directory.EnumerateFiles(source)) File.Copy(file, Path.Combine(destination, Path.GetFileName(file))); }
