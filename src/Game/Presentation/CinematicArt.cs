using System;
using Godot;
using System.Collections.Generic;

namespace Game.Presentation;

/// <summary>Resolution-independent light and restrained metal framing for the game UI.</summary>
public static class CinematicArt
{
    private static readonly Dictionary<string, Texture2D> Frames = new();
    private static Texture2D? _glow;
    public static Texture2D Glow => _glow ??= RadialLightTexture.Create(256, RadialLightProfile.Glow);

    public static void DrawStarlight(CanvasItem canvas, Vector2 at, float radius, Color color, float opacity = 1)
    {
        var extent = radius * 7;
        canvas.DrawTextureRect(Glow, new(at - Vector2.One * extent, Vector2.One * extent * 2), false,
            new Color(color.R, color.G, color.B, opacity));
        canvas.DrawCircle(at, radius * .34f, new Color(1,.97f,.91f,opacity), true, -1, true);
        canvas.DrawLine(at - new Vector2(radius * 2.8f,0), at + new Vector2(radius * 2.8f,0),
            new Color(color.R,color.G,color.B,opacity * .22f), .65f, true);
    }

    public static StyleBoxTexture Frame(string name = "panel", int margin = 14) => new()
    {
        Texture = FrameTexture(name),
        TextureMarginLeft = 18, TextureMarginRight = 18, TextureMarginTop = 18, TextureMarginBottom = 18,
        ContentMarginLeft = margin, ContentMarginRight = margin,
        ContentMarginTop = margin, ContentMarginBottom = margin
    };

    private static Texture2D FrameTexture(string name)
    {
        if (Frames.TryGetValue(name, out var texture)) return texture;
        // Nine-slice coordinates are authored in pixels. The project's 4x icon
        // importer must not magnify these margins or turn borders into brackets.
        var path = $"res://assets/visual/ui/{name}-frame.svg";
        using var source = GD.Load<Texture2D>(path).GetImage();
        source.Resize(128, 128, Image.Interpolation.Lanczos);
        texture = ImageTexture.CreateFromImage(source);
        Frames.Add(name, texture);
        return texture;
    }
}

/// <summary>Shared sampled profiles for cached radial light textures. Every profile becomes
/// fully transparent well inside the bitmap. Smooth profiles use bilinear filtering without
/// mipmaps: thin diffraction rays otherwise select an opaque averaged 1x1 mip.</summary>
public enum RadialLightProfile
{
    Glow,
    Bloom,
    Core,
}

public static class RadialLightTexture
{
    public const float TransparentEdgeStart = .90f;
    private static readonly (float Radius, float Alpha)[] GlowStops =
        [(0f, 1f), (.05f, .94f), (.14f, .44f), (.32f, .13f), (.62f, .035f), (TransparentEdgeStart, 0f)];
    private static readonly (float Radius, float Alpha)[] BloomStops =
        [(0f, 1f), (.14f, .90f), (.34f, .50f), (.62f, .12f), (TransparentEdgeStart, 0f)];
    private static readonly (float Radius, float Alpha)[] CoreStops =
        [(0f, 1f), (.46f, 1f), (.68f, .92f), (TransparentEdgeStart, 0f)];

    public static float AlphaAt(RadialLightProfile profile, float normalizedRadius)
    {
        var radius = Math.Max(0, normalizedRadius);
        return profile switch
        {
            RadialLightProfile.Glow => ThroughStops(radius, GlowStops),
            RadialLightProfile.Bloom => ThroughStops(radius, BloomStops),
            _ => ThroughStops(radius, CoreStops),
        };
    }

    public static Texture2D Create(int size, RadialLightProfile profile)
    {
        if (size < 8) throw new ArgumentOutOfRangeException(nameof(size));
        var pixels = new byte[checked(size * size * 4)];
        var center = size * .5f;
        for (var y = 0; y < size; y++)
        for (var x = 0; x < size; x++)
        {
            var dx = (x + .5f - center) / center;
            var dy = (y + .5f - center) / center;
            var offset = (y * size + x) * 4;
            pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = byte.MaxValue;
            pixels[offset + 3] = (byte)Math.Clamp(
                MathF.Round(AlphaAt(profile, MathF.Sqrt(dx * dx + dy * dy)) * byte.MaxValue), 0, byte.MaxValue);
        }
        using var image = Image.CreateFromData(size, size, false, Image.Format.Rgba8, pixels);
        // A 60x2 diffraction ray samples the coarsest mip in both axes. Averaging this
        // radial texture down to 1x1 gives the entire quad nonzero alpha, visibly boxing
        // in the star. These smooth cached gradients need no minification detail chain.
        return ImageTexture.CreateFromImage(image);
    }

    private static float ThroughStops(float radius, (float Radius, float Alpha)[] stops)
    {
        if (radius <= stops[0].Radius) return stops[0].Alpha;
        for (var index = 1; index < stops.Length; index++)
        {
            var next = stops[index];
            if (radius > next.Radius) continue;
            var previous = stops[index - 1];
            var fraction = (radius - previous.Radius) / (next.Radius - previous.Radius);
            fraction = fraction * fraction * (3 - 2 * fraction);
            return previous.Alpha + (next.Alpha - previous.Alpha) * fraction;
        }
        return 0;
    }
}
