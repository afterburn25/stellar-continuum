using Game.Validation;

namespace Game.Territory.Validation;

internal static class Program
{
    private static int Main()
    {
        var failures = RegressionRunner.Run(typeof(Program).Assembly);
        Console.WriteLine($"Territorial influence validation: {RegressionRunner.Count - failures}/{RegressionRunner.Count} passed.");
        Console.WriteLine("Profiles: anchored-line regression; competitive compact/rapid long-run; generated 100, 500, 1000, 2500-system benchmarks.");
        return failures == 0 ? 0 : 1;
    }
}
