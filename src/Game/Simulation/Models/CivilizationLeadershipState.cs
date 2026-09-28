using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Simulation.Models;

/// <summary>Current office holders and cosmetic character metadata. No audio or renderer dependency.
/// Elections, appointments and succession can all replace the same current assignment.</summary>
public sealed record CivilizationCharacter(string Id, string DisplayName, string? VoiceProfileId = null,
    string? Portrait = null);

public sealed class CivilizationLeadershipState : IEquatable<CivilizationLeadershipState>
{
    private readonly Dictionary<string, CivilizationCharacter> _offices = new(StringComparer.Ordinal);
    public IReadOnlyDictionary<string, CivilizationCharacter> Offices => _offices;
    public CivilizationCharacter? Current(string office) => _offices.GetValueOrDefault(office);
    public CivilizationCharacter? Find(string characterId) => _offices.Values.FirstOrDefault(c => c.Id == characterId);

    public void Assign(string office, CivilizationCharacter character)
    {
        ArgumentNullException.ThrowIfNull(character);
        if (string.IsNullOrWhiteSpace(office) || office.Length > 64 ||
            string.IsNullOrWhiteSpace(character.Id) || character.Id.Length > 128 ||
            string.IsNullOrWhiteSpace(character.DisplayName) || character.DisplayName.Length > 160 ||
            character.VoiceProfileId?.Length > 128 || character.Portrait?.Length > 512)
            throw new ArgumentException("An office assignment contains invalid character metadata.");
        if (!_offices.ContainsKey(office) && _offices.Count >= 32)
            throw new InvalidOperationException("A civilization cannot hold more than 32 named offices.");
        _offices[office] = character;
    }

    public bool Vacate(string office) => _offices.Remove(office);

    public bool Equals(CivilizationLeadershipState? other) => other is not null &&
        _offices.Count == other._offices.Count && _offices.All(entry =>
            other._offices.TryGetValue(entry.Key, out var character) && character == entry.Value);
    public override bool Equals(object? obj) => obj is CivilizationLeadershipState other && Equals(other);
    public override int GetHashCode()
    {
        var hash = new HashCode();
        foreach (var entry in _offices.OrderBy(p => p.Key, StringComparer.Ordinal)) { hash.Add(entry.Key); hash.Add(entry.Value); }
        return hash.ToHashCode();
    }

    public static CivilizationLeadershipState Restore(IReadOnlyDictionary<string, CivilizationCharacter>? offices)
    {
        var result = new CivilizationLeadershipState();
        if (offices is not null)
            foreach (var entry in offices.OrderBy(p => p.Key, StringComparer.Ordinal)) result.Assign(entry.Key, entry.Value);
        return result;
    }

    public static CivilizationLeadershipState CreateFoundingRoster(int civilizationId, bool human)
    {
        var result = new CivilizationLeadershipState();
        var founding = new (string Office, string Name)[]
        {
            ("FleetCommander", human ? "Commander Elena Voss" : "Fleet Commander"),
            ("ChiefScientist", human ? "Dr. Amara Chen" : "Chief Scientist"),
            ("Diplomat", human ? "Ambassador Mara Okafor" : "Diplomatic Envoy"),
            ("Governor", human ? "Governor Elias Ward" : "Governor"),
            ("EconomicAdvisor", "Economic Advisor"), ("OperationsOfficer", "Operations Officer"),
            ("ExpeditionCommander", "Expedition Commander"),
        };
        foreach (var (office, name) in founding)
            result.Assign(office, new CivilizationCharacter($"civ-{civilizationId}:{office}:founder", name));
        return result;
    }
}
