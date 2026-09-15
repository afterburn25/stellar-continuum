using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using Godot;

namespace Game.Presentation;

/// <summary>Presentation-only asset identities; never reads a civilization or reveals a technology.</summary>
public static class CatalogArtwork
{
    public const string Concealed = "concealed";
    private static readonly Lazy<ArtworkCatalog> Catalog = new(() =>
    {
        using var stream = typeof(CatalogArtwork).Assembly.GetManifestResourceStream("Game.Artwork.Catalog.json")
            ?? throw new InvalidDataException("Artwork catalog is missing.");
        return JsonSerializer.Deserialize<ArtworkCatalog>(stream, new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
            ?? throw new InvalidDataException("Artwork catalog is empty.");
    });
    private static readonly Lazy<IReadOnlyDictionary<string, string>> ResearchIndex = new(() =>
        Catalog.Value.Research.Concat(Catalog.Value.PlannedResearch).ToDictionary(entry => entry.Id, entry => entry.Art, StringComparer.Ordinal));
    private static readonly Lazy<IReadOnlyDictionary<string, string>> ModuleIndex = new(() =>
        Catalog.Value.Modules.ToDictionary(entry => entry.Id, entry => entry.Art, StringComparer.Ordinal));
    private static readonly Lazy<HashSet<string>> KnownArt = new(() =>
        ResearchIndex.Value.Values.Concat(ModuleIndex.Value.Values).ToHashSet(StringComparer.Ordinal));
    // Thumbnail cache is fixed by the shipped art catalog; portraits have a small LRU.
    private static readonly Dictionary<string, Texture2D> Thumbnails = new(StringComparer.Ordinal);
    private static readonly Dictionary<string, Texture2D> Portraits = new(StringComparer.Ordinal);
    private static readonly LinkedList<string> PortraitOrder = new();
    public const int PortraitCacheLimit = 12;
    public static IReadOnlyList<ResearchArtworkEntry> Research => Catalog.Value.Research;
    public static IReadOnlyList<ModuleArtworkEntry> Modules => Catalog.Value.Modules;

    public static string ResearchArt(string? id, bool revealed) => !revealed || id is null
        ? Concealed : ResearchIndex.Value.GetValueOrDefault(id, Concealed);
    public static string ModuleArt(string? id, bool revealed = true) => !revealed || id is null
        ? Concealed : ModuleIndex.Value.GetValueOrDefault(id, Concealed);
    public static string AssetPath(string art, bool portrait = false) => art == Concealed
        ? "res://assets/visual/icons/research/icon_research_locked.svg"
        : $"res://assets/visual/catalog/{(portrait ? "portraits" : "thumbnails")}/{art}.png";

    public static Texture2D Texture(string art, bool portrait = false)
    {
        // Only known shipped identities may enter caches. Foreign future content gets the
        // concealed fallback until it registers an illustration in the asset catalog.
        if (art != Concealed && !KnownArt.Value.Contains(art))
            art = Concealed;
        var cache = portrait ? Portraits : Thumbnails;
        if (cache.TryGetValue(art, out var cached))
        {
            if (portrait) { PortraitOrder.Remove(art); PortraitOrder.AddLast(art); }
            return cached;
        }
        var path = AssetPath(art, portrait);
        var texture = ResourceLoader.Exists(path) ? GD.Load<Texture2D>(path) : VisualIconLibrary.ResearchLocked;
        if (portrait && cache.Count >= PortraitCacheLimit)
        {
            cache.Remove(PortraitOrder.First!.Value);
            PortraitOrder.RemoveFirst();
        }
        cache.Add(art, texture);
        if (portrait) PortraitOrder.AddLast(art);
        return texture;
    }

    public static TextureRect Thumbnail(string art, string name, float size = 76) => new()
    {
        Name = name, Texture = Texture(art), CustomMinimumSize = new(size, size),
        ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
        StretchMode = TextureRect.StretchModeEnum.KeepAspectCentered,
        MouseFilter = Control.MouseFilterEnum.Ignore,
    };

    public sealed class ArtworkCatalog
    {
        public ResearchArtworkEntry[] Research { get; set; } = Array.Empty<ResearchArtworkEntry>();
        public ResearchArtworkEntry[] PlannedResearch { get; set; } = Array.Empty<ResearchArtworkEntry>();
        public ModuleArtworkEntry[] Modules { get; set; } = Array.Empty<ModuleArtworkEntry>();
    }
    public sealed class ResearchArtworkEntry
    {
        public string Id { get; set; } = "";
        public string Name { get; set; } = "";
        public string Domain { get; set; } = "";
        public int Depth { get; set; }
        public string Art { get; set; } = "";
    }
    public sealed class ModuleArtworkEntry
    {
        public string Id { get; set; } = "";
        public string Name { get; set; } = "";
        public string Race { get; set; } = "";
        public string Family { get; set; } = "";
        public int Level { get; set; }
        public string Art { get; set; } = "";
        public string Status { get; set; } = "";
        public string Description { get; set; } = "";
    }
}
