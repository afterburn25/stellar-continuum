using Game.Simulation.Generation;
using Godot;

namespace Game.Presentation;

public partial class Main
{
    private static Texture2D? _undisclosedCoreFog;

    private void DrawUndisclosedCoreFog(Vector2 center, float radius)
    {
        if (radius < 1) return;
        var extent = radius * 1.45f;
        if (!new Rect2(center - Vector2.One * extent, Vector2.One * extent * 2)
                .Intersects(GetViewportRect())) return;
        // Only diffuse cloud is public. No label, black-hole silhouette, destination or
        // discovery state is exposed by this decorative, non-interactive layer.
        _undisclosedCoreFog ??= CreateUndisclosedCoreFog();
        DrawTextureRect(_undisclosedCoreFog,
            new(center - Vector2.One * extent, Vector2.One * extent * 2), false);
    }

    private static Texture2D CreateUndisclosedCoreFog()
    {
        const int size = 384;
        using var noise = new FastNoiseLite
        {
            NoiseType = FastNoiseLite.NoiseTypeEnum.SimplexSmooth,
            Seed = 41729, Frequency = .024f,
            FractalType = FastNoiseLite.FractalTypeEnum.Fbm, FractalOctaves = 4,
        };
        var pixels = new byte[size * size * 4];
        for (var y = 0; y < size; y++)
        for (var x = 0; x < size; x++)
        {
            var dx = (x + .5f - size * .5f) / (size * .5f);
            var dy = (y + .5f - size * .5f) / (size * .5f);
            var distance = Mathf.Sqrt(dx * dx + dy * dy);
            var cloud = noise.GetNoise2D(x, y) * .5f + .5f;
            var edge = 1f - Mathf.SmoothStep(.53f, .91f, distance + (cloud - .5f) * .08f);
            var light = .49f + .35f * cloud + .14f * (1f - Mathf.SmoothStep(0f, .6f, distance));
            var i = (y * size + x) * 4;
            pixels[i] = (byte)(light * 230);
            pixels[i + 1] = (byte)(light * 240);
            pixels[i + 2] = (byte)(light * 255);
            pixels[i + 3] = (byte)(edge * 252);
        }
        using var image = Image.CreateFromData(size, size, false, Image.Format.Rgba8, pixels);
        image.GenerateMipmaps();
        return ImageTexture.CreateFromImage(image);
    }

    /// <summary>The landmark is secret until this observer has both unlocked access and explored it.</summary>
    public GalacticCoreMetadata? UiGalacticCore => _galaxy is not null &&
        _galaxy.Knowledge.IsGalacticCoreDiscovered(_galaxy.PlayerCivilizationId) ? _galaxy.GalacticCore : null;

    public Vector2? UiGalacticCoreScreenPosition => UiGalacticCore is { } core
        ? ToScreen(new System.Numerics.Vector2(core.X, core.Y), UiMapOriginScreen)
        : null;

    public float UiGalacticCoreScreenRadius => UiGalacticCore is { } core
        ? core.ExclusionRadius * UiCatalogVisualCoordinateScale * UiMapZoom
        : 0;

    internal Vector2? UndisclosedCoreScreenPosition => _galaxy?.GalacticCore is { } core
        ? ToScreen(new System.Numerics.Vector2(core.X, core.Y), UiMapOriginScreen) : null;

    private bool IsInsideGalacticCoreMarker(Vector2 screenPoint) =>
        UndisclosedCoreScreenPosition is { } center &&
        screenPoint.DistanceTo(center) <= _galaxy!.GalacticCore!.ExclusionRadius * UiCatalogVisualCoordinateScale * UiMapZoom;

    private void ExplainUnavailableGalacticCore()
    {
        if (UiGalacticCore is null) return; // Fog does not announce the secret or grant knowledge.
        SetStatus("No safe approach route is available yet.", 6.0);
    }
}
