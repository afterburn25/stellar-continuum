using System;
using System.Collections.Generic;

namespace Game.Presentation;

public static class RefreshRatePolicy
{
    public const int FallbackHz = 60;
    public const int MinimumSupportedHz = 30;
    public const int MaximumSupportedHz = 1000;

    public static bool IsSupportedRefresh(int refreshHz) =>
        refreshHz is >= MinimumSupportedHz and <= MaximumSupportedHz;

    public static int Normalize(double detectedHz)
    {
        if (!double.IsFinite(detectedHz) || detectedHz < MinimumSupportedHz || detectedHz > MaximumSupportedHz)
            return FallbackHz;
        return (int)Math.Round(detectedHz, MidpointRounding.AwayFromZero);
    }

    public static int ResolveFrameCap(VideoSettingsService.FrameCap preference, int activeMonitorHz) => preference switch
    {
        VideoSettingsService.FrameCap.Automatic => Normalize(activeMonitorHz),
        VideoSettingsService.FrameCap.Fps60 => 60,
        VideoSettingsService.FrameCap.Fps120 => 120,
        VideoSettingsService.FrameCap.Fps144 => 144,
        VideoSettingsService.FrameCap.Unlimited => 0,
        _ => Normalize(activeMonitorHz),
    };

    public static RefreshDisplayMode? HighestProgressiveAtCurrentResolution(
        IEnumerable<RefreshDisplayMode> modes,
        RefreshDisplayMode current)
    {
        RefreshDisplayMode? best = null;
        foreach (var mode in modes)
        {
            if (!string.Equals(mode.DeviceName, current.DeviceName, StringComparison.Ordinal) ||
                mode.Width != current.Width || mode.Height != current.Height || !mode.Progressive ||
                !IsSupportedRefresh(mode.RefreshHz))
                continue;
            if (best is null || mode.RefreshHz > best.Value.RefreshHz)
                best = mode;
        }
        return best;
    }

    public static int HighestSupportedAtCurrentResolution(
        IEnumerable<(int Width, int Height, int RefreshHz, bool Progressive)> modes,
        int currentWidth,
        int currentHeight,
        int fallbackHz)
    {
        var current = new RefreshDisplayMode("display", currentWidth, currentHeight, Normalize(fallbackHz), true);
        var candidates = new List<RefreshDisplayMode>();
        foreach (var mode in modes)
            candidates.Add(new RefreshDisplayMode("display", mode.Width, mode.Height, mode.RefreshHz, mode.Progressive));
        return HighestProgressiveAtCurrentResolution(candidates, current)?.RefreshHz ?? current.RefreshHz;
    }
}
