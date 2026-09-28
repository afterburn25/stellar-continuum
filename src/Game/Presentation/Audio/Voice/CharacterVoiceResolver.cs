using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Game.Presentation.Audio.Voice;

public enum VoiceSpeakerRole
{
    Narrator,
    FleetCommander,
    ChiefScientist,
    Diplomat,
    Governor,
    EconomicAdvisor,
    OperationsOfficer,
    ShipComputer,
    ColonyComputer,
    ExpeditionCommander,
    AlienDiplomat,
    AlienScientist,
    AlienCommander,
}

public sealed record VoiceCharacter(string Id, string DisplayName, string Office, string? VoiceProfileId,
    string? Portrait = null);

public sealed record VoiceSpeakerContext(VoiceSpeakerRole Role, int SourceCivilizationId,
    string SourceSpeciesId = "", string? ExactCharacterId = null, string? ExactVoiceProfileId = null);

public sealed record ResolvedVoiceSpeaker(string ProfileId, string DisplayName, VoiceSpeakerRole Role,
    string? CharacterId = null, string? Portrait = null, bool IsFallback = false);

public sealed record VoiceRoleMapping(string Role, string Profile, int? CivilizationId = null, string Species = "*");

/// <summary>Resolves a role against the current roster at presentation time, then applies
/// civilization, species, and generic profile fallbacks without consulting simulation internals.</summary>
public sealed class CharacterVoiceResolver
{
    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true };
    private readonly VoiceProfileRegistry _profiles;
    private readonly IReadOnlyList<VoiceRoleMapping> _mappings;
    private readonly Func<VoiceSpeakerContext, VoiceCharacter?>? _currentCharacter;

    public CharacterVoiceResolver(VoiceProfileRegistry profiles, IEnumerable<VoiceRoleMapping> mappings,
        Func<VoiceSpeakerContext, VoiceCharacter?>? currentCharacter = null)
    {
        _profiles = profiles ?? throw new ArgumentNullException(nameof(profiles));
        _mappings = (mappings ?? throw new ArgumentNullException(nameof(mappings))).ToArray();
        _currentCharacter = currentCharacter;
    }

    public static CharacterVoiceResolver FromJson(VoiceProfileRegistry profiles, string json,
        Func<VoiceSpeakerContext, VoiceCharacter?>? currentCharacter = null) => new(profiles,
        JsonSerializer.Deserialize<VoiceRoleMapping[]>(json, JsonOptions) ?? Array.Empty<VoiceRoleMapping>(),
        currentCharacter);

    public static CharacterVoiceResolver Load(VoiceProfileRegistry profiles, string path,
        Func<VoiceSpeakerContext, VoiceCharacter?>? currentCharacter = null) =>
        FromJson(profiles, File.ReadAllText(path), currentCharacter);

    public ResolvedVoiceSpeaker? Resolve(VoiceSpeakerContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        var requireNonHuman = IsAlienRole(context.Role) || IsNonHumanSpecies(context.SourceSpeciesId);
        var character = _currentCharacter?.Invoke(context);

        if (TryProfile(context.ExactVoiceProfileId, requireNonHuman, out var exactProfile))
        {
            if (character is not null &&
                (string.IsNullOrWhiteSpace(context.ExactCharacterId) ||
                 string.Equals(character.Id, context.ExactCharacterId, StringComparison.OrdinalIgnoreCase)))
            {
                return new(exactProfile.Id, character.DisplayName, context.Role, character.Id,
                    character.Portrait ?? exactProfile.Portrait);
            }
            return FromProfile(exactProfile, context, context.ExactCharacterId, false);
        }

        if (character is not null &&
            (string.IsNullOrWhiteSpace(context.ExactCharacterId) ||
             string.Equals(character.Id, context.ExactCharacterId, StringComparison.OrdinalIgnoreCase)) &&
            TryProfile(character.VoiceProfileId, requireNonHuman, out var characterProfile))
        {
            return new(characterProfile.Id, character.DisplayName, context.Role, character.Id,
                character.Portrait ?? characterProfile.Portrait);
        }

        foreach (var mapping in RankedMappings(context))
        {
            if (TryProfile(mapping.Profile, requireNonHuman, out var profile))
                return character is null
                    ? FromProfile(profile, context, null, true)
                    : new(profile.Id, character.DisplayName, context.Role, character.Id,
                        character.Portrait ?? profile.Portrait, true);
        }

        return null;
    }

    private IEnumerable<VoiceRoleMapping> RankedMappings(VoiceSpeakerContext context) => _mappings
        .Where(mapping => Enum.TryParse<VoiceSpeakerRole>(mapping.Role, true, out var role) && role == context.Role)
        .Where(mapping => (!mapping.CivilizationId.HasValue || mapping.CivilizationId == context.SourceCivilizationId) &&
                          Matches(mapping.Species, context.SourceSpeciesId))
        .OrderByDescending(mapping => (mapping.CivilizationId.HasValue ? 2 : 0) +
                                      Specificity(mapping.Species, context.SourceSpeciesId));

    private bool TryProfile(string? id, bool alienRole, out VoiceProfile profile)
    {
        profile = null!;
        if (string.IsNullOrWhiteSpace(id) || !_profiles.TryResolve(id, out profile)) return false;
        return !alienRole || (!string.Equals(profile.Species, "human", StringComparison.OrdinalIgnoreCase) &&
                              !string.Equals(profile.Species, "terran_baseline", StringComparison.OrdinalIgnoreCase));
    }

    private static ResolvedVoiceSpeaker FromProfile(VoiceProfile profile, VoiceSpeakerContext context,
        string? characterId, bool fallback) => new(profile.Id,
        string.IsNullOrWhiteSpace(profile.SubtitleName) ? profile.DisplayName : profile.SubtitleName,
        context.Role, characterId, profile.Portrait, fallback);

    private static bool Matches(string configured, string actual) => configured == "*" ||
        (!string.IsNullOrWhiteSpace(actual) && string.Equals(configured, actual, StringComparison.OrdinalIgnoreCase));

    private static int Specificity(string configured, string actual) => Matches(configured, actual) && configured != "*" ? 1 : 0;

    private static bool IsAlienRole(VoiceSpeakerRole role) =>
        role is VoiceSpeakerRole.AlienDiplomat or VoiceSpeakerRole.AlienScientist or VoiceSpeakerRole.AlienCommander;

    private static bool IsNonHumanSpecies(string species) => !string.IsNullOrWhiteSpace(species) &&
        !string.Equals(species, "human", StringComparison.OrdinalIgnoreCase) &&
        !string.Equals(species, "terran_baseline", StringComparison.OrdinalIgnoreCase);
}
