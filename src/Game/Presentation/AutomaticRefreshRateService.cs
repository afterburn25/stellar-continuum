using System;
using System.Collections.Generic;

namespace Game.Presentation;

public readonly record struct RefreshDisplayMode(
    string DeviceName, int Width, int Height, int RefreshHz, bool Progressive, object? PlatformState = null);

public interface IRefreshRatePlatform
{
    string? FindDisplayForWindow(nint windowHandle);
    RefreshDisplayMode? GetCurrentMode(string deviceName);
    IReadOnlyList<RefreshDisplayMode> GetSupportedModes(string deviceName);
    string? TryApplyTemporary(RefreshDisplayMode mode);
    string? TryRestore(RefreshDisplayMode originalMode);
}

public readonly record struct RefreshRateActivation(int EffectiveHz, bool ChangedMode, string? Error);

public sealed class AutomaticRefreshRateService : IDisposable
{
    private readonly IRefreshRatePlatform _platform;
    private RefreshDisplayMode? _originalMode;
    private RefreshDisplayMode? _appliedMode;
    private string? _observedDevice;
    private int _effectiveHz = RefreshRatePolicy.FallbackHz;

    public AutomaticRefreshRateService(IRefreshRatePlatform platform) => _platform = platform;

    public bool HasPendingRestore => _originalMode is not null;
    public string? LastError { get; private set; }

    public RefreshRateActivation Activate(nint windowHandle, int fallbackHz)
    {
        var device = _platform.FindDisplayForWindow(windowHandle);
        if (string.IsNullOrWhiteSpace(device))
            return Result(RefreshRatePolicy.Normalize(fallbackHz), false,
                "Windows could not identify the monitor containing the game window.");

        if (_originalMode is not null && !string.Equals(_originalMode.Value.DeviceName, device, StringComparison.Ordinal))
        {
            var restoreError = Restore();
            if (restoreError is not null)
                return Result(_effectiveHz, true, restoreError);
        }

        if (string.Equals(_observedDevice, device, StringComparison.Ordinal))
        {
            var observed = _platform.GetCurrentMode(device);
            if (observed is null)
                return Result(RefreshRatePolicy.Normalize(fallbackHz), _originalMode is not null,
                    "Windows could not verify the monitor's current display mode.");
            if (observed.Value.Width == _appliedMode?.Width &&
                observed.Value.Height == _appliedMode?.Height &&
                observed.Value.RefreshHz == _appliedMode?.RefreshHz)
                return Result(RefreshRatePolicy.Normalize(observed.Value.RefreshHz), _originalMode is not null, null);
            if (_originalMode is not null && _appliedMode is { } applied &&
                observed.Value.Width == applied.Width && observed.Value.Height == applied.Height)
            {
                var reapplyError = _platform.TryApplyTemporary(applied);
                if (reapplyError is null)
                {
                    _effectiveHz = applied.RefreshHz;
                    return Result(_effectiveHz, true, null);
                }
                _effectiveHz = RefreshRatePolicy.Normalize(observed.Value.RefreshHz);
                return Result(_effectiveHz, true, reapplyError);
            }
            // The resolution changed outside this service. Release the old mode before
            // selecting a highest refresh for the newly current resolution.
            if (_originalMode is not null)
                RelinquishStaleOverride(observed.Value);
            _observedDevice = null;
        }

        var current = _platform.GetCurrentMode(device);
        if (current is null)
            return Observe(device, RefreshRatePolicy.Normalize(fallbackHz),
                "Windows could not read the monitor's current display mode.", observedMode: null);

        var target = RefreshRatePolicy.HighestProgressiveAtCurrentResolution(
            _platform.GetSupportedModes(device), current.Value);
        if (target is null || target.Value.RefreshHz <= current.Value.RefreshHz)
            return Observe(device, RefreshRatePolicy.Normalize(current.Value.RefreshHz), null, current.Value);

        var requested = target.Value with { PlatformState = current.Value.PlatformState };
        var applyError = _platform.TryApplyTemporary(requested);
        if (applyError is not null)
            return Observe(device, RefreshRatePolicy.Normalize(current.Value.RefreshHz), applyError, current.Value);

        _originalMode = current.Value;
        _appliedMode = requested;
        _observedDevice = device;
        _effectiveHz = requested.RefreshHz;
        LastError = null;
        return new RefreshRateActivation(_effectiveHz, true, null);
    }

    public string? Deactivate()
    {
        var error = Restore();
        if (error is null)
        {
            _observedDevice = null;
            _appliedMode = null;
            _effectiveHz = RefreshRatePolicy.FallbackHz;
        }
        return error;
    }

    private void RelinquishStaleOverride(RefreshDisplayMode observed)
    {
        // A resolution change replaces the temporary mode at the OS boundary. Restoring the
        // old full DEVMODE here would undo the user's new resolution, so it becomes the next
        // baseline instead of being treated as a mode we still own.
        _originalMode = null;
        _appliedMode = null;
        _effectiveHz = RefreshRatePolicy.Normalize(observed.RefreshHz);
        LastError = null;
    }

    public void Dispose()
    {
        if (Deactivate() is not null)
            Deactivate();
    }

    private string? Restore()
    {
        if (_originalMode is null)
        {
            LastError = null;
            return null;
        }
        if (_appliedMode is { } applied)
        {
            var observed = _platform.GetCurrentMode(_originalMode.Value.DeviceName);
            if (observed is { } current &&
                (current.Width != applied.Width || current.Height != applied.Height))
            {
                RelinquishStaleOverride(current);
                return null;
            }
        }
        var error = _platform.TryRestore(_originalMode.Value);
        if (error is not null)
        {
            LastError = error;
            return error;
        }
        _originalMode = null;
        _appliedMode = null;
        LastError = null;
        return null;
    }

    private RefreshRateActivation Observe(
        string device, int effectiveHz, string? error, RefreshDisplayMode? observedMode)
    {
        _observedDevice = device;
        _appliedMode = observedMode;
        _effectiveHz = effectiveHz;
        return Result(effectiveHz, false, error);
    }

    private RefreshRateActivation Result(int effectiveHz, bool changedMode, string? error)
    {
        LastError = error;
        return new RefreshRateActivation(effectiveHz, changedMode, error);
    }
}
