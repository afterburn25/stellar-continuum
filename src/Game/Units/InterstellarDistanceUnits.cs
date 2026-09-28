using System;
using System.Globalization;

namespace Game.Units;

/// <summary>Pure display conversion for interstellar lengths; simulation keeps light-year values.</summary>
public static class InterstellarDistanceUnits
{
    public const double KilometresPerLightYear = 9.4607304725808e12;

    public static string FormatMetricPrimary(double lightYears) =>
        double.IsFinite(lightYears) && lightYears >= 0.0
            ? $"{Scientific(lightYears * KilometresPerLightYear)} km · {lightYears:0.#} ly"
            : "Distance unconfirmed";

    public static string FormatMetricSpeed(double lightYearsPerDay) =>
        double.IsFinite(lightYearsPerDay) && lightYearsPerDay >= 0.0
            ? $"{Scientific(lightYearsPerDay * KilometresPerLightYear)} km/day · {lightYearsPerDay:0.#} ly/day"
            : "Speed unconfirmed";

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
