using System;

namespace Game.Presentation.Spatial;

/// <summary>Display ellipse with the star at a focus. This is not an ephemeris or a travel rule.</summary>
public static class SystemOrbitGeometry
{
    public static (float X, float Y, float Height) Point(float radius, float eccentricity,
        float inclinationDegrees, float eccentricAnomaly)
    {
        var e = Math.Clamp(eccentricity, 0f, .95f);
        var inclination = inclinationDegrees * MathF.PI / 180f;
        var x = radius * (MathF.Cos(eccentricAnomaly) - e);
        var z = radius * MathF.Sqrt(1f - e * e) * MathF.Sin(eccentricAnomaly);
        var y = z * MathF.Cos(inclination);
        // A consistent apsidal orientation makes eccentric paths readable on the chart.
        var angle = e > 0 ? .62f : 0f;
        return (x * MathF.Cos(angle) - y * MathF.Sin(angle),
            x * MathF.Sin(angle) + y * MathF.Cos(angle), z * MathF.Sin(inclination));
    }
}
