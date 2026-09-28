using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Colonization;

/// <summary>
/// Reconstructs same-civilization colony mission reservations from live fleet state.
/// It never includes foreign missions, preventing hidden opponent destination intent from
/// affecting player/AI eligibility. No reservation state is persisted independently.
/// </summary>
public static class FriendlyColonyMissionReservations
{
    public static IReadOnlyDictionary<int, int> BuildBySystem(
        GalaxyState galaxy,
        FleetState requestingFleet)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(requestingFleet);

        return galaxy.Fleets
            .Where(other =>
                other.Id != requestingFleet.Id &&
                other.IsActive &&
                other.CivilizationId == requestingFleet.CivilizationId &&
                other.Role == FleetRole.Colony &&
                other.EmbarkedPopulationMillions > 0.0)
            .Select(other => new
            {
                Fleet = other,
                SystemId = ResolveReservationSystemId(galaxy, other),
            })
            .Where(entry => entry.SystemId is not null)
            .OrderBy(entry => entry.Fleet.Id)
            .GroupBy(entry => entry.SystemId!.Value)
            .ToDictionary(
                group => group.Key,
                group => group.First().Fleet.Id);
    }

    public static bool TryGetReservingFleetId(
        GalaxyState galaxy,
        FleetState requestingFleet,
        int systemId,
        out int reservingFleetId)
    {
        var reservations = BuildBySystem(galaxy, requestingFleet);
        return reservations.TryGetValue(systemId, out reservingFleetId);
    }

    private static int? ResolveReservationSystemId(GalaxyState galaxy, FleetState fleet)
    {
        // A committed remote mission remains a reservation until explicitly retargeted,
        // destroyed/deactivated, or the destination is cleared at arrival.
        if (fleet.DestinationSystemId is int destinationSystemId)
            return destinationSystemId;

        if (fleet.CurrentSystemId is not int currentSystemId)
            return null;

        // An already-colonized system is non-orderable independently, so an idle colony ship
        // parked at a friendly colony must not create a misleading "mission reservation" there.
        if (galaxy.Colonies.Any(colony => colony.SystemId == currentSystemId))
            return null;

        // Local/arrived reservations represent settlement intent only when the observer has the
        // required full survey and the embarked passenger species can actually found somewhere
        // in the unoccupied system. This prevents migrated/body-less or otherwise stranded fleets
        // from deadlocking a system for another friendly population that really can settle it.
        if (!galaxy.Knowledge.IsSystemFullySurveyed(fleet.CivilizationId, currentSystemId))
            return null;

        var speciesId = fleet.EmbarkedPopulationSpeciesId;
        if (string.IsNullOrWhiteSpace(speciesId) || !SpeciesCatalog.TryGet(speciesId, out _))
            return null;

        var habitability = new SpeciesPlanetaryHabitabilityEvaluator();
        if (fleet.DestinationPlanetaryBodyId is int explicitBodyId)
        {
            var explicitBody = galaxy.PlanetaryBodies.FirstOrDefault(body =>
                body.Id == explicitBodyId && body.SystemId == currentSystemId);
            return explicitBody is not null && habitability.Evaluate(explicitBody, speciesId).CanFoundCurrentColony
                ? currentSystemId
                : null;
        }

        return galaxy.PlanetaryBodies.Any(body =>
                body.SystemId == currentSystemId &&
                habitability.Evaluate(body, speciesId).CanFoundCurrentColony)
            ? currentSystemId
            : null;
    }
}
