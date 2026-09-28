using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text.Json;
using System.Text.RegularExpressions;
using Game.Simulation.Models;

namespace Game.Simulation.Generation;

/// <summary>Versioned offline stellar facts. Planets and civilizations remain seeded game content.</summary>
public static class NearbyStarCatalog
{
    public const int SystemCount = 500;
    private static readonly Lazy<IReadOnlyList<NearbyCatalogStar>> Catalog = new(Load);
    public static IReadOnlyList<NearbyCatalogStar> Stars => Catalog.Value;

    public static IReadOnlyList<NearbyCatalogStar> NearestClassified(int count)
    {
        if (count < 1 || count > SystemCount) throw new ArgumentOutOfRangeException(nameof(count));
        var result = Stars.Where(star => Classify(star.SpectralType).HasValue)
            .OrderBy(star => star.DistanceParsecs).ThenBy(star => star.HygId).Take(count).ToArray();
        if (result.Length != count)
            throw new InvalidDataException($"The nearby-star catalogue contains fewer than {count} classified systems.");
        return Array.AsReadOnly(result);
    }

    private static IReadOnlyList<NearbyCatalogStar> Load()
    {
        using var stream = typeof(NearbyStarCatalog).Assembly.GetManifestResourceStream("Game.Astronomy.Nearby500.json")
            ?? throw new InvalidDataException("The bundled nearby-star catalogue is missing: Game.Astronomy.Nearby500.json.");
        var document = JsonSerializer.Deserialize<CatalogDocument>(stream,
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
        if (document?.CatalogVersion != GalaxyGenerationMetadata.CatalogGeneratorVersion ||
            document.Systems.Length != SystemCount || document.Systems[0].HygId != 0 ||
            document.Systems.Select(star => star.HygId).Distinct().Count() != SystemCount ||
            document.Systems.Select(star => star.Name).Distinct(StringComparer.OrdinalIgnoreCase).Count() != SystemCount)
            throw new InvalidDataException("The bundled nearby-star catalogue has an invalid version, count or identity.");
        foreach (var star in document.Systems)
        {
            if (string.IsNullOrWhiteSpace(star.Name) || !double.IsFinite(star.XLightYears) ||
                !double.IsFinite(star.YLightYears) || !double.IsFinite(star.ZLightYears) ||
                !double.IsFinite(star.DistanceParsecs) || star.DistanceParsecs < 0 || star.Components.Length == 0)
                throw new InvalidDataException($"Invalid HYG catalogue entry {star.HygId} ({star.Name}).");
        }
        return Array.AsReadOnly(document.Systems);
    }

    public static StarSystemState Apply(StarSystemState system, NearbyCatalogStar star) => system with
    {
        Name = star.Name,
        Position = star.HygId == 0 ? Vector2.Zero : new((float)star.XLightYears, (float)star.YLightYears),
        GalacticDepthLightYears = star.HygId == 0 ? 0 : star.ZLightYears,
        StellarCatalogId = $"hyg-v41:{star.HygId}",
        StellarClass = Classify(star.SpectralType),
        SecondaryStellarClass = star.Components.Length > 1 && Classify(star.SpectralType).HasValue
            ? Classify(star.Components[1].SpectralType) : null,
        TertiaryStellarClass = star.Components.Length > 2 && Classify(star.SpectralType).HasValue &&
            Classify(star.Components[1].SpectralType).HasValue ? Classify(star.Components[2].SpectralType) : null,
    };

    // The game's palette has fewer bins than the astronomical spectral sequence. A source
    // type is preserved verbatim in the catalogue; this conversion selects its visual family.
    public static StellarPrimaryClass? Classify(string spectralType)
    {
        var type = spectralType.Trim();
        if (type.Length == 0) return null;
        if (type.StartsWith('D')) return StellarPrimaryClass.WhiteDwarf;
        if (Regex.IsMatch(type, @"I{1,3}(?!V)")) return StellarPrimaryClass.Giant;
        var match = Regex.Match(type, @"[OBAFGKMLTY]");
        return match.Value switch
        {
            "O" or "B" => StellarPrimaryClass.HotBlueStar,
            "A" => StellarPrimaryClass.AWhiteStar,
            "F" => StellarPrimaryClass.FYellowWhiteDwarf,
            "G" => StellarPrimaryClass.GYellowDwarf,
            "K" => StellarPrimaryClass.KOrangeDwarf,
            "M" or "L" or "T" or "Y" => StellarPrimaryClass.MRedDwarf,
            _ => null,
        };
    }

    private sealed record CatalogDocument(string CatalogVersion, NearbyCatalogStar[] Systems);
}

public sealed record NearbyCatalogStar(int HygId, string Name, string NameKind, double DistanceParsecs,
    double XLightYears, double YLightYears, double ZLightYears, string SpectralType, NearbyCatalogComponent[] Components);
public sealed record NearbyCatalogComponent(int HygId, string Name, string SpectralType);
