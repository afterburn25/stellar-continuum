using System;
using System.Linq;
using Game.Simulation;
using Game.Simulation.Combat;
using Game.Simulation.Diplomacy;
using Game.Simulation.Models;

namespace Game.Presentation;

public partial class Main
{
    /// <summary>Creates a disposable two-sided encounter only for the maintained native acceptance process.</summary>
    public bool UiPrepareMassiveCombatMenuCapture()
    {
        if (!string.Equals(Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS"), "massive-combat-menu", StringComparison.Ordinal))
            return false;

        var system = _galaxy.Systems.First(x => x.Id == PlayerCivilization.HomeSystemId);
        var hostile = _galaxy.Civilizations.Where(x => x.Id != _galaxy.PlayerCivilizationId).OrderBy(x => x.Id).First();
        var nextId = _galaxy.Fleets.Select(x => x.Id).DefaultIfEmpty(0).Max() + 1;
        var profile = CombatProfileRegistry.Get(CombatProfileIds.PatrolCorvetteMk1);
        FleetState Vessel(int id, int civilizationId, string name) => new()
        {
            Id = id,
            CivilizationId = civilizationId,
            Name = name,
            Role = FleetRole.Military,
            Position = system.Position,
            CurrentSystemId = system.Id,
            Combat = CombatProfileRegistry.CreateInitialState(profile.Id, FleetRole.Military),
        };

        // Persist the same authoritative hostility that drives the encounter. A capture-only
        // hostility adapter would disappear across the real save/load path this fixture verifies.
        var tick = DiplomacyCampaignClock.FromSimulationDays(_clock.SimulationDays);
        var diplomacy = new DiplomacySimulation(_diplomacyState);
        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            _galaxy.PlayerCivilizationId,
            $"capture-{hostile.Id}",
            hostile.Id,
            tick,
            system.Id,
            ContactAwareness.Identified,
            ContactCondition.Active,
            false,
            1));
        diplomacy.DeclareWar(_galaxy.PlayerCivilizationId, hostile.Id, tick);

        _galaxy.ActiveCombatEncounter = null;
        _galaxy.Fleets.Add(Vessel(nextId, _galaxy.PlayerCivilizationId, "Menu Acceptance Picket"));
        _galaxy.Fleets.Add(Vessel(nextId + 1, hostile.Id, "Menu Acceptance Contact"));
        _massiveCombat = new CampaignMassiveCombat(new DiplomacyCombatHostilityView(_diplomacyState));
        return BeginMassiveCombat(nextId).Accepted;
    }
}
