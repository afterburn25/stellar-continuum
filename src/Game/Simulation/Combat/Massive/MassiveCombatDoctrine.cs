using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;

namespace Game.Simulation.Combat.Massive;

/// <summary>Pure observer-safe tactical recommendations. AI and player automation feed these
/// commands through the same authority-checked engine entry point.</summary>
public static class MassiveCombatDoctrine
{
    public static IReadOnlyList<MassiveCombatOrder> Decide(MassiveCombatSnapshot snapshot, int civilizationId)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        var own = snapshot.Formations.Where(x => x.CivilizationId == civilizationId && x.IsExact).OrderBy(x => x.FormationId).ToArray();
        var hostile = snapshot.Formations.Where(x => x.CivilizationId != civilizationId).OrderBy(x => x.FormationId).ToArray();
        var orders = new List<MassiveCombatOrder>();
        var ownInterdictor = own.FirstOrDefault(x => x.IsInterdicting);
        foreach (var formation in own)
        {
            if (formation.IsWarpBlocked)
            {
                var source = hostile.Where(x => x.IsInterdicting).OrderBy(x => Vector2.DistanceSquared(x.Position, formation.Position)).FirstOrDefault();
                orders.Add(source is null
                    ? new(formation.FormationId, MassiveCombatOrderType.EmergencyRetreat, Objective: formation.Position + SafeAway(formation.Position) * 2_000, Shape: MassiveFormationShape.RetreatColumn)
                    : new(formation.FormationId, MassiveCombatOrderType.Breakout, source.FormationId, Shape: MassiveFormationShape.Breakout));
                continue;
            }
            if (ownInterdictor is not null && formation.FormationId != ownInterdictor.FormationId && formation.Shape is MassiveFormationShape.Screen or MassiveFormationShape.Escort)
            {
                orders.Add(new(formation.FormationId, MassiveCombatOrderType.ProtectCriticalAsset, ownInterdictor.FormationId, Shape: MassiveFormationShape.Escort));
                continue;
            }
            var target = hostile.OrderBy(x => Vector2.DistanceSquared(x.Position, formation.Position)).ThenBy(x => x.FormationId).FirstOrDefault();
            if (target is not null) orders.Add(new(formation.FormationId, MassiveCombatOrderType.Engage, target.FormationId));
        }
        return orders;
    }

    private static Vector2 SafeAway(Vector2 position) => position.LengthSquared() > .01f ? Vector2.Normalize(position) : Vector2.UnitX;
}
