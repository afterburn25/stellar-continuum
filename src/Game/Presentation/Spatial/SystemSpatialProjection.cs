using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Exploration;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;

namespace Game.Presentation.Spatial;

/// <summary>
/// Presentation-only body classes. These are derived exclusively from observer-safe exploration
/// data and never become authoritative simulation facts.
/// </summary>
public enum SystemSpatialBodyVisualClass
{
    UnknownPlanet,
    UnknownMoon,
    Rocky,
    Oceanic,
    Frozen,
    HotRocky,
    GasGiant,
    IceGiant,
    Moon,
}

public sealed record SystemSpatialBodyMarker(
    int BodyId,
    int? ParentBodyId,
    int OrbitIndex,
    string Label,
    PlanetaryBodyKind Kind,
    SystemSpatialBodyVisualClass VisualClass,
    float OffsetX,
    float OffsetY,
    float OrbitRadius,
    float DisplayRadius,
    bool HasDetailedEnvironment,
    bool PositiveResourceSignature,
    bool PositiveAnomalySignature,
    bool PositiveActivitySignature,
    double RadiusEarth,
    double? MassEarth,
    double? GravityG,
    double? TemperatureKelvin,
    double? PressureKPa,
    PlanetaryAtmosphereRegime? Atmosphere,
    string? SurfaceKey = null,
    bool HasCityLights = false,
    float OrbitalEccentricity = 0,
    float OrbitalInclinationDegrees = 0,
    float OffsetHeight = 0)
{
    // A terrestrial Earth still illustrates oceans without becoming an immersed environment.
    public bool HasIllustratedOcean => HasDetailedEnvironment &&
        VisualClass is not (SystemSpatialBodyVisualClass.UnknownPlanet or SystemSpatialBodyVisualClass.UnknownMoon) &&
        (VisualClass == SystemSpatialBodyVisualClass.Oceanic || SurfaceKey == "earth");
}

public enum SystemSpatialInfrastructureState
{
    Locked,
    Available,
    Active,
    Complete,
}

public sealed record SystemSpatialInfrastructureMarker(
    string ProjectId,
    string Label,
    SystemSpatialInfrastructureState State,
    double Progress, int? HostBodyId = null);

public sealed record SystemSpatialSnapshot(
    int SystemId,
    string CatalogName,
    SystemSurveyLevel SurveyLevel,
    double SurveyProgress,
    StarArchetype? StarArchetype,
    float DesignRadius,
    IReadOnlyList<SystemSpatialBodyMarker> Bodies,
    string? CatalogPresetId = null,
    IReadOnlyList<SystemSpatialInfrastructureMarker>? Infrastructure = null,
    StellarPrimaryClass? StellarClass = null,
    StellarPrimaryClass? SecondaryStellarClass = null,
    StellarPrimaryClass? TertiaryStellarClass = null);

/// <summary>
/// Converts the simulation-owned fog-safe exploration read model into deterministic schematic
/// display geometry. Simulation distance, travel timing and authoritative orbital facts are never
/// changed by this projection.
/// </summary>
public sealed class SystemSpatialProjection
{
    private const float FirstMoonOrbit = 40.0f;
    private const float MoonOrbitStep = 24.0f;

    public SystemSpatialSnapshot Build(KnownSystemExplorationView system)
    {
        ArgumentNullException.ThrowIfNull(system);
        if (system.SurveyLevel < SystemSurveyLevel.PartiallySurveyed || !system.HasReconnaissanceCatalog)
        {
            throw new InvalidOperationException(
                "A system spatial projection requires at least reconnaissance-grade orbital knowledge.");
        }

        var markers = new List<SystemSpatialBodyMarker>(system.PlanetaryBodies.Count);
        var positions = new Dictionary<int, (float X, float Y, float Radius)>();

        var planets = system.PlanetaryBodies
                     .Where(body => body.Kind != PlanetaryBodyKind.Moon)
                     .OrderBy(body => body.OrbitIndex)
                     .ThenBy(body => body.BodyId).ToArray();
        // Allocate space for each complete planet/moon family, including Saturn's rings.
        // The central clearance also preserves a readable primary star at overview scale.
        float ParentExtent(PlanetaryBodyExplorationView body) => ResolveDisplayRadius(body) *
            (body.HasDetailedEnvironment && system.CatalogPresetId == "sol-v1" && body.Name == "Saturn" ? 2.8f : 1f);
        float MoonOrbit(PlanetaryBodyExplorationView moon, float parentExtent) =>
            parentExtent + FirstMoonOrbit + moon.OrbitIndex * MoonOrbitStep + ResolveDisplayRadius(moon);
        var familyExtents = planets.ToDictionary(body => body.BodyId, body =>
            system.PlanetaryBodies.Where(moon => moon.Kind == PlanetaryBodyKind.Moon && moon.ParentBodyId == body.BodyId)
                .Select(moon => MoonOrbit(moon, ParentExtent(body)) + ResolveDisplayRadius(moon))
                .DefaultIfEmpty(ParentExtent(body)).Max());
        var nextOrbit = Math.Max(1700f, familyExtents.Values.Sum() * 3f);
        var previousExtent = 0f;
        foreach (var body in planets)
        {
            if (markers.Count > 0) nextOrbit += previousExtent + familyExtents[body.BodyId] + 160f;
            var orbitRadius = nextOrbit;
            previousExtent = familyExtents[body.BodyId];
            var angle = StableAngle(body.BodyId);
            var point = SystemOrbitGeometry.Point(orbitRadius, (float)body.OrbitalEccentricity,
                (float)body.OrbitalInclinationDegrees, angle);
            var x = point.X;
            var y = point.Y;
            var displayRadius = ResolveDisplayRadius(body);
            var marker = BuildMarker(body, x, y, orbitRadius, displayRadius, system.CatalogPresetId)
                with { OffsetHeight = point.Height };
            markers.Add(marker);
            positions[body.BodyId] = (x, y, ParentExtent(body));
        }

        foreach (var body in system.PlanetaryBodies
                     .Where(body => body.Kind == PlanetaryBodyKind.Moon)
                     .OrderBy(body => body.ParentBodyId)
                     .ThenBy(body => body.OrbitIndex)
                     .ThenBy(body => body.BodyId))
        {
            if (body.ParentBodyId is not int parentId || !positions.TryGetValue(parentId, out var parent))
            {
                throw new InvalidOperationException(
                    $"Observer-safe moon {body.BodyId} has no visible parent planet in system {system.SystemId}.");
            }

            var orbitRadius = MoonOrbit(body, parent.Radius);
            var angle = StableAngle(body.BodyId ^ parentId);
            var x = parent.X + MathF.Cos(angle) * orbitRadius;
            var y = parent.Y + MathF.Sin(angle) * orbitRadius;
            var displayRadius = ResolveDisplayRadius(body);
            var marker = BuildMarker(body, x, y, orbitRadius, displayRadius, system.CatalogPresetId);
            markers.Add(marker);
            positions[body.BodyId] = (x, y, displayRadius);
        }

        // Fit the complete paths that are actually drawn. A moon can currently sit on the
        // inward side of its parent while its circular path still extends farther outward.
        var orbitalPathExtent = markers.Count == 0 ? 150f : markers.Max(marker =>
            marker.Kind != PlanetaryBodyKind.Moon
                ? marker.OrbitRadius * (1f + marker.OrbitalEccentricity) + familyExtents[marker.BodyId]
                : marker.ParentBodyId is int parentId && positions.TryGetValue(parentId, out var parent)
                    ? MathF.Sqrt(parent.X * parent.X + parent.Y * parent.Y) + marker.OrbitRadius + marker.DisplayRadius
                    : MathF.Sqrt(marker.OffsetX * marker.OffsetX + marker.OffsetY * marker.OffsetY));
        var designRadius = Math.Max(180.0f, orbitalPathExtent + 30.0f);

        return new SystemSpatialSnapshot(
            system.SystemId,
            system.CatalogName,
            system.SurveyLevel,
            system.SurveyProgress,
            system.Archetype,
            designRadius,
            markers.OrderBy(marker => marker.BodyId).ToArray(), system.CatalogPresetId,
            StellarClass: system.StellarClass,
            SecondaryStellarClass: system.SecondaryStellarClass,
            TertiaryStellarClass: system.TertiaryStellarClass);
    }

    private static SystemSpatialBodyMarker BuildMarker(
        PlanetaryBodyExplorationView body,
        float x,
        float y,
        float orbitRadius,
        float displayRadius,
        string? catalogPresetId) =>
        new(
            body.BodyId,
            body.ParentBodyId,
            body.OrbitIndex,
            body.Name,
            body.Kind,
            ResolveVisualClass(body, catalogPresetId),
            x,
            y,
            orbitRadius,
            displayRadius,
            body.HasDetailedEnvironment,
            body.HasRareResource == true || body.HasRareResourceSignature == true,
            body.HasAnomaly == true || body.HasAnomalySignature == true,
            body.HasPreWarpCivilization == true || body.HasActivitySignature == true,
            body.RadiusEarth,
            body.MassEarth,
            body.GravityG,
            body.TemperatureKelvin,
            body.PressureKPa,
            body.Atmosphere,
            body.HasDetailedEnvironment && catalogPresetId == "sol-v1" ? body.Name.ToLowerInvariant() : null,
            OrbitalEccentricity: (float)body.OrbitalEccentricity,
            OrbitalInclinationDegrees: (float)body.OrbitalInclinationDegrees);

    private static float ResolveDisplayRadius(PlanetaryBodyExplorationView body) =>
        SystemCelestialScale.BodyRadius(body.RadiusEarth, body.Kind);

    private static SystemSpatialBodyVisualClass ResolveVisualClass(PlanetaryBodyExplorationView body, string? catalogPresetId)
    {
        if (!body.HasDetailedEnvironment)
        {
            return body.Kind == PlanetaryBodyKind.Moon
                ? SystemSpatialBodyVisualClass.UnknownMoon
                : SystemSpatialBodyVisualClass.UnknownPlanet;
        }

        if (body.Kind == PlanetaryBodyKind.Moon)
            return SystemSpatialBodyVisualClass.Moon;

        // The generic temperature heuristic cannot distinguish cold gas giants from ice giants.
        // Only the explicit, fully known canonical catalog supplies these named distinctions.
        if (catalogPresetId == "sol-v1")
        {
            if (body.Name is "Jupiter" or "Saturn") return SystemSpatialBodyVisualClass.GasGiant;
            if (body.Name is "Uranus" or "Neptune") return SystemSpatialBodyVisualClass.IceGiant;
        }

        var temperature = body.TemperatureKelvin ?? 280.0;
        if (body.HasSolidSurface == false)
        {
            return temperature < 170.0
                ? SystemSpatialBodyVisualClass.IceGiant
                : SystemSpatialBodyVisualClass.GasGiant;
        }

        if (body.IsImmersedEnvironment == true)
            return SystemSpatialBodyVisualClass.Oceanic;
        if (temperature < 200.0)
            return SystemSpatialBodyVisualClass.Frozen;
        if (temperature > 410.0)
            return SystemSpatialBodyVisualClass.HotRocky;
        return SystemSpatialBodyVisualClass.Rocky;
    }

    private static float StableAngle(int bodyId)
    {
        var value = unchecked((uint)(bodyId + 1) * 2654435761u);
        value ^= value >> 16;
        var fraction = (value & 0x00FF_FFFFu) / 16777216.0f;
        return fraction * MathF.PI * 2.0f;
    }
}
