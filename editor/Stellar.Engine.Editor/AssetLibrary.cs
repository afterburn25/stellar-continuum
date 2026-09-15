using System.IO;
using System.Security.Cryptography;
using System.Text.Json;

namespace Stellar.Editor;
public sealed class LibraryAsset
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Category { get; set; } = "";
    public long Size { get; set; }
    public string RelativePath { get; set; } = "";
    public string Description { get; set; } = "";
    public override string ToString() => $"{Name}   ·   {Category}";
}
public sealed class AssetLibrary
{
    public string Root { get; }
    public List<LibraryAsset> Assets { get; private set; } = [];
    public AssetLibrary(string? root = null)
    {
        Root = Path.GetFullPath(root ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "Stellar Engine", "Assets"));
        Directory.CreateDirectory(Root);
        var path = Path.Combine(Root, "library.json");
        if (File.Exists(path))
        {
            Assets = JsonSerializer.Deserialize<List<LibraryAsset>>(File.ReadAllText(path)) ?? throw new InvalidDataException("The asset library index is empty.");
            foreach (var asset in Assets) _ = Resolve(asset);
        }
    }
    public string Resolve(LibraryAsset asset)
    {
        if (Path.IsPathRooted(asset.RelativePath)) throw new InvalidDataException("Invalid library asset path.");
        var path = Path.GetFullPath(Path.Combine(Root, asset.RelativePath));
        if (!path.StartsWith(Root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Invalid library asset path.");
        return path;
    }
    public LibraryAsset Import(string source, string description = "")
    {
        var info = new FileInfo(source);
        if (!info.Exists || info.Length > 25 * 1024 * 1024) throw new InvalidDataException("Import a file up to 25 MB for this editor preview.");
        return ImportBytes(info.Name, File.ReadAllBytes(source), description);
    }
    public LibraryAsset ImportBytes(string name, byte[] bytes, string description = "")
    {
        if (string.IsNullOrWhiteSpace(name) || name != Path.GetFileName(name) || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || bytes.Length > 25 * 1024 * 1024) throw new InvalidDataException("Invalid asset filename or size.");
        var extension = Path.GetExtension(name).ToLowerInvariant();
        var category = extension switch
        {
            ".wav" or ".mp3" or ".ogg" or ".flac" => "Audio",
            ".png" or ".jpg" or ".jpeg" or ".svg" or ".webp" => "Images",
            ".glb" or ".gltf" or ".obj" => "Models",
            ".json" or ".csv" or ".txt" or ".md" => "Data",
            _ => throw new InvalidDataException("Supported assets: audio, images, GLB/GLTF/OBJ models, JSON, CSV, and text.")
        };
        var hash = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
        var existing = Assets.FirstOrDefault(a => a.Id == hash && a.Name == name);
        if (existing is not null && File.Exists(Resolve(existing))) return existing;
        var asset = new LibraryAsset { Id = hash, Name = name, Category = category, Size = bytes.Length, RelativePath = Path.Combine(category, hash, name), Description = description };
        var destination = Resolve(asset);
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        if (!File.Exists(destination)) File.WriteAllBytes(destination, bytes);
        else if (!SHA256.HashData(File.ReadAllBytes(destination)).SequenceEqual(SHA256.HashData(bytes))) throw new IOException("A stored asset has changed. The import has been stopped to preserve it.");
        if (existing is null) Assets.Add(asset);
        WorldProject.AtomicWrite(Path.Combine(Root, "library.json"), JsonSerializer.Serialize(Assets, WorldProject.JsonOptions));
        return existing ?? asset;
    }
    public ProjectAsset CopyToProject(LibraryAsset asset)
    {
        var bytes = File.ReadAllBytes(Resolve(asset));
        if (!string.Equals(Convert.ToHexString(SHA256.HashData(bytes)), asset.Id, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("This asset changed outside the editor. Import it again before adding it to a project.");
        return new() { Name = asset.Name, Bytes = bytes };
    }
}
