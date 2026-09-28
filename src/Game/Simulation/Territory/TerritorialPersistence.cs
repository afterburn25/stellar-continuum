using System;
using System.IO;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Territory;

public static class TerritorialPersistence
{
    public static TerritorialState? Capture(GalaxyState galaxy)
    {
        if (galaxy.Territory is not { } state) return null;
        Validate(galaxy);
        return new TerritorialState { SchemaVersion = state.SchemaVersion, ElapsedDays = state.ElapsedDays,
            NextReviewDay = state.NextReviewDay,
            NextDiplomaticReviewDay = state.NextDiplomaticReviewDay, BorderIncidents = state.BorderIncidents.ToList(),
            Observations = state.Observations.ToList(),
            Installations = state.Installations.Select(i => new TerritorialInstallation {
                Id = i.Id, CivilizationId = i.CivilizationId, SystemId = i.SystemId, Kind = i.Kind,
                BuilderFleetId = i.BuilderFleetId, CompletedDays = i.CompletedDays, RequiredDays = i.RequiredDays,
                PaidCredits = i.PaidCredits, PaidIndustry = i.PaidIndustry }).ToList(),
            Expeditions = state.Expeditions.ToList() };
    }
    public static void Validate(GalaxyState galaxy)
    {
        if (galaxy.Territory is not { } state) return;
        if (state.SchemaVersion != 1 || !Valid(state.ElapsedDays) || !Valid(state.NextReviewDay) || !Valid(state.NextDiplomaticReviewDay) || state.BorderIncidents is null || state.Observations is null ||
            state.Installations is null || state.Expeditions is null ||
            state.Installations.GroupBy(i => i.Id).Any(g => g.Count() != 1) ||
            state.Installations.GroupBy(i => (i.CivilizationId, i.SystemId, i.Kind)).Any(g => g.Count() != 1) ||
            state.Expeditions.GroupBy(e => e.FleetId).Any(g => g.Count() != 1))
            throw new InvalidDataException("Invalid territorial state: version, clock or duplicate source/expedition.");
        foreach (var incident in state.BorderIncidents)
            if (incident.FirstCivilizationId >= incident.SecondCivilizationId || !Valid(incident.LastDay) ||
                !galaxy.Civilizations.Any(c => c.Id == incident.FirstCivilizationId) || !galaxy.Civilizations.Any(c => c.Id == incident.SecondCivilizationId))
                throw new InvalidDataException("Invalid territorial border-incident memory.");
        if (state.Observations.GroupBy(item => (item.ObserverCivilizationId, item.SystemId)).Any(group => group.Count() != 1))
            throw new InvalidDataException("Invalid duplicate territorial observation memory.");
        foreach (var observation in state.Observations)
            if (!Enum.IsDefined(observation.Status) || observation.Status is not (TerritorialControlStatus.Controlled or TerritorialControlStatus.Dominant) ||
                !galaxy.Civilizations.Any(c => c.Id == observation.ObserverCivilizationId) || !galaxy.Civilizations.Any(c => c.Id == observation.CivilizationId) ||
                !galaxy.Systems.Any(s => s.Id == observation.SystemId))
                throw new InvalidDataException("Invalid territorial observer memory.");
        foreach (var site in state.Installations)
        {
            if (site.Id < 1 || !Enum.IsDefined(site.Kind) || !galaxy.Civilizations.Any(c => c.Id == site.CivilizationId) ||
                !galaxy.Systems.Any(s => s.Id == site.SystemId) || !Valid(site.RequiredDays) || site.RequiredDays <= 0 ||
                !Valid(site.CompletedDays) || site.CompletedDays > site.RequiredDays || !Valid(site.PaidCredits) || !Valid(site.PaidIndustry))
                throw new InvalidDataException($"Invalid territorial installation {site.Id}: owner, system, duration or paid capital.");
            var definition = TerritorialBalance.Definition(site.Kind);
            if (site.PaidCredits > definition.Credits + .0001 || site.PaidIndustry > definition.Industry + .0001)
                throw new InvalidDataException($"Territorial installation {site.Id} has an invalid refundable authorization.");
            // A destroyed builder is valid; another logistics ship can resume the site.
        }
        foreach (var expedition in state.Expeditions)
            if (!galaxy.Fleets.Any(f => f.Id == expedition.FleetId) || !galaxy.PlanetaryBodies.Any(b => b.Id == expedition.BodyId) ||
                !Valid(expedition.PaidCredits) || !Valid(expedition.RequiredDays) || expedition.RequiredDays <= 0)
                throw new InvalidDataException($"Invalid territorial expedition for fleet {expedition.FleetId}.");
    }
    private static bool Valid(double value) => double.IsFinite(value) && value >= 0;
}
