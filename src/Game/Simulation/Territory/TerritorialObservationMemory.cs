using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Territory;

/// <summary>Fog-of-war retention for observer presentation. It records only a currently
/// visible runtime report and never changes the derived territorial simulation.</summary>
public static class TerritorialObservationMemory
{
    public static bool CanObserve(GalaxyState galaxy, int observer, int owner, int system) =>
        owner == observer || galaxy.Knowledge.IsCivilizationKnown(observer, owner) &&
        galaxy.Knowledge.IsSystemFullySurveyed(observer, system);

    public static void Remember(GalaxyState galaxy, int observer, int system, int owner, TerritorialControlStatus status)
    {
        if (status is not (TerritorialControlStatus.Controlled or TerritorialControlStatus.Dominant) ||
            !CanObserve(galaxy, observer, owner, system)) return;
        var observations = galaxy.Territory?.Observations;
        if (observations is null) return;
        observations.RemoveAll(item => item.ObserverCivilizationId == observer && item.SystemId == system);
        observations.Add(new(observer, system, owner, status));
    }

    public static TerritorialObservation? Recall(GalaxyState galaxy, int observer, int system) => galaxy.Territory?.Observations
        .FirstOrDefault(item => item.ObserverCivilizationId == observer && item.SystemId == system);

    public static void Forget(GalaxyState galaxy, int observer, int system) =>
        galaxy.Territory?.Observations.RemoveAll(item => item.ObserverCivilizationId == observer && item.SystemId == system);
}
