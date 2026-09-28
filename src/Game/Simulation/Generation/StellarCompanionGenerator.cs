using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Generation;

/// <summary>
/// New-campaign stellar catalog detail. A separate random stream preserves all existing
/// coordinates, primary spectral quotas and planetary generation. Save loading never calls
/// this generator: an absent companion field means the original single-star catalog.
/// Multiplicity is catalog metadata, not a new gravitational or habitability simulation.
/// </summary>
public static class StellarCompanionGenerator
{
    public static void Apply(long seed, IList<StarSystemState> systems)
    {
        var eligible = systems.Select((system, index) => (system, index))
            .Where(item => item.system.CatalogPresetId is null && item.system.StellarCatalogId is null && item.system.StellarClass is
                StellarPrimaryClass.MRedDwarf or StellarPrimaryClass.KOrangeDwarf or
                StellarPrimaryClass.GYellowDwarf or StellarPrimaryClass.FYellowWhiteDwarf or
                StellarPrimaryClass.AWhiteStar or StellarPrimaryClass.HotBlueStar or StellarPrimaryClass.Giant)
            .OrderBy(item => item.system.Id).Select(item => item.index).ToArray();
        var random = new Random(unchecked((int)(seed ^ (seed >> 32) ^ 0x434F4D50)));
        for (var index = eligible.Length - 1; index > 0; index--)
        {
            var swap = random.Next(index + 1);
            (eligible[index], eligible[swap]) = (eligible[swap], eligible[index]);
        }

        // Bounded initial game tuning among ordinary, non-authored stars, not an observed
        // astronomical census. Compact remnants/protostars keep their existing single model.
        var binaryCount = (int)Math.Round(eligible.Length * .20, MidpointRounding.AwayFromZero);
        var tripleCount = (int)Math.Round(eligible.Length * .05, MidpointRounding.AwayFromZero);
        for (var index = 0; index < eligible.Length; index++)
        {
            var slot = eligible[index];
            var system = systems[slot];
            var multiple = index < binaryCount + tripleCount;
            systems[slot] = system with
            {
                SecondaryStellarClass = multiple ? Companion(system.StellarClass!.Value, random) : null,
                TertiaryStellarClass = index < tripleCount ? Companion(system.StellarClass!.Value, random) : null,
            };
        }
    }

    private static StellarPrimaryClass Companion(StellarPrimaryClass primary, Random random)
    {
        // Keep the dominant primary's palette legible; lower-luminosity companions are common.
        var choices = primary switch
        {
            StellarPrimaryClass.MRedDwarf => 1,
            StellarPrimaryClass.KOrangeDwarf => 2,
            _ => 3,
        };
        var choice = random.Next(10);
        return choices >= 3 && choice >= 9 ? StellarPrimaryClass.GYellowDwarf
            : choices >= 2 && choice >= 6 ? StellarPrimaryClass.KOrangeDwarf
            : StellarPrimaryClass.MRedDwarf;
    }
}
