using System;

namespace Game.Presentation;

public static class StartupInitializationFailure
{
    public static string BuildDiagnostic(
        Exception exception,
        long? startupSeed,
        string playerSavePath,
        string developerSavePath,
        string requestedMode)
    {
        ArgumentNullException.ThrowIfNull(exception);
        return $"Integrated campaign initialization failed. mode={requestedMode}; " +
            $"startupSeed={(startupSeed?.ToString(System.Globalization.CultureInfo.InvariantCulture) ?? "unavailable")}; " +
            $"playerSave={playerSavePath}; developerSave={developerSavePath}{Environment.NewLine}{exception}";
    }
}
