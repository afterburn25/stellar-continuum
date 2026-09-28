using System.Runtime.CompilerServices;
using Game.Simulation.Species;

internal static class DemographicPressureChecks
{
    [ModuleInitializer]
    internal static void Initialize()
    {
        Run();
        Console.WriteLine("PASS: species demographic pressure");
    }

    public static void Run()
    {
        var terran = SpeciesDemographicPressureEvaluator.Evaluate(SpeciesCatalog.TerranBaselineId);
        RequireClose(terran.GenerationPaceFactor, 1.0, "Terran generation pace was not normalized to 1");
        RequireClose(terran.ReproductiveEventThroughputFactor, 1.0, "Terran reproductive throughput was not normalized to 1");
        RequireClose(terran.MaturityPaceFactor, 1.0, "Terran maturity pace was not normalized to 1");
        RequireClose(terran.IntrinsicGrowthPaceFactor, 1.0, "Terran intrinsic demographic pace was not normalized to 1");

        var pelagic = SpeciesDemographicPressureEvaluator.Evaluate(SpeciesCatalog.PelagicHighPressureId);
        var highGravity = SpeciesDemographicPressureEvaluator.Evaluate(SpeciesCatalog.CompactHighGravityId);
        var cryogenic = SpeciesDemographicPressureEvaluator.Evaluate(SpeciesCatalog.CryogenicHydrocarbonId);

        foreach (var pressure in new[] { terran, pelagic, highGravity, cryogenic })
        {
            Require(double.IsFinite(pressure.IntrinsicGrowthPaceFactor), $"{pressure.SpeciesId} demographic pace was non-finite");
            Require(
                pressure.IntrinsicGrowthPaceFactor >= SpeciesDemographicPressureEvaluator.MinimumIntrinsicGrowthPace &&
                pressure.IntrinsicGrowthPaceFactor <= SpeciesDemographicPressureEvaluator.MaximumIntrinsicGrowthPace,
                $"{pressure.SpeciesId} demographic pace escaped its safety bounds");

            var repeated = SpeciesDemographicPressureEvaluator.Evaluate(pressure.SpeciesId);
            RequireClose(
                repeated.IntrinsicGrowthPaceFactor,
                pressure.IntrinsicGrowthPaceFactor,
                $"{pressure.SpeciesId} demographic pace was not deterministic");
        }

        Require(
            cryogenic.IntrinsicGrowthPaceFactor < highGravity.IntrinsicGrowthPaceFactor &&
            cryogenic.IntrinsicGrowthPaceFactor < pelagic.IntrinsicGrowthPaceFactor,
            "long-generation cryogenic biology did not produce slower population turnover than the shorter-generation proving species");
        Require(
            pelagic.IntrinsicGrowthPaceFactor < terran.IntrinsicGrowthPaceFactor,
            "Pelagic life-history timing did not reduce demographic pace relative to the Terran reference");
        Require(
            highGravity.IntrinsicGrowthPaceFactor < terran.IntrinsicGrowthPaceFactor,
            "high-gravity life-history timing did not reduce demographic pace relative to the Terran reference");

        var expectedCryogenic = Math.Cbrt(
            (28.0 / 82.0) *
            ((1.5 / 8.0) / (1.05 / 1.5)) *
            (18.0 / 55.0));
        RequireClose(
            cryogenic.IntrinsicGrowthPaceFactor,
            expectedCryogenic,
            "cryogenic demographic pace did not emerge from the authored life-history dimensions");
    }

    private static void RequireClose(double actual, double expected, string message)
    {
        if (Math.Abs(actual - expected) > 0.000000001)
            throw new InvalidOperationException($"{message}: expected {expected}, got {actual}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
