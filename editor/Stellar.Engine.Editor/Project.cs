using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Stellar.Editor;

public sealed class WorldProject
{
    public int SchemaVersion { get; set; } = 1;
    public string Format { get; set; } = "stellar-engine-editor-project";
    public string Name { get; set; } = "Stellar Continuum — First Light";
    public long Seed { get; set; } = 8374837;
    public int SystemCount { get; set; } = 500;
    public string Notes { get; set; } = "A Stellar Continuum world project. Generate a galaxy, inspect its systems, and bookmark places to develop.";
    public string EngineVersion { get; set; } = "0.1.9";
    public string EngineCommit { get; set; } = "e80e87f90563801166aeb68d8d8b8b9ec6ce79f3";
    public JsonObject? Catalog { get; set; }
    public Dictionary<int, Annotation> Annotations { get; set; } = [];
    public List<ProjectAsset> Assets { get; set; } = [];
    public static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    public WorldProject Copy() => new()
    {
        SchemaVersion = SchemaVersion, Format = Format, Name = Name, Seed = Seed, SystemCount = SystemCount,
        Notes = Notes, EngineVersion = EngineVersion, EngineCommit = EngineCommit, Catalog = Catalog?.DeepClone().AsObject(),
        Annotations = Annotations.ToDictionary(p => p.Key, p => new Annotation { DisplayName = p.Value.DisplayName, Notes = p.Value.Notes, Bookmarked = p.Value.Bookmarked }),
        // Asset payloads are immutable; undo history shares bytes instead of multiplying memory use.
        Assets = Assets.Select(a => new ProjectAsset { Name = a.Name, Bytes = a.Bytes }).ToList()
    };
    public static WorldProject Load(string path)
    {
        var info = new FileInfo(path);
        if (info.Length > 256 * 1024 * 1024) throw new InvalidDataException("This project exceeds the 256 MB editor preview limit.");
        var project = JsonSerializer.Deserialize<WorldProject>(File.ReadAllText(path), JsonOptions) ?? throw new InvalidDataException("The project is empty.");
        project.Validate();
        return project;
    }
    public void Validate()
    {
        if (Format != "stellar-engine-editor-project" || SchemaVersion != 1) throw new InvalidDataException("This file is not a supported Stellar Engine Editor project. Game saves use a different format.");
        if (string.IsNullOrWhiteSpace(Name) || Name.Length > 120 || Notes is null || Notes.Length > 20000 || Annotations is null || Assets is null) throw new InvalidDataException("The project has invalid details.");
        if (!new[] { 250, 500, 1000, 2500 }.Contains(SystemCount)) throw new InvalidDataException("Choose 250, 500, 1,000 or 2,500 systems.");
        foreach (var asset in Assets)
            if (asset is null || string.IsNullOrWhiteSpace(asset.Name) || asset.Name != Path.GetFileName(asset.Name) || asset.Bytes is null || asset.Bytes.Length > 25 * 1024 * 1024) throw new InvalidDataException("The project contains an invalid asset.");
        if (Assets.Sum(a => (long)a.Bytes.Length) > 100 * 1024 * 1024) throw new InvalidDataException("Preview projects support up to 100 MB of imported assets.");
        foreach (var row in Annotations)
            if (row.Key < 0 || row.Value is null || row.Value.DisplayName is null || row.Value.DisplayName.Length > 120 || row.Value.Notes is null || row.Value.Notes.Length > 10000) throw new InvalidDataException("The project contains invalid annotations.");
        if (Catalog is null) return;
        if (Catalog["phase"]?.GetValue<string>() != "colonies-before-fleets") throw new InvalidDataException("This catalog is not a supported colony preview.");
        var systems = Catalog["systems"] as JsonArray ?? throw new InvalidDataException("Missing world systems.");
        if (systems.Count != SystemCount || Catalog["count"]?.GetValue<int>() != SystemCount || Catalog["seed"]?.GetValue<long>() != Seed) throw new InvalidDataException("Project settings do not match the generated world.");
        static bool OptionalCoordinate(JsonNode? node) => node is null || node is JsonValue value && value.TryGetValue<double>(out var number) && double.IsFinite(number);
        if (Catalog["core"] is JsonNode core && (core is not JsonObject || !OptionalCoordinate(core["x"]) || !OptionalCoordinate(core["y"]))) throw new InvalidDataException("Invalid galaxy center.");
        if (!OptionalCoordinate(Catalog["radiusLightYears"]) || Catalog["radiusLightYears"] is JsonNode radius && radius.GetValue<double>() <= 0) throw new InvalidDataException("Invalid galaxy radius.");
        var ids = new HashSet<int>();
        foreach (var item in systems)
        {
            var s = item as JsonObject ?? throw new InvalidDataException("Invalid system record.");
            if (!ids.Add(s["id"]!.GetValue<int>()) || string.IsNullOrWhiteSpace(s["name"]?.GetValue<string>()) || !double.IsFinite(s["xLightYears"]!.GetValue<double>()) || !double.IsFinite(s["yLightYears"]!.GetValue<double>())) throw new InvalidDataException("Invalid or duplicate star system.");
            if (!OptionalCoordinate(s["depthLightYears"])) throw new InvalidDataException("Invalid star system depth.");
        }
        if (Catalog["planetaryBodies"] is not JsonArray || Catalog["civilizations"] is not JsonArray || Catalog["colonies"] is not JsonArray) throw new InvalidDataException("The generated world is incomplete.");
    }
    public void Save(string path)
    {
        Validate();
        AtomicWrite(path, JsonSerializer.Serialize(this, JsonOptions));
    }
    public static void AtomicWrite(string path, string content)
    {
        path = Path.GetFullPath(path);
        var temporary = path + "." + Guid.NewGuid().ToString("N") + ".pending";
        try
        {
            using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            using (var writer = new StreamWriter(stream)) { writer.Write(content); writer.Flush(); stream.Flush(true); }
            if (File.Exists(path)) File.Replace(temporary, path, path + ".bak", true);
            else File.Move(temporary, path);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
}
public sealed class Annotation { public string DisplayName { get; set; } = ""; public string Notes { get; set; } = ""; public bool Bookmarked { get; set; } }
public sealed class ProjectAsset { public string Name { get; set; } = ""; public byte[] Bytes { get; set; } = []; }
public sealed record WorldItem(string Kind, int Id, int SystemId, string Name, JsonObject Data)
{
    public string Key => $"{Kind}:{Id}";
    public override string ToString() => Name;
}
public static class WorldData
{
    public static List<WorldItem> Items(WorldProject project)
    {
        var result = new List<WorldItem>();
        foreach (var (array, kind) in new[] { ("systems", "System"), ("planetaryBodies", "Body"), ("civilizations", "Civilization"), ("colonies", "Colony") })
            foreach (var row in project.Catalog?[array]?.AsArray() ?? [])
            {
                var data = row!.AsObject(); var id = data["id"]!.GetValue<int>();
                var system = kind == "System" ? id : data[kind == "Civilization" ? "homeSystemId" : "systemId"]!.GetValue<int>();
                var name = data["name"]!.GetValue<string>();
                if (kind == "System" && project.Annotations.TryGetValue(id, out var a) && !string.IsNullOrWhiteSpace(a.DisplayName)) name = a.DisplayName;
                result.Add(new(kind, id, system, name, data));
            }
        return result;
    }
}
public static class EngineRunner
{
    public static async Task<(JsonObject Catalog, JsonObject Receipt)> Generate(long seed, int count, CancellationToken cancellation = default)
    {
        var executable = Path.Combine(AppContext.BaseDirectory, "Engine", "stellar-continuum.exe");
        if (!File.Exists(executable)) throw new FileNotFoundException("The engine is missing. Keep the Engine folder beside StellarEngineEditor.exe.");
        var temporary = Path.Combine(Path.GetTempPath(), "StellarEditor-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(temporary);
        try
        {
            var output = Path.Combine(temporary, "catalog.json");
            var start = new ProcessStartInfo(executable) { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true, WorkingDirectory = Path.GetDirectoryName(executable)! };
            foreach (var arg in new[] { "--headless", "--generate-galaxy", "--seed-colonies", "--systems", count.ToString(CultureInfo.InvariantCulture), "--seed", seed.ToString(CultureInfo.InvariantCulture), "--catalog-output", output }) start.ArgumentList.Add(arg);
            using var process = Process.Start(start) ?? throw new IOException("The engine could not start.");
            var stdout = process.StandardOutput.ReadToEndAsync(); var stderr = process.StandardError.ReadToEndAsync();
            using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellation);
            deadline.CancelAfter(TimeSpan.FromMinutes(2));
            try { await process.WaitForExitAsync(deadline.Token); }
            catch (OperationCanceledException) { if (!process.HasExited) process.Kill(true); await process.WaitForExitAsync(); throw new IOException(cancellation.IsCancellationRequested ? "Generation cancelled. The previous world has been kept." : "The engine took too long. The previous world has been kept."); }
            var errors = await stderr; var receipt = await stdout;
            if (process.ExitCode != 0) throw new IOException("World generation failed: " + errors.Trim());
            var catalog = JsonNode.Parse(await File.ReadAllTextAsync(output, cancellation))!.AsObject();
            return (catalog, JsonNode.Parse(receipt)!.AsObject());
        }
        finally { Directory.Delete(temporary, true); }
    }
}
