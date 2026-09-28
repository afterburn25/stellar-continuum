using System;
using System.Text.Json.Serialization;

namespace Game.Simulation.Models;

public enum PlanetaryBodyKind
{
    Planet,
    Moon,
    DwarfPlanet,
}

/// <summary>
/// Broad physical atmosphere regime reported by planetary science. This is a world fact,
/// not a species-relative breathability judgement.
/// </summary>
public enum PlanetaryAtmosphereRegime
{
    Vacuum,
    OxygenNitrogen,
    OxygenRich,
    CarbonDioxideRich,
    Reducing,
    Inert,
    Other,
}

/// <summary>
/// Dominant naturally available surface/ocean liquid where one exists. Species decides
/// whether that solvent is biologically useful; the world model only reports the environment.
/// </summary>
public enum PlanetarySolventRegime
{
    None,
    Water,
    Ammonia,
    Hydrocarbon,
    Other,
}

public sealed record PlanetaryEnvironmentState(
    double GravityG,
    double TemperatureKelvin,
    double PressureKPa,
    PlanetaryAtmosphereRegime Atmosphere,
    PlanetarySolventRegime AvailableSolvent,
    double RadiationHazard,
    bool IsImmersedEnvironment,
    bool HasSolidSurface)
{
    public PlanetaryEnvironmentState Validated()
    {
        if (!double.IsFinite(GravityG) || GravityG < 0.0)
            throw new InvalidOperationException("Planetary gravity must be finite and non-negative.");
        if (!double.IsFinite(TemperatureKelvin) || TemperatureKelvin <= 0.0)
            throw new InvalidOperationException("Planetary temperature must be finite and above absolute zero.");
        if (!double.IsFinite(PressureKPa) || PressureKPa < 0.0)
            throw new InvalidOperationException("Planetary pressure must be finite and non-negative.");
        if (!double.IsFinite(RadiationHazard) || RadiationHazard < 0.0 || RadiationHazard > 1.0)
            throw new InvalidOperationException("Planetary radiation hazard must be between 0 and 1.");
        return this;
    }
}

/// <summary>
/// Authoritative physical body data. No field here means universally habitable. The temporary
/// LegacyColonizationCandidate flag exists only to preserve the current pre-Species gameplay
/// contract while contextual habitability is being integrated.
/// </summary>
public sealed record PlanetaryBodyState(
    int Id,
    int SystemId,
    int? ParentBodyId,
    int OrbitIndex,
    string Name,
    PlanetaryBodyKind Kind,
    double RadiusEarth,
    double MassEarth,
    PlanetaryEnvironmentState Environment,
    bool LegacyColonizationCandidate,
    bool HasRareResource,
    bool HasAnomaly,
    bool HasPreWarpCivilization,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)] double OrbitalEccentricity = 0.0,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)] double OrbitalInclinationDegrees = 0.0)
{
    public PlanetaryBodyState Validated()
    {
        if (Id < 0) throw new InvalidOperationException("Planetary body IDs must be non-negative.");
        if (SystemId < 0) throw new InvalidOperationException("Planetary body system IDs must be non-negative.");
        if (OrbitIndex < 0) throw new InvalidOperationException("Planetary body orbit indices must be non-negative.");
        if (string.IsNullOrWhiteSpace(Name)) throw new InvalidOperationException("Planetary bodies require a name.");
        if (!Enum.IsDefined(Kind)) throw new InvalidOperationException("Planetary body kind is invalid.");
        if ((Kind is PlanetaryBodyKind.Planet or PlanetaryBodyKind.DwarfPlanet) && ParentBodyId is not null)
            throw new InvalidOperationException("Primary planetary bodies cannot have a parent body.");
        if (Kind == PlanetaryBodyKind.Moon && ParentBodyId is null)
            throw new InvalidOperationException("Moons require a parent body.");
        if (!double.IsFinite(RadiusEarth) || RadiusEarth <= 0.0)
            throw new InvalidOperationException("Planetary body radius must be finite and positive.");
        if (!double.IsFinite(MassEarth) || MassEarth <= 0.0)
            throw new InvalidOperationException("Planetary body mass must be finite and positive.");
        if (!double.IsFinite(OrbitalEccentricity) || OrbitalEccentricity < 0.0 || OrbitalEccentricity >= 1.0)
            throw new InvalidOperationException("Planetary body orbital eccentricity must be finite and in [0, 1).");
        if (!double.IsFinite(OrbitalInclinationDegrees) || OrbitalInclinationDegrees < 0.0 || OrbitalInclinationDegrees > 180.0)
            throw new InvalidOperationException("Planetary body orbital inclination must be finite and between 0 and 180 degrees.");
        Environment.Validated();
        return this;
    }
}
