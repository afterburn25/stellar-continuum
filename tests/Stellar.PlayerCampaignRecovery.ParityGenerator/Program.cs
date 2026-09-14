using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using Game.Campaign;
using Game.Persistence;
using Game.Simulation.Generation;

try
{
if (args.Length != 3) throw new ArgumentException("Usage: PlayerCampaignRecovery.ParityGenerator <fixture.json> <new evidence-directory> <Game-source-root>");
var fixturePath = Path.GetFullPath(args[0]);
var evidenceRoot = Path.GetFullPath(args[1]);
if (Directory.Exists(evidenceRoot)) throw new IOException("Use a new evidence directory; existing evidence is never deleted.");
Directory.CreateDirectory(evidenceRoot);

// Game.csproj excludes this maintained tests project from its source glob.
// This tool invokes the maintained C# implementation
// to record a source oracle; it does not reproduce LoadExisting's decisions.
var root = Path.Combine(Path.GetTempPath(), "stellar-player-recovery-098-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(root);
try
{
    var report = new List<object>();
    var evidencePath = Path.Combine(root, "evidence.json");
    SeedPair(new CampaignSessionService(), evidencePath);
    File.Copy(evidencePath, Path.Combine(evidenceRoot, "primary.json"));
    File.Copy(evidencePath + ".bak", Path.Combine(evidenceRoot, "backup.json"));
    Run("primary-success", SeedPair, (service, path) =>
    {
        var before = SaveHashes(path);
        var updates = new List<CampaignRestorationProgress>();
        var loaded = service.LoadExisting(path, updates.Add);
        Require(loaded.Source == CampaignBootstrapSource.LoadedSave, "primary source");
        Require(before.SequenceEqual(SaveHashes(path)), "load changed files");
        return Record(loaded, updates, path, "success");
    });
    Run("corrupt-primary-backup-recovery", SeedPair, (service, path) =>
    {
        File.WriteAllText(path, "{ malformed primary", new UTF8Encoding(false));
        var before = SaveHashes(path);
        var updates = new List<CampaignRestorationProgress>();
        var loaded = service.LoadExisting(path, updates.Add);
        Require(loaded.Source == CampaignBootstrapSource.RecoveredFromBackup, "backup source");
        Require(before.SequenceEqual(SaveHashes(path)), "recovery changed files");
        return Record(loaded, updates, path, "success");
    });
    Run("missing-primary-backup-recovery", SeedPair, (service, path) =>
    {
        File.Delete(path);
        var before = SaveHashes(path);
        var updates = new List<CampaignRestorationProgress>();
        var loaded = service.LoadExisting(path, updates.Add);
        Require(loaded.Source == CampaignBootstrapSource.RecoveredFromBackup, "missing-primary backup source");
        Require(before.SequenceEqual(SaveHashes(path)), "missing-primary recovery changed files");
        return Record(loaded, updates, path, "success");
    });
    Run("both-malformed", SeedPair, (service, path) =>
    {
        File.WriteAllText(path, "{ malformed primary", new UTF8Encoding(false));
        File.WriteAllText(path + ".bak", "{ malformed backup", new UTF8Encoding(false));
        return FailedRecord(service, path);
    });
    Run("both-missing", NoFiles, (service, path) => FailedRecord(service, path));
    Run("developer-envelope-rejected", SeedPair, (service, path) =>
    {
        File.WriteAllText(path, "{\"DeveloperFormatVersion\":1}", new UTF8Encoding(false));
        File.WriteAllText(path + ".bak", "{\"DeveloperFormatVersion\":1}", new UTF8Encoding(false));
        return FailedRecord(service, path);
    });
    Run("blank-primary-backup-valid", SeedPair, (service, path) =>
    {
        File.WriteAllText(path, "", new UTF8Encoding(false));
        var updates = new List<CampaignRestorationProgress>();
        var loaded = service.LoadExisting(path, updates.Add);
        Require(loaded.RecoveredFromBackup, "blank primary did not fall back");
        return Record(loaded, updates, path, "success");
    });
    Run("primary-valid-backup-malformed", SeedPair, (service, path) =>
    {
        File.WriteAllText(path + ".bak", "not-json", new UTF8Encoding(false));
        var updates = new List<CampaignRestorationProgress>();
        var loaded = service.LoadExisting(path, updates.Add);
        Require(loaded.Source == CampaignBootstrapSource.LoadedSave, "primary was not preferred");
        return Record(loaded, updates, path, "success");
    });
    Run("primary-progress-stages", SeedPair, (service, path) =>
    {
        var updates = new List<CampaignRestorationProgress>();
        _ = service.LoadExisting(path, updates.Add);
        Require(updates.Zip(updates.Skip(1)).All(x => x.First.Fraction <= x.Second.Fraction), "progress was not monotonic");
        return new { status = "success", progress = updates };
    });
    Run("backup-progress-carry", SeedPair, (service, path) =>
    {
        File.WriteAllText(path, "{ malformed primary", new UTF8Encoding(false));
        var updates = new List<CampaignRestorationProgress>();
        _ = service.LoadExisting(path, updates.Add);
        Require(updates.Zip(updates.Skip(1)).All(x => x.First.Fraction <= x.Second.Fraction), "fallback progress was not monotonic");
        Require(updates.Any(x => x.Fraction >= .12 && x.Status.Contains("backup", StringComparison.OrdinalIgnoreCase)), "backup floor/status absent");
        return new { status = "success", progress = updates };
    });

    Run("utf8-bom-primary", SeedPair, (service, path) =>
    {
        File.WriteAllBytes(path, new byte[] { 0xef, 0xbb, 0xbf }.Concat(File.ReadAllBytes(path)).ToArray());
        var updates = new List<CampaignRestorationProgress>();
        return Record(service.LoadExisting(path, updates.Add), updates, path, "success");
    });
    Run("utf16-bom-primary-source-only", SeedPair, (service, path) =>
    {
        File.WriteAllText(path, File.ReadAllText(path), Encoding.Unicode);
        var updates = new List<CampaignRestorationProgress>();
        return new { nativeComparable = false, exclusion = "Native recovery deliberately accepts strict UTF-8 only.",
            source = Record(service.LoadExisting(path, updates.Add), updates, path, "success") };
    });
    Run("invalid-utf8-primary-backup-recovery", SeedPair, (service, path) =>
    {
        File.WriteAllBytes(path, new byte[] { 0xff, 0xfe, 0xfd });
        var updates = new List<CampaignRestorationProgress>();
        return Record(service.LoadExisting(path, updates.Add), updates, path, "success");
    });
    Run("directory-primary-backup-recovery", SeedPair, (service, path) =>
    {
        File.Delete(path); Directory.CreateDirectory(path);
        var updates = new List<CampaignRestorationProgress>();
        return Record(service.LoadExisting(path, updates.Add), updates, path, "success");
    });
    Run("unicode-路径-🚀", SeedPair, (service, path) =>
    {
        var updates = new List<CampaignRestorationProgress>();
        return Record(service.LoadExisting(path, updates.Add), updates, path, "success");
    });
    Run("callback-primary-failure-backup-recovery", SeedPair, (service, path) =>
    {
        var updates = new List<CampaignRestorationProgress>(); var first = true;
        var loaded = service.LoadExisting(path, update => { if (first) { first = false; throw new CallbackMarkerException("callback-marker"); } updates.Add(update); });
        return Record(loaded, updates, path, "success");
    });
    try { _ = new CampaignSessionService().LoadExisting(" \t\r\n "); throw new InvalidOperationException("whitespace path accepted"); }
    catch (ArgumentException error) { report.Add(new { name = "whitespace-path-rejected", result = new { status="failure", type=error.GetType().FullName, message=error.Message } }); }

    var gameRoot = Path.GetFullPath(args[2]);
    var sourceFiles = new[] { "Campaign/CampaignSessionService.cs", "Persistence/CampaignStatePersistenceService.cs" }
        .Select(relative => new { path = relative, sha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(gameRoot, relative)))) });
    var document = new { schemaVersion = 1, rowCount = report.Count, sourceFiles, encoding = new { nativeAccepted = "strict UTF-8 with optional UTF-8 BOM", excluded = new[] { "UTF-16LE", "UTF-16BE", "legacy code pages", "malformed UTF-8 byte streams (source decoding can reach JSON parsing before fallback)" } }, rows = report };
    var json = System.Text.Json.JsonSerializer.Serialize(document, new System.Text.Json.JsonSerializerOptions { WriteIndented = true });
    Directory.CreateDirectory(Path.GetDirectoryName(fixturePath)!); File.WriteAllText(fixturePath, json + Environment.NewLine, new UTF8Encoding(false));
    Console.WriteLine($"player recovery source oracle: {report.Count} rows");

    void Run(string name, Action<CampaignSessionService, string> arrange, Func<CampaignSessionService, string, object> execute)
    {
        var caseRoot = Path.Combine(root, name);
        Directory.CreateDirectory(caseRoot);
        var path = Path.Combine(caseRoot, "autosave.json");
        var service = new CampaignSessionService();
        arrange(service, path);
        report.Add(new { name, result = execute(service, path) });
    }
}
finally
{
    if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
}
}
catch (Exception error)
{
    Console.Error.WriteLine($"Exception: {error}\nCurrent directory: {Environment.CurrentDirectory}");
    Environment.ExitCode = 1;
}

static void SeedPair(CampaignSessionService service, string path)
{
    var campaign = service.CreateNew(980_017, new GalaxyGenerationSettings
    {
        SystemCount = 24, PreWarpCivilizationCount = 4, AncientCivilizationCount = 0, Radius = 320,
    });
    service.Save(path, campaign.Galaxy, campaign.Diplomacy, campaign.AdaptiveResearch, 12.5);
    service.Save(path, campaign.Galaxy, campaign.Diplomacy, campaign.AdaptiveResearch, 18.75);
    NormalizeTimestamp(path); NormalizeTimestamp(path + ".bak");
}

static void NormalizeTimestamp(string path)
{
    var text = File.ReadAllText(path);
    text = Regex.Replace(text, "(\\\"(?:SavedAtUtc|CreatedAtUtc)\\\"\\s*:\\s*\\\")[^\\\"]+(\\\")",
        match => match.Groups[1].Value + "2030-01-02T03:04:05.0000000+00:00" + match.Groups[2].Value);
    File.WriteAllText(path, text, new UTF8Encoding(false));
}

static void NoFiles(CampaignSessionService _, string __) { }

static object FailedRecord(CampaignSessionService service, string path)
{
    var before = SaveHashes(path);
    var updates = new List<CampaignRestorationProgress>();
    try
    {
        _ = service.LoadExisting(path, updates.Add);
        throw new InvalidOperationException("LoadExisting unexpectedly succeeded.");
    }
    catch (Exception error)
    {
        Require(before.SequenceEqual(SaveHashes(path)), "failed load changed files");
        return new { status = "failure", type = error.GetType().FullName, message = NormalizeDiagnostic(error.Message), progress = updates, hashes = before };
    }
}

static object Record(CampaignBootstrapResult loaded, List<CampaignRestorationProgress> updates, string path, string status) => new
{
    status, source = loaded.Source.ToString(), loaded.Seed, loaded.SimulationDays, loaded.GameVersion,
    LoadFailure = NormalizeDiagnostic(loaded.LoadFailure), progress = updates, hashes = SaveHashes(path), stateHash = StateHash(loaded),
};

// Stack frames contain checkout paths, compiler-generated lambda names and
// source line numbers. Preserve the source exception type/message chain while
// excluding these location-dependent details from the deterministic fixture.
static string? NormalizeDiagnostic(string? value) => value is null ? null :
    string.Join("\n", value.Split('\n').Select(line => line.TrimEnd('\r'))
        .Where(line => !string.IsNullOrWhiteSpace(line) &&
                       !line.TrimStart().StartsWith("at ", StringComparison.Ordinal)));

static string[] SaveHashes(string path) => new[] { path, path + ".bak" }.Select(file =>
    File.Exists(file) ? Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file))) : "MISSING").ToArray();

static string StateHash(CampaignBootstrapResult campaign)
{
    var text = new StringBuilder().Append(campaign.Seed.ToString(CultureInfo.InvariantCulture)).Append('|')
        .Append(campaign.SimulationDays.ToString("R", CultureInfo.InvariantCulture)).Append('|');
    foreach (var system in campaign.Galaxy.Systems) text.Append(system.Id).Append(':').Append(system.Name).Append(';');
    foreach (var civilization in campaign.Galaxy.Civilizations) text.Append(civilization.Id).Append(':').Append(civilization.Name).Append(';');
    foreach (var fleet in campaign.Galaxy.Fleets) text.Append(fleet.Id).Append(':').Append(fleet.CivilizationId).Append(':').Append(fleet.CurrentSystemId).Append(';');
    return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(text.ToString())));
}

static void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

sealed class CallbackMarkerException(string message) : Exception(message);
