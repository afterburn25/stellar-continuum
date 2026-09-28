using Game.Simulation;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Territory;
using Game.Validation;

namespace Game.Territory.Validation;

/// <summary>Observational campaign evidence: it exercises normal coordinator ordering and
/// records actual strategic choices without requiring a particular random expansion outcome.</summary>
internal static class TerritorialCampaignPlaytestValidation
{
    [RegressionCheck]
    private static void CoordinatedCampaignProfilesRemainObservable()
    {
        Run("compact-neighbors", 36, 3, 0x5445_5354_31L, 90);
        Run("frontier-100", 100, 4, 0x5445_5354_32L, 90);
        Run("frontier-500", 500, 8, 0x5445_5354_33L, 45);
    }

    private static void Run(string profile, int systems, int civilizations, long seed, int days)
    {
        var galaxy = new GalaxyGenerator().Generate(seed, new GalaxyGenerationSettings
        {
            SystemCount = systems, PreWarpCivilizationCount = civilizations, AncientCivilizationCount = 0, Radius = Math.Max(380, systems * 1.4f),
        });
        foreach (var civilization in galaxy.Civilizations.ToArray())
        {
            var index = galaxy.Civilizations.ToList().FindIndex(item => item.Id == civilization.Id);
            galaxy.Civilizations[index] = civilization with { DevelopmentStage = CivilizationDevelopmentStage.WarpCapable, ExpansionAllowed = true };
            galaxy.Technologies.Single(technology => technology.CivilizationId == civilization.Id).CompletedTechnologyIds.Add("orbital_industry");
            galaxy.Fleets.Add(TerritoryScenario.Fleet(galaxy, civilization.Id, civilization.HomeSystemId, FleetRole.Logistics));
        }
        var runtime = TerritorialRuntime.Initialize(galaxy);
        var beforeCredits = galaxy.Economies.Sum(economy => economy.Credits);
        var decisions = galaxy.Civilizations.Select(civilization => TerritorialStrategicPlanner.Assess(galaxy, civilization.Id)).ToArray();
        var coordinator = new GalaxySimulationStepCoordinator();
        for (var day = 0; day < days; day++) coordinator.Advance(galaxy, 1);
        var installationCount = galaxy.Territory!.Installations.Count;
        var completed = galaxy.Territory.Installations.Count(site => site.IsComplete);
        var started = galaxy.Territory.Installations.Count(site => !site.IsComplete);
        var decisionText = string.Join(";", decisions.Select(decision => $"{decision.CivilizationId}:{decision.Kind?.ToString() ?? "hold"}:{decision.NeedsLogisticsShip}"));
        Console.WriteLine($"PLAYTEST territory profile={profile} systems={systems} days={days} decisions={decisionText} installations_started={installationCount} completed={completed} active={started} colonies={galaxy.Colonies.Count} treasury_before={beforeCredits:0.##} treasury_after={galaxy.Economies.Sum(e => e.Credits):0.##}");
        Require(runtime.Systems.Count == systems && galaxy.Economies.All(economy => double.IsFinite(economy.Credits) && double.IsFinite(economy.Industry)),
            $"{profile} coordinator campaign produced incomplete territory snapshot or non-finite treasury state");
    }

    private static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
}
