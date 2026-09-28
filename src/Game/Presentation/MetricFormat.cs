using System;
using System.Globalization;
using Game.Units;

namespace Game.Presentation;

/// <summary>Display-only SI formatting. Simulation ratios and schematic map coordinates stay untouched.</summary>
public static class MetricFormat
{
    public const double EarthRadiusKilometres = 6_371.0;
    public const double EarthMassKilograms = 5.9722e24;
    public const double StandardGravityMetresPerSecondSquared = 9.80665;

    public static string Radius(double earthRadii, bool confirmed) =>
        confirmed && double.IsFinite(earthRadii) && earthRadii > 0.0
            ? $"{earthRadii * EarthRadiusKilometres:N0} km"
            : "Unconfirmed";

    public static string Mass(double? earthMasses, bool confirmed) =>
        confirmed && earthMasses is double mass && double.IsFinite(mass) && mass >= 0.0
            ? Scientific(mass * EarthMassKilograms) + " kg"
            : "Unconfirmed";

    public static string Gravity(double? gravityG, bool confirmed) =>
        confirmed && gravityG is double gravity && double.IsFinite(gravity) && gravity >= 0.0
            ? $"{gravity * StandardGravityMetresPerSecondSquared:0.##} m/s²"
            : "Unconfirmed";

    public static string Temperature(double? kelvin, bool confirmed) =>
        confirmed && kelvin is double temperature && double.IsFinite(temperature) && temperature >= 0.0
            ? $"{temperature:0} K"
            : "Unconfirmed";

    public static string Pressure(double? kiloPascals, bool confirmed) =>
        confirmed && kiloPascals is double pressure && double.IsFinite(pressure) && pressure >= 0.0
            ? $"{pressure:0.##} kPa"
            : "Unconfirmed";

    public static string PhysicalScaleSummary(double radiusEarth, double? massEarth, bool confirmed) =>
        confirmed && massEarth.HasValue
            ? $"{Radius(radiusEarth, true)} · {Mass(massEarth, true)}"
            : "Physical data unconfirmed";

    public static string PhysicalEnvironmentSummary(double? gravityG, double? kelvin, double? kiloPascals, bool confirmed) =>
        confirmed && gravityG.HasValue && kelvin.HasValue && kiloPascals.HasValue
            ? $"{Gravity(gravityG, true)} · {Temperature(kelvin, true)} · {Pressure(kiloPascals, true)}"
            : "Environment data unconfirmed";

    public static string InterstellarDistance(double lightYears, double parsecs) =>
        double.IsFinite(lightYears) && lightYears >= 0.0
            ? InterstellarLength(lightYears) + $" · {parsecs:0.0} pc"
            : "Distance unconfirmed";

    public static string InterstellarDistance(double lightYears) =>
        InterstellarDistance(lightYears, lightYears / 3.26156);

    public static string InterstellarLength(double lightYears) => InterstellarDistanceUnits.FormatMetricPrimary(lightYears);

    public static string InterstellarSpeed(double lightYearsPerDay) => InterstellarDistanceUnits.FormatMetricSpeed(lightYearsPerDay);

    private static string Scientific(double value)
    {
        if (value < 1_000_000.0) return value.ToString("N0", CultureInfo.InvariantCulture);
        var exponent = (int)Math.Floor(Math.Log10(value));
        var coefficient = value / Math.Pow(10.0, exponent);
        return coefficient.ToString("0.###", CultureInfo.InvariantCulture) + " × 10" + Superscript(exponent);
    }

    private static string Superscript(int value)
    {
        const string digits = "⁰¹²³⁴⁵⁶⁷⁸⁹";
        if (value == 0) return digits[0].ToString();
        var result = value < 0 ? "⁻" : string.Empty;
        foreach (var digit in Math.Abs(value).ToString(CultureInfo.InvariantCulture)) result += digits[digit - '0'];
        return result;
    }
}
