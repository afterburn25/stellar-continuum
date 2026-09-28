using System;

namespace Game.Presentation.Spatial;

/// <summary>Pure presentation geometry for the fixed galaxy artwork and focus transition.</summary>
public static class SpatialNavigationLayout
{
    // The 100-system campaign occupies this whole four-arm galaxy presentation. Keeping these
    // values together makes future size profiles adjustable without scattering camera constants.
    public const float GalaxyWorldWidth = 2200;
    public const float GalaxyWorldHeight = 2200;
    public const float GalaxyWorldCenterX = -324;
    public const float GalaxyWorldCenterY = -129.6f;
    public const float OverviewBlendFullScale = .52f;
    public const float OverviewBlendEndScale = .78f;
    public const float StellarRegionScale = .90f;
    public static (float Left, float Top, float Width, float Height) GalaxyWorldFrame =>
        (GalaxyWorldCenterX - GalaxyWorldWidth * .5f,
            GalaxyWorldCenterY - GalaxyWorldHeight * .5f,
            GalaxyWorldWidth, GalaxyWorldHeight);

    public static float GalaxyOverviewBlend(float scale) => Math.Clamp(
        (OverviewBlendEndScale - scale) / (OverviewBlendEndScale - OverviewBlendFullScale), 0, 1);

    public static float PopulationOverviewBlend(float scale, float fittedScale)
    {
        // Compact maps and high-resolution windows must still become local space at Home.
        var end = Math.Min(fittedScale * 5f, OverviewBlendEndScale);
        var start = Math.Min(fittedScale * 1.2f, end * .8f);
        var progress = Math.Clamp((scale - start) / Math.Max(.0000001f, end - start), 0f, 1f);
        return 1f - progress * progress * (3f - 2f * progress);
    }

    public static SystemSpatialViewport FitGalaxyOverview(float width, float height)
    {
        const float top = 112, bottom = 128, left = 112, right = 16;
        var centerX = width * 0.5f + 34;
        var centerY = (top + height - bottom) * 0.5f;
        var availableWidth = Math.Max(1, 2 * Math.Min(centerX - left, width - right - centerX));
        var availableHeight = Math.Max(1, height - top - bottom);
        var scale = Math.Max(0.001f, Math.Min(OverviewBlendFullScale,
            Math.Min(availableWidth / GalaxyWorldWidth, availableHeight / GalaxyWorldHeight)));
        // Camera origin keeps the galactic center in the usable viewport while Sol retains its
        // actual generator offset inside the four-arm campaign galaxy.
        return new(centerX - GalaxyWorldCenterX * scale,
            centerY - GalaxyWorldCenterY * scale, scale);
    }

    public static float OrbitalContextOpacity(float currentRadius, float orbitalRadius, float focusRadius)
    {
        var progress = Math.Clamp((currentRadius - orbitalRadius) / Math.Max(1, focusRadius - orbitalRadius), 0, 1);
        var fade = Math.Clamp((progress - 0.15f) / 0.65f, 0, 1);
        return 1 - fade * fade * (3 - 2 * fade);
    }
}
