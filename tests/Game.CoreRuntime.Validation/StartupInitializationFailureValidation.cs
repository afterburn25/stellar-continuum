using System;
using Game.Presentation;

namespace Game.CoreRuntime.Validation;

internal static class StartupInitializationFailureValidation
{
    internal static void Run()
    {
        Exception failure;
        try
        {
            ThrowNestedFailure();
            throw new InvalidOperationException("fixture did not throw");
        }
        catch (Exception exception)
        {
            failure = exception;
        }

        var diagnostic = StartupInitializationFailure.BuildDiagnostic(
            failure, 20260911, @"C:\profile\saves\autosave.json", @"C:\profile\saves\developer-campaign.json", "Player");
        Require(diagnostic.Contains("startupSeed=20260911", StringComparison.Ordinal), "startup seed missing");
        Require(diagnostic.Contains(@"playerSave=C:\profile\saves\autosave.json", StringComparison.Ordinal) &&
                diagnostic.Contains(@"developerSave=C:\profile\saves\developer-campaign.json", StringComparison.Ordinal),
            "relevant save paths missing");
        Require(diagnostic.Contains("outer initialization failure", StringComparison.Ordinal) &&
                diagnostic.Contains("inner persistence failure", StringComparison.Ordinal) &&
                diagnostic.Contains(nameof(ThrowNestedFailure), StringComparison.Ordinal),
            "full exception, inner exception, or stack trace missing");
    }

    private static void ThrowNestedFailure()
    {
        try { throw new InvalidOperationException("inner persistence failure"); }
        catch (Exception inner) { throw new ApplicationException("outer initialization failure", inner); }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
