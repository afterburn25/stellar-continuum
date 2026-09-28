using System;
using Game.Simulation.Models;

namespace Game.Simulation.Economy;

public sealed record ResourceDepositProfile(
    string MaterialName,
    string Grade,
    double GradeMultiplier,
    double Accessibility,
    double EnvironmentalHazard,
    double ExtractionYieldMultiplier)
{
    public static ResourceDepositProfile ForBody(PlanetaryBodyState body)
    {
        ArgumentNullException.ThrowIfNull(body);
        if (!body.HasRareResource)
            return new("No confirmed deposit", "None", 0.0, 0.0, 0.0, 0.0);

        var hash = unchecked((uint)(body.Id * 747796405 + 2891336453));
        hash = (hash >> ((int)(hash >> 28) + 4)) ^ hash;
        hash *= 277803737u;
        hash = (hash >> 22) ^ hash;

        var material = body.Environment.TemperatureKelvin < 190.0
            ? "Volatile ices"
            : (hash % 5) switch
            {
                0 => "Nickel-iron ore",
                1 => "Cobalt-rich ore",
                2 => "Platinum-group ore",
                3 => "Rare-earth minerals",
                _ => "Uranium-thorium ore",
            };
        var gradeMultiplier = 0.70 + ((hash >> 8) & 0xFF) / 255.0 * 0.70;
        var grade = gradeMultiplier >= 1.22 ? "Exceptional" :
            gradeMultiplier >= 1.04 ? "Rich" :
            gradeMultiplier >= 0.86 ? "Standard" : "Marginal";
        var environment = body.Environment;
        var hazard = Math.Clamp(
            environment.RadiationHazard * 0.45 +
            Math.Abs(environment.GravityG - 1.0) * 0.12 +
            (environment.PressureKPa <= 0.01 ? 0.12 : Math.Max(0.0, environment.PressureKPa - 180.0) / 2_000.0) +
            Math.Abs(environment.TemperatureKelvin - 288.0) / 1_000.0,
            0.0,
            0.75);
        var localVariation = 0.90 + ((hash >> 16) & 0xFF) / 255.0 * 0.10;
        var accessibility = Math.Clamp((1.0 - hazard) * localVariation, 0.45, 1.0);
        return new(material, grade, gradeMultiplier, accessibility, hazard,
            gradeMultiplier * accessibility);
    }
}
