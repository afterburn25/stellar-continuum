using System;
using System.Collections.Generic;
using Godot;

namespace Game.Presentation.Spatial;

/// <summary>Loads attributed original imagery and describes its documented projection.</summary>
public static class SolBodyMaterials
{
    private static readonly Dictionary<string, Texture2D> TextureCache = new(StringComparer.Ordinal);

    public static bool IsCanonicalKey(string? key) =>
        key is "mercury" or "venus" or "earth" or "mars" or "jupiter" or "saturn" or "uranus" or "neptune" or "moon";

    public static Texture2D? LoadColorTexture(string? key)
    {
        if (!IsCanonicalKey(key)) return null;
        if (TextureCache.TryGetValue(key!, out var cached)) return cached;
        foreach (var extension in new[] { ".jpg", ".png", ".webp" })
        {
            var path = $"res://assets/visual/sol/{key}{extension}";
            if (!ResourceLoader.Exists(path)) continue;
            var texture = GD.Load<Texture2D>(path)
                ?? throw new InvalidOperationException($"Planetary appearance asset failed to load: {path}");
            TextureCache.Add(key!, texture);
            return texture;
        }
        if (key == "moon") return null;
        throw new InvalidOperationException($"Missing Sol planetary appearance asset for {key} in res://assets/visual/sol/.");
    }

    /// <summary>xy is the disc center; zw are its horizontal/vertical radii. Zero means equirectangular.</summary>
    public static Vector4 GetSourceDisc(string? key) => key switch
    {
        "earth" => new Vector4(0.5280f, 0.5836f, 0.1851f, 0.1968f),
        "mercury" => new Vector4(0.5f, 0.5f, 0.454f, 0.454f),
        "uranus" => new Vector4(0.25f, 0.501f, 0.176f, 0.352f),
        "moon" => new Vector4(0.529f, 0.503f, 0.398f, 0.398f),
        "venus" => new Vector4(0.500f, 0.500f, 0.414f, 0.426f),
        _ => Vector4.Zero,
    };

    // Compatibility for existing orbital thumbnails. Focused planets use the cached
    // original Texture2D directly on the GPU and never make a 256-pixel intermediate.
    public static Image? LoadColorSource(string? key)
    {
        var texture = LoadColorTexture(key);
        if (texture is null) return null;
        var source = texture.GetImage()
            ?? throw new InvalidOperationException($"Planetary appearance asset has no image: {key}");
        if (source.IsCompressed() && source.Decompress() != Error.Ok)
            throw new InvalidOperationException($"Planetary appearance asset could not decompress: {key}");
        return source;
    }

    public static Color Sample(Image source, string key, float nx, float ny, float nz)
    {
        var disc = GetSourceDisc(key);
        var u = disc.Z > 0 ? disc.X + nx * disc.Z : 0.5f + MathF.Atan2(nx, nz) / MathF.Tau;
        var v = disc.W > 0 ? disc.Y + ny * disc.W : 0.5f + MathF.Asin(Math.Clamp(ny, -1, 1)) / MathF.PI;
        var x = Math.Clamp(u * (source.GetWidth() - 1), 0, source.GetWidth() - 1);
        var y = Math.Clamp(v * (source.GetHeight() - 1), 0, source.GetHeight() - 1);
        var x0 = (int)x;
        var y0 = (int)y;
        var x1 = Math.Min(x0 + 1, source.GetWidth() - 1);
        var y1 = Math.Min(y0 + 1, source.GetHeight() - 1);
        return source.GetPixel(x0, y0).Lerp(source.GetPixel(x1, y0), x - x0)
            .Lerp(source.GetPixel(x0, y1).Lerp(source.GetPixel(x1, y1), x - x0), y - y0);
    }
}
