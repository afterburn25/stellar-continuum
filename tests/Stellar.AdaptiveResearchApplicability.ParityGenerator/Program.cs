using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Simulation.Research.Adaptive;

CultureInfo.DefaultThreadCurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.DefaultThreadCurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 2) throw new ArgumentException("usage: ResearchApplicabilityOracle <research-directory> <output>");
var root = Path.GetFullPath(args[0]);
var output = Path.GetFullPath(args[1]);
var records = new List<object>();
using var scratch = new OwnedScratchDirectory();
var baseCatalog = AdaptiveResearchCatalogLoader.LoadFromDirectory(root);
RunLoad("canonical-load", root, baseCatalog);
foreach (var id in new[] { "metabolic_biology", "metabolic_suppression_possible", "machine_cognition_present", "biological_fabrication_possible" }) RunGet("get-" + id, root, id);
RunGet("get-case-sensitive-miss", root, "Metabolic_Biology");
RunGet("get-unknown", root, "missing_trait");
foreach (var item in new (string Name, Action<string> Change)[] {
  ("missing-file", d => File.Delete(Path.Combine(d,"applicability_traits.json"))),
  ("malformed-json", d => File.WriteAllText(Path.Combine(d,"applicability_traits.json"), "{")),
  ("catalog-mismatch", d => Set(d,"/catalog_id", "\"wrong.catalog\"")),
  ("missing-catalog-id", d => Remove(d,"/catalog_id")),
  ("null-catalog-id", d => Set(d,"/catalog_id", "null")),
  ("catalog-id-wrong-type", d => Set(d,"/catalog_id", "17")),
  ("missing-traits", d => Remove(d,"/traits")),
  ("traits-wrong-type", d => Set(d,"/traits", "{}")),
  ("missing-id", d => Remove(d,"/traits/0/id")),
  ("null-id", d => Set(d,"/traits/0/id", "null")),
  ("empty-id", d => Set(d,"/traits/0/id", "\"\"")),
  ("missing-scope", d => Remove(d,"/traits/0/scope")),
  ("null-scope", d => Set(d,"/traits/0/scope", "null")),
  ("unknown-scope", d => Set(d,"/traits/0/scope", "\"Civilization\"")),
  ("missing-mutable", d => Remove(d,"/traits/0/mutable")),
  ("mutable-wrong-type", d => Set(d,"/traits/0/mutable", "\"true\"")),
  ("unknown-id", d => Set(d,"/traits/0/id", "\"missing_trait\"")),
  ("duplicate-id", d => CopyAppend(d,"/traits/0","/traits")),
  ("count-mismatch", d => Remove(d,"/traits/0")),
}) RunMutation(item.Name, root, baseCatalog, item.Change);
var fixture = new { Schema = "stellar-adaptive-research-applicability-oracle-v1", Cases = records, SourceOnlyBoundaries = new[] {
  "ArgumentNullException for a null catalog cannot arise through the native reference API.",
  "Path.GetFullPath, File.ReadAllText, and JsonDocument parser/access exception types are platform/runtime boundaries; semantic messages are compared exactly.",
  "The retained ReadOnlyDictionary blocks mutation through the public API; native views establish an equivalent const-query boundary." }, Metadata = new { Culture = "InvariantCulture", ProductionRecords = records.Count } };
Directory.CreateDirectory(Path.GetDirectoryName(output)!); File.WriteAllText(output, JsonSerializer.Serialize(fixture, new JsonSerializerOptions { WriteIndented = true }) + Environment.NewLine);
Console.WriteLine($"adaptive research applicability oracle: {records.Count} cases");

void RunLoad(string name, string directory, AdaptiveResearchCatalog catalog) {
  var before = Fingerprint(directory);
  AdaptiveResearchApplicabilityCatalog? result = null; Exception? error = null;
  try { result = AdaptiveResearchApplicabilityCatalog.LoadFromDirectory(directory, catalog); }
  catch (Exception caught) when (caught is IOException or UnauthorizedAccessException or JsonException or InvalidDataException or InvalidOperationException or KeyNotFoundException) { error = caught; }
  records.Add(new { Name = name, Kind = "Load", InputFingerprintBefore = before, InputFingerprintAfter = Fingerprint(directory), TraitFileUtf8Base64 = TraitBytes(directory), Result = result is null ? null : ProjectCatalog(result), Error = Error(error, directory) });
}
void RunGet(string name, string directory, string id) {
  var baseCatalog = AdaptiveResearchCatalogLoader.LoadFromDirectory(directory); var catalog = AdaptiveResearchApplicabilityCatalog.LoadFromDirectory(directory, baseCatalog); var before = Fingerprint(directory);
  ResearchApplicabilityTraitDefinition? result = null; Exception? error = null;
  try { result = catalog.GetTrait(id); }
  catch (Exception caught) when (caught is KeyNotFoundException) { error = caught; }
  records.Add(new { Name = name, Kind = "GetTrait", Arguments = new { TraitId = id }, InputFingerprintBefore = before, InputFingerprintAfter = Fingerprint(directory), TraitFileUtf8Base64 = TraitBytes(directory), Result = result is null ? null : ProjectTrait(result), Error = Error(error, directory) });
}
void RunMutation(string name, string source, AdaptiveResearchCatalog catalog, Action<string> change) { var target = Path.Combine(scratch.Path, "case-" + records.Count); Copy(source, target); change(target); RunLoad(name, target, catalog); }
static object ProjectCatalog(AdaptiveResearchApplicabilityCatalog value) => new { Traits = value.Traits.Values.Select(ProjectTrait).ToArray() };
static object ProjectTrait(ResearchApplicabilityTraitDefinition value) => new { value.Id, Scope = (int)value.Scope, value.Mutable };
static object? Error(Exception? value, string root) => value is null ? null : new { Type = value.GetType().Name, Message = value.Message.Replace(root, "<ROOT>", StringComparison.OrdinalIgnoreCase) };
static string? TraitBytes(string directory) { var path = System.IO.Path.Combine(directory, "applicability_traits.json"); return File.Exists(path) ? Convert.ToBase64String(File.ReadAllBytes(path)) : null; }
static string Fingerprint(string directory) { using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256); foreach (var path in Directory.EnumerateFiles(directory).OrderBy(System.IO.Path.GetFileName, StringComparer.Ordinal)) { hash.AppendData(Encoding.UTF8.GetBytes(System.IO.Path.GetFileName(path)!)); hash.AppendData(File.ReadAllBytes(path)); } return Convert.ToHexString(hash.GetHashAndReset()); }
static void Copy(string source, string target) { Directory.CreateDirectory(target); foreach (var f in Directory.EnumerateFiles(source)) File.Copy(f, Path.Combine(target, Path.GetFileName(f))); }
static JsonNode Node(string dir) => JsonNode.Parse(File.ReadAllText(Path.Combine(dir,"applicability_traits.json")))!;
static void Save(string d, JsonNode n) => File.WriteAllText(Path.Combine(d,"applicability_traits.json"), n.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
static JsonNode Resolve(JsonNode root, string pointer) { foreach (var s in pointer.Split('/', StringSplitOptions.RemoveEmptyEntries)) root = root is JsonArray a ? a[int.Parse(s)]! : root[s]!; return root; }
static (JsonNode Parent,string Segment) Parent(JsonNode root,string pointer) { var s=pointer.Split('/',StringSplitOptions.RemoveEmptyEntries); return (s.Length==1?root:Resolve(root,"/"+string.Join('/',s[..^1])),s[^1]); }
static void Set(string d,string p,string v) { var n=Node(d); var (parent,s)=Parent(n,p); if(parent is JsonArray a)a[int.Parse(s)]=JsonNode.Parse(v);else parent[s]=JsonNode.Parse(v); Save(d,n); }
static void Remove(string d,string p) { var n=Node(d); var (parent,s)=Parent(n,p); if(parent is JsonArray a)a.RemoveAt(int.Parse(s));else parent.AsObject().Remove(s); Save(d,n); }
static void CopyAppend(string d,string source,string target) { var n=Node(d); Resolve(n,target).AsArray().Add(Resolve(n,source).DeepClone()); Save(d,n); }
sealed class OwnedScratchDirectory : IDisposable
{
    private readonly string _temporaryParent = System.IO.Path.GetFullPath(System.IO.Path.GetTempPath()).TrimEnd(System.IO.Path.DirectorySeparatorChar, System.IO.Path.AltDirectorySeparatorChar);
    public string Path { get; }
    public OwnedScratchDirectory()
    {
        Path = System.IO.Path.GetFullPath(System.IO.Path.Combine(_temporaryParent, "stellar-applicability-045-" + Guid.NewGuid().ToString("N")));
        if (Directory.Exists(Path)) throw new IOException($"Refusing to reuse pre-existing scratch directory '{Path}'.");
        Directory.CreateDirectory(Path);
    }
    public void Dispose()
    {
        var canonical = System.IO.Path.GetFullPath(Path);
        var parent = Directory.GetParent(canonical)?.FullName?.TrimEnd(System.IO.Path.DirectorySeparatorChar, System.IO.Path.AltDirectorySeparatorChar);
        if (!string.Equals(parent, _temporaryParent, StringComparison.OrdinalIgnoreCase) || !System.IO.Path.GetFileName(canonical).StartsWith("stellar-applicability-045-", StringComparison.Ordinal))
            throw new InvalidOperationException($"Refusing to recursively delete unowned scratch path '{canonical}'.");
        if (Directory.Exists(canonical)) Directory.Delete(canonical, true);
    }
}
