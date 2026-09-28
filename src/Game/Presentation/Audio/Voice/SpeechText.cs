using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;

namespace Game.Presentation.Audio.Voice;

public static partial class SpeechText
{
    private static readonly IReadOnlyDictionary<string, string> GlobalTerms =
        new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["Stellar Continuum"] = "Stellar Continuum", ["Sol"] = "Sohl",
            ["Alpha Centauri"] = "Alpha Sen-tor-eye", ["Proxima Centauri"] = "Proxima Sen-tor-eye",
            ["Thalori"] = "Tha-lor-ee", ["FTL"] = "F T L", ["AU"] = "astronomical units",
        };
    [GeneratedRegex(@"\s+", RegexOptions.CultureInvariant)] private static partial Regex WhiteSpace();
    [GeneratedRegex(@"(?<![\p{L}\p{N}])(-?\d+(?:\.\d+)?)\s*%", RegexOptions.CultureInvariant)] private static partial Regex Percent();
    [GeneratedRegex(@"\b([A-Z]{2,6})-(\d{1,6})\b", RegexOptions.CultureInvariant)] private static partial Regex ShipId();
    [GeneratedRegex(@"(?<!\w)(-?\d+(?:\.\d+)?),\s+(-?\d+(?:\.\d+)?)(?!\w)", RegexOptions.CultureInvariant)] private static partial Regex Coordinates();
    [GeneratedRegex(@"\b(\d{1,2}):(\d{2})\b", RegexOptions.CultureInvariant)] private static partial Regex ClockTime();
    [GeneratedRegex(@"\b([IVX]{2,5})\b", RegexOptions.CultureInvariant)] private static partial Regex RomanNumeral();
    [GeneratedRegex(@"\b(-?\d+(?:\.\d+)?)\s*c\b", RegexOptions.CultureInvariant | RegexOptions.IgnoreCase)] private static partial Regex LightSpeed();

    public static string Normalize(string text) => Normalize(text, null, null);
    public static string Normalize(string text, IReadOnlyDictionary<string, string>? profileTerms,
        IReadOnlyDictionary<string, string>? requestTerms)
    {
        var value = WhiteSpace().Replace((text ?? string.Empty).Replace('\r', ' ').Replace('\n', ' ').Trim(), " ");
        value = ShipId().Replace(value, match => string.Join(' ', match.Groups[1].Value.ToCharArray()) + " " + SpeakDigits(match.Groups[2].Value));
        value = ApplyPronunciations(value, Merge(GlobalTerms, profileTerms, requestTerms));
        value = Coordinates().Replace(value, "$1 by $2");
        value = Percent().Replace(value, "$1 percent");
        value = ClockTime().Replace(value, "$1 $2 hours");
        value = LightSpeed().Replace(value, "$1 times the speed of light");
        value = Regex.Replace(value, @"\bkm\b", "kilometers", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
        value = Regex.Replace(value, @"\bK\b", "kelvin", RegexOptions.CultureInvariant);
        value = RomanNumeral().Replace(value, match => RomanToWords(match.Value));
        value = Regex.Replace(value, @"\b(planet|mark|sector|class) I\b", "$1 one", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
        value = Regex.Replace(value, @"\b(\d{4})-(\d{2})-(\d{2})\b", match =>
            DateTime.TryParseExact(match.Value, "yyyy-MM-dd", System.Globalization.CultureInfo.InvariantCulture,
                System.Globalization.DateTimeStyles.None, out var date) ? date.ToString("MMMM d, yyyy", System.Globalization.CultureInfo.InvariantCulture) : match.Value);
        return WhiteSpace().Replace(value, " ").Trim();
    }

    public static string NormalizeForProfile(string text, VoiceProfile profile, IReadOnlyDictionary<string, string>? requestTerms) =>
        Normalize(text, Merge(profile.CivilizationPronunciations, profile.SpeciesPronunciations,
            profile.Pronunciations, profile.CharacterPronunciations), requestTerms);

    private static IReadOnlyDictionary<string, string> Merge(params IReadOnlyDictionary<string, string>?[] dictionaries)
    {
        var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var dictionary in dictionaries)
            if (dictionary is not null) foreach (var pair in dictionary) result[pair.Key] = pair.Value;
        return result;
    }

    public static string ApplyPronunciations(string text, IReadOnlyDictionary<string, string>? terms)
    {
        if (terms is null) return text;
        var replacements = new Dictionary<string, string>(terms, StringComparer.OrdinalIgnoreCase);
        var keys = terms.Keys.Where(key => !string.IsNullOrWhiteSpace(key)).OrderByDescending(key => key.Length).ToArray();
        if (keys.Length == 0) return text;
        return Regex.Replace(text, $@"(?<![\p{{L}}\p{{N}}])(?:{string.Join("|", keys.Select(Regex.Escape))})(?![\p{{L}}\p{{N}}])",
            match => replacements[match.Value], RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    }

    private static string SpeakDigits(string digits) => string.Join(' ', digits.Select(character => character switch
    {
        '0' => "zero", '1' => "one", '2' => "two", '3' => "three", '4' => "four", '5' => "five",
        '6' => "six", '7' => "seven", '8' => "eight", '9' => "nine", _ => character.ToString(),
    }));
    private static string RomanToWords(string numeral) => numeral switch
    {
        "I" => "one", "II" => "two", "III" => "three", "IV" => "four", "V" => "five",
        "VI" => "six", "VII" => "seven", "VIII" => "eight", "IX" => "nine", "X" => "ten", _ => numeral,
    };
}
