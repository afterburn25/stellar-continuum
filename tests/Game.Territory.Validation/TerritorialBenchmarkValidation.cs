using System.Diagnostics;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Territory;
using Game.Validation;

namespace Game.Territory.Validation;

internal static class TerritorialBenchmarkValidation
{
    [RegressionCheck]
    private static void GeneratedGalaxyScaleProfilesAreDeterministic()
    {
        foreach (var systems in new[] { 100, 500, 1000, 2500 })
        {
            var settings = new GalaxyGenerationSettings
            {
                // The generator deliberately bounds seed placement by viable systems. These are
                // dense enough to exercise competing sources without requesting impossible starts.
                SystemCount = systems, PreWarpCivilizationCount = Math.Clamp(systems / 100, 4, 12), AncientCivilizationCount = 0, Radius = 500 + systems / 3f,
            };
            var first = new GalaxyGenerator().Generate(0x5445_5252_0000L + systems, settings);
            var second = new GalaxyGenerator().Generate(0x5445_5252_0000L + systems, settings);
            Require(first.Colonies.Count >= settings.PreWarpCivilizationCount, $"{systems}-system profile had unrealistically few permanent sources");
            GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
            var retainedBefore = GC.GetTotalMemory(forceFullCollection: true);
            var allocatedBefore = GC.GetTotalAllocatedBytes(precise: true); var stopwatch = Stopwatch.StartNew();
            var runtime = TerritorialRuntime.Initialize(first); stopwatch.Stop();
            var coldMilliseconds = stopwatch.Elapsed.TotalMilliseconds;
            var allocated = GC.GetTotalAllocatedBytes(precise: true) - allocatedBefore;
            var warm = new List<double>();
            for (var run = 0; run < 7; run++)
            {
                stopwatch.Restart(); runtime.Recompute(first); stopwatch.Stop(); warm.Add(stopwatch.Elapsed.TotalMilliseconds);
            }
            warm.Sort(); var warmP95 = warm[(int)Math.Ceiling(warm.Count * .95) - 1];
            var retained = GC.GetTotalMemory(forceFullCollection: true) - retainedBefore;
            var repeat = TerritorialRuntime.Initialize(second);
            Require(runtime.Systems.Count == systems && repeat.Systems.Count == systems, $"{systems}-system profile omitted systems from territorial snapshot");
            Require(Fingerprint(runtime) == Fingerprint(repeat), $"{systems}-system profile produced nondeterministic territorial scores");
            Console.WriteLine($"BENCH territory systems={systems} civilizations={first.Civilizations.Count} colonies={first.Colonies.Count} sources={first.Colonies.Count + (first.Territory?.Installations.Count ?? 0)} cold_ms={coldMilliseconds:0.###} warm_p95_ms={warmP95:0.###} allocated_bytes={allocated} retained_bytes={retained}");
        }
        BusyFiveHundredSourceProfile();
    }

    private static void BusyFiveHundredSourceProfile()
    {
        var galaxy = new GalaxyGenerator().Generate(0x4255_5359_3530_30L, new GalaxyGenerationSettings
        {
            SystemCount = 500, PreWarpCivilizationCount = 8, AncientCivilizationCount = 0, Radius = 670,
        });
        var sources = galaxy.Colonies.Count;
        foreach (var system in galaxy.Systems.Where(system => galaxy.Colonies.All(colony => colony.SystemId != system.Id)).Take(100 - sources))
        {
            var owner = galaxy.Civilizations[sources % galaxy.Civilizations.Count];
            galaxy.Colonies.Add(new ColonyState
            {
                Id = galaxy.Colonies.Max(colony => colony.Id) + 1, CivilizationId = owner.Id, SystemId = system.Id,
                Name = $"Busy source {sources}", PopulationMillions = 600, Infrastructure = 4, Stability = 1, SurfaceHubLevel = 3,
            });
            sources++;
        }
        var runtime = TerritorialRuntime.Initialize(galaxy); var warm = new List<double>(); var stopwatch = new Stopwatch();
        for (var run = 0; run < 7; run++) { stopwatch.Restart(); runtime.Recompute(galaxy); stopwatch.Stop(); warm.Add(stopwatch.Elapsed.TotalMilliseconds); }
        warm.Sort(); var p95 = warm[(int)Math.Ceiling(warm.Count * .95) - 1];
        Require(sources >= 100, "busy 500-system benchmark did not create its 100 represented permanent sources");
        Console.WriteLine($"BENCH territory_busy systems=500 civilizations={galaxy.Civilizations.Count} colonies={galaxy.Colonies.Count} sources={sources} warm_p95_ms={p95:0.###}");
    }

    private static string Fingerprint(TerritorialRuntime runtime) => string.Join(";", runtime.Systems.OrderBy(p => p.Key).Select(p =>
        $"{p.Key}:{p.Value.Status}:{p.Value.ControllerId}:{string.Join(',', p.Value.Civilizations.Select(c => $"{c.CivilizationId}/{c.Political:0.000000}/{c.Administration:0.000000}/{c.Supply:0.000000}"))}"));
    private static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
}
