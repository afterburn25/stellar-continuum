using Godot;

namespace Game.Presentation;

/// <summary>
/// Runtime mirror of the authoritative color roles in assets/visual/ui/visual_tokens.json.
/// Keep this compact palette centralized so direct-drawn presentation does not scatter
/// subtly different color literals across map/menu renderers.
/// </summary>
public static class VisualPalette
{
    public static readonly Color Canvas = Rgb(0x05, 0x0B, 0x12);
    public static readonly Color SurfacePrimary = Rgb(0x07, 0x13, 0x1F);
    public static readonly Color SurfaceSecondary = Rgb(0x0D, 0x1D, 0x2B);
    public static readonly Color SurfaceRaised = Rgb(0x13, 0x28, 0x3A);
    public static readonly Color Keyline = Rgb(0x27, 0x43, 0x59);
    public static readonly Color TextPrimary = Rgb(0xE6, 0xF0, 0xF6);
    public static readonly Color TextSecondary = Rgb(0xA9, 0xBB, 0xC8);
    public static readonly Color TextMuted = Rgb(0x6F, 0x84, 0x94);
    public static readonly Color Selected = Rgb(0x58, 0xCF, 0xFB);
    public static readonly Color Focus = Rgb(0x93, 0xE2, 0xFF);
    public static readonly Color Success = Rgb(0x64, 0xD6, 0xA5);
    public static readonly Color Caution = Rgb(0xEB, 0xCB, 0x67);
    public static readonly Color Danger = Rgb(0xFF, 0x71, 0x6C);
    public static readonly Color Unknown = Rgb(0x8B, 0x82, 0xA2);
    public static readonly Color Disabled = Rgb(0x52, 0x65, 0x74);
    public static readonly Color Exploration = Rgb(0x58, 0xCF, 0xFB);
    public static readonly Color Science = Rgb(0xAF, 0x8F, 0xFF);
    public static readonly Color Economy = Rgb(0xE9, 0xB6, 0x5C);
    public static readonly Color Construction = Rgb(0xF1, 0x97, 0x5B);
    public static readonly Color Diplomacy = Rgb(0x5F, 0xD2, 0xC0);
    public static readonly Color Military = Rgb(0xFF, 0x77, 0x6E);

    public static Color WithAlpha(Color color, float alpha) =>
        new(color.R, color.G, color.B, alpha);

    private static Color Rgb(int red, int green, int blue) =>
        new(red / 255.0f, green / 255.0f, blue / 255.0f, 1.0f);
}
