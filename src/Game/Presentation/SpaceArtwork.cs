using Godot;
using System;

namespace Game.Presentation;

/// <summary>Original decorative art; it never creates systems or changes navigation coordinates.</summary>
public static class SpaceArtwork
{
    private static Texture2D? _nebula;
    private static Texture2D? _deepField;
    public static Texture2D DeepField => _deepField ??= GD.Load<Texture2D>("res://assets/visual/space/deep-field-v2.png");

    public static void DrawDeepField(CanvasItem canvas, Vector2 size, float opacity = .70f) =>
        canvas.DrawTextureRect(DeepField, new Rect2(Vector2.Zero, size), false, new Color(1,1,1,opacity));
    public static Texture2D Nebula => _nebula ??= GD.Load<Texture2D>("res://assets/visual/space/regional-nebula-b.png");

    public static void DrawNebula(CanvasItem canvas, Vector2 size, Vector2 pan, float opacity = .70f)
    {
        var texture = Nebula;
        var scale = Mathf.Max(size.X / texture.GetWidth(), size.Y / texture.GetHeight()) * 1.12f;
        var extent = texture.GetSize() * scale;
        var drift = new Vector2(Mathf.Sin(pan.X * .0007f), Mathf.Sin(pan.Y * .0007f)) * 18;
        canvas.DrawTextureRect(texture, new Rect2((size - extent) * .5f + drift, extent), false, new Color(1, 1, 1, opacity));
    }

    /// <summary>
    /// Draws galactic dust through a shader evaluated at the current native pixel size.
    /// It is cosmetic only: the world frame is shared with the catalogue, but no generated
    /// point here represents a selectable system.
    /// </summary>
    public static void DrawGalaxyOverview(CanvasItem canvas, Rect2 frame, long seed, float opacity,
        bool spiral = true, float prominence = 1.0f, float reservedCoreRadius = 0f)
    {
        if (opacity <= .002f || frame.Size.X <= 1 || frame.Size.Y <= 1)
            return;

        var texture = GalaxyCloudRenderer.Render(canvas, frame, seed, spiral, prominence, reservedCoreRadius);
        if (texture is not null)
            canvas.DrawTextureRect(texture, frame, false, new Color(1, 1, 1, opacity));
    }
}
