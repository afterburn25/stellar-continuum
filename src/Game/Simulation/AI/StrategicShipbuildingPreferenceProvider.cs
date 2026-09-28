using System;
using System.Collections.Generic;
using Game.Simulation.Shipbuilding;

namespace Game.Simulation.AI;

/// <summary>
/// Read-only bridge from the latest scheduled Civilization strategic intent into Shipbuilding's
/// advisory role-preference contract. It never starts orders, spends Industry, or chooses mission
/// destinations.
/// </summary>
public sealed class StrategicShipbuildingPreferenceProvider : IShipbuildingStrategicPreferenceView
{
    private readonly Dictionary<int, ShipbuildingStrategicPreference> _preferences = new();

    public int PublishedPreferenceCount => _preferences.Count;

    public void Publish(CivilizationStrategicReview review)
    {
        ArgumentNullException.ThrowIfNull(review);
        Publish(review.Intent);
    }

    public void Publish(CivilizationStrategicIntent intent)
    {
        ArgumentNullException.ThrowIfNull(intent);
        _preferences[intent.CivilizationId] = new ShipbuildingStrategicPreference(
            intent.PreferredNewFleetRole,
            intent.DeferNewColonization);
    }

    public ShipbuildingStrategicPreference GetPreference(int civilizationId) =>
        _preferences.TryGetValue(civilizationId, out var preference)
            ? preference
            : ShipbuildingStrategicPreference.None;

    public void Remove(int civilizationId) => _preferences.Remove(civilizationId);

    public void Clear() => _preferences.Clear();
}
