using System;
using System.Linq;
using Game.Simulation.Diplomacy;
using Game.Simulation.Models;

namespace Game.Simulation.Territory;

/// <summary>Small adapter into existing access, claim and relationship authority. Never invents first contact or war.</summary>
public static class TerritorialDiplomacyBridge
{
    public static void Bind(GalaxyState galaxy, DiplomacyState state)
    {
        var runtime = TerritorialRuntime.Initialize(galaxy);
        if (ReferenceEquals(runtime.Diplomacy, state)) return;
        runtime.Diplomacy = state; runtime.Recompute(galaxy);
    }
    public static int Review(GalaxyState galaxy, DiplomacyState state, long tick)
    {
        Bind(galaxy, state);
        var territorial = galaxy.Territory!;
        if (territorial.ElapsedDays < territorial.NextDiplomaticReviewDay) return 0;
        territorial.NextDiplomaticReviewDay = territorial.ElapsedDays + 30;
        var runtime = TerritorialRuntime.Peek(galaxy)!;
        var snapshots = state.Snapshot();
        var simulation = new DiplomacySimulation(state);
        var count = 0;
        foreach (var region in runtime.Systems.Values.Where(s => s.Status == TerritorialControlStatus.Contested))
        {
            var contenders = region.Civilizations.Where(c => c.Share >= TerritorialBalance.ContestedMinimumShare).Take(2).ToArray();
            if (contenders.Length < 2) continue;
            var first = contenders[0].CivilizationId; var second = contenders[1].CivilizationId;
            // Both must have legitimately observed the contested place and identified the other.
            if (!galaxy.Knowledge.IsSystemFullySurveyed(first, region.SystemId) || !galaxy.Knowledge.IsSystemFullySurveyed(second, region.SystemId) ||
                state.GetContact(first, second)?.Awareness < ContactAwareness.Identified || state.GetContact(first, second) is null ||
                state.GetContact(second, first)?.Awareness < ContactAwareness.Identified || state.GetContact(second, first) is null) continue;
            var claims = snapshots.Claims.Where(c => c.Active && c.SystemId == region.SystemId && (c.ClaimantCivilizationId == first || c.ClaimantCivilizationId == second)).ToArray();
            if (claims.Length < 2 || !claims.All(c => c.KnownToCivilizationIds.Contains(first) && c.KnownToCivilizationIds.Contains(second))) continue;
            // Cooperative/access agreements allow shared frontier influence without recurring friction.
            if (state.GetAccessPermission(first, second) == AccessPermission.Granted && state.GetAccessPermission(second, first) == AccessPermission.Granted) continue;
            var pair = (First: Math.Min(first, second), Second: Math.Max(first, second));
            if (territorial.BorderIncidents.Any(i => i.FirstCivilizationId == pair.First && i.SecondCivilizationId == pair.Second &&
                territorial.ElapsedDays - i.LastDay < 180)) continue;
            simulation.ApplyRelationshipImpact(first, second, new RelationshipImpact(-.01, .015, 0, 0, -.01, .03,
                $"Competing territorial claims in surveyed system {region.SystemId} strain border relations."), tick);
            territorial.BorderIncidents.RemoveAll(i => i.FirstCivilizationId == pair.First && i.SecondCivilizationId == pair.Second);
            territorial.BorderIncidents.Add(new(pair.First, pair.Second, territorial.ElapsedDays));
            count++;
        }
        return count;
    }
}
