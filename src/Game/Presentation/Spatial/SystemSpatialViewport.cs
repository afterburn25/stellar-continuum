using System;
using Game.Simulation.Models;

namespace Game.Presentation.Spatial;

/// <summary>Shared schematic transform and visible object radii for drawing and pointer targeting.</summary>
public readonly record struct SystemSpatialViewport(float CenterX, float CenterY, float Scale)
{
    public static SystemSpatialViewport Fit(SystemSpatialSnapshot snapshot, float width, float height)
    {
        // Fit against the actual orbital-safe rectangle instead of moving its centre and
        // radius independently. The field can begin behind the compact translucent title
        // card at 146, while the command dock begins 130 px from the bottom. This gives the
        // restored 2D orbital view more useful scale without entering the rail or inspector.
        var safeLeft = 104.0f;
        var safeRight = Math.Max(safeLeft + 2.0f, width - 224.0f);
        var safeTop = Math.Min(146.0f, Math.Max(0.0f, height - 131.0f));
        var safeBottom = Math.Max(safeTop + 1.0f, height - 130.0f);
        var centerX = (safeLeft + safeRight) * .5f;
        var centerY = (safeTop + safeBottom) * .5f;
        var availableRadius = Math.Max(1.0f, Math.Min(
            Math.Min(centerX - safeLeft, safeRight - centerX),
            Math.Min(centerY - safeTop, safeBottom - centerY)));
        return new(centerX, centerY,
            // Open at a useful orbital scale. Wheel-out reveals the full outer gate belt;
            // fitting every decorative label here would collapse the planetary system.
            Math.Min(availableRadius * .95f / snapshot.DesignRadius, 1.15f));
    }

    public (float X, float Y) WorldToScreen(float x, float y) => (CenterX + x * Scale, CenterY + y * Scale);
    public (float X, float Y) ScreenToWorld(float x, float y) => ((x - CenterX) / Scale, (y - CenterY) / Scale);

    public float BodyRadius(SystemSpatialBodyMarker body) =>
        Math.Max(SystemCelestialScale.MinimumScreenRadius, body.DisplayRadius * Scale);

    public bool IsBodyVisible(SystemSpatialSnapshot snapshot, SystemSpatialBodyMarker body)
    {
        if (body.Kind != PlanetaryBodyKind.Moon) return true;
        foreach (var parent in snapshot.Bodies)
            if (parent.BodyId == body.ParentBodyId)
                return body.OrbitRadius * Scale > BodyRadius(parent) + BodyRadius(body) + 5f;
        return false;
    }

    public int? HitBody(SystemSpatialSnapshot snapshot, float x, float y)
    {
        int? nearestId = null;
        var nearestDistance = float.MaxValue;
        foreach (var body in snapshot.Bodies)
        {
            if (!IsBodyVisible(snapshot, body)) continue;
            var dx = x - CenterX - body.OffsetX * Scale;
            var dy = y - CenterY - body.OffsetY * Scale;
            var distance = dx * dx + dy * dy;
            var hitRadius = BodyRadius(body) * (body.SurfaceKey == "saturn" ? 2.25f : 1f) + 4.0f;
            if (distance <= hitRadius * hitRadius && distance < nearestDistance)
            {
                nearestId = body.BodyId;
                nearestDistance = distance;
            }
        }
        return nearestId;
    }

    public bool HitsCelestialObject(SystemSpatialSnapshot snapshot, float x, float y) =>
        HitsStar(x, y) || HitBody(snapshot, x, y).HasValue;

    public bool HitsStar(float x, float y) =>
        Inside(x, y, CenterX, CenterY, SystemCelestialScale.StarScreenRadius(Scale) * 1.75f);

    private static bool Inside(float x, float y, float centerX, float centerY, float radius)
    {
        var dx = x - centerX;
        var dy = y - centerY;
        return dx * dx + dy * dy <= radius * radius;
    }
}
