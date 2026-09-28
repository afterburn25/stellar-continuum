using Game.Presentation;

namespace Game.CoreRuntime.Validation;

internal static class RefreshRateValidation
{
    public static void Run()
    {
        ValidateSuccessfulApplyAndSingleRestore();
        ValidateApplyFailureDoesNotClaimOwnership();
        ValidateRestoreFailureRemainsRetryable();
        ValidateFocusCycle();
        ValidateMonitorChangeRestoresOldMonitorFirst();
        ValidateSameMonitorModeDriftIsReappliedWithoutReenumeration();
        ValidateAlreadyHighestModeUsesStableFastPath();
        ValidateExternalResolutionBecomesNewBaseline();
        ValidateDeactivatePreservesExternalResolution();
        ValidateMonitorMovePreservesExternalResolutionOnOldMonitor();
        ValidateDisposeRetriesOneFailedRestore();
        ValidateModeFiltering();
    }

    private static void ValidateSuccessfulApplyAndSingleRestore()
    {
        var platform = FakePlatform.OneDisplay();
        var service = new AutomaticRefreshRateService(platform);

        var activation = service.Activate(1, 60);
        Require(activation is { EffectiveHz: 144, ChangedMode: true, Error: null } &&
                Equals(platform.Applied.Single().PlatformState, 60),
            "automatic refresh did not report the applied mode");
        Require(service.Deactivate() is null && service.Deactivate() is null,
            "successful restore reported an error");
        Require(platform.Restored.Count == 1 && platform.Restored[0].RefreshHz == 60,
            "the exact original mode was not restored exactly once");
    }

    private static void ValidateApplyFailureDoesNotClaimOwnership()
    {
        var platform = FakePlatform.OneDisplay();
        platform.ApplyError = "apply failed";
        var service = new AutomaticRefreshRateService(platform);

        var activation = service.Activate(1, 60);
        Require(activation.EffectiveHz == 60 && activation.Error == "apply failed",
            "apply failure did not retain the real active refresh and error");
        service.Deactivate();
        Require(!service.HasPendingRestore && platform.Restored.Count == 0,
            "failed apply incorrectly captured or restored an original mode");
    }

    private static void ValidateRestoreFailureRemainsRetryable()
    {
        var platform = FakePlatform.OneDisplay();
        platform.RestoreErrors.Enqueue("restore failed");
        platform.RestoreErrors.Enqueue(null);
        var service = new AutomaticRefreshRateService(platform);
        service.Activate(1, 60);

        Require(service.Deactivate() == "restore failed" && service.HasPendingRestore &&
                platform.Current["A"].RefreshHz == 144,
            "failed restore discarded the original mode");
        Require(service.Deactivate() is null && !service.HasPendingRestore && platform.Restored.Count == 2 &&
                platform.Current["A"].RefreshHz == 60,
            "failed restore could not be retried to completion");
    }

    private static void ValidateFocusCycle()
    {
        var platform = FakePlatform.OneDisplay();
        var service = new AutomaticRefreshRateService(platform);
        service.Activate(1, 60);
        service.Deactivate();
        service.Activate(1, 60);

        Require(platform.Applied.Count == 2 && platform.Restored.Count == 1,
            "focus loss and regain did not restore then reacquire the highest mode");
    }

    private static void ValidateMonitorChangeRestoresOldMonitorFirst()
    {
        var platform = FakePlatform.TwoDisplays();
        var service = new AutomaticRefreshRateService(platform);
        service.Activate(1, 60);
        platform.WindowDevice = "B";
        var activation = service.Activate(1, 60);

        Require(activation.EffectiveHz == 165 &&
                platform.Events.SequenceEqual(new[] { "apply:A:144", "restore:A:60", "apply:B:165" }),
            "monitor move did not restore the old monitor before changing the new monitor");
    }

    private static void ValidateModeFiltering()
    {
        var platform = FakePlatform.OneDisplay();
        platform.Modes["A"] = new List<RefreshDisplayMode>
        {
            Mode("A", 1920, 1080, 60),
            Mode("A", 1920, 1080, 240, progressive: false),
            Mode("A", 1920, 1080, 2000),
            Mode("A", 2560, 1440, 360),
            Mode("A", 1920, 1080, 144),
        };

        var activation = new AutomaticRefreshRateService(platform).Activate(1, 60);
        Require(activation.EffectiveHz == 144 && platform.Applied.Single().RefreshHz == 144,
            "interlaced, unsupported, or different-resolution modes were not filtered");
    }

    private static void ValidateSameMonitorModeDriftIsReappliedWithoutReenumeration()
    {
        var platform = FakePlatform.OneDisplay();
        var service = new AutomaticRefreshRateService(platform);
        service.Activate(1, 60);
        platform.Current["A"] = Mode("A", 1920, 1080, 60);

        var activation = service.Activate(1, 60);
        Require(activation is { EffectiveHz: 144, ChangedMode: true, Error: null } &&
                platform.Applied.Count == 2 && platform.SupportedModeReads == 1,
            "same-monitor mode drift was cached or needlessly re-enumerated supported modes");
        service.Deactivate();
    }

    private static void ValidateDisposeRetriesOneFailedRestore()
    {
        var platform = FakePlatform.OneDisplay();
        platform.RestoreErrors.Enqueue("transient restore failure");
        platform.RestoreErrors.Enqueue(null);
        var service = new AutomaticRefreshRateService(platform);
        service.Activate(1, 60);

        service.Dispose();
        Require(!service.HasPendingRestore && platform.Restored.Count == 2 && platform.Current["A"].RefreshHz == 60,
            "disposal did not make one bounded retry of a transient restore failure");
    }

    private static void ValidateAlreadyHighestModeUsesStableFastPath()
    {
        var platform = FakePlatform.OneDisplay();
        platform.Current["A"] = Mode("A", 1920, 1080, 144);
        var service = new AutomaticRefreshRateService(platform);

        service.Activate(1, 144);
        var second = service.Activate(1, 144);
        Require(second is { EffectiveHz: 144, ChangedMode: false, Error: null } &&
                platform.Applied.Count == 0 && platform.SupportedModeReads == 1,
            "an already-highest stable mode re-enumerated supported display modes");
    }

    private static void ValidateExternalResolutionBecomesNewBaseline()
    {
        var platform = FakePlatform.OneDisplay();
        platform.Modes["A"] = new[]
        {
            Mode("A", 1920, 1080, 60), Mode("A", 1920, 1080, 144),
            Mode("A", 2560, 1440, 60), Mode("A", 2560, 1440, 165),
        };
        var service = new AutomaticRefreshRateService(platform);
        service.Activate(1, 60);
        platform.Current["A"] = Mode("A", 2560, 1440, 60);

        var activation = service.Activate(1, 60);
        Require(activation is { EffectiveHz: 165, ChangedMode: true, Error: null } &&
                platform.Events.SequenceEqual(new[] { "apply:A:144", "apply:A:165" }) &&
                platform.Current["A"] is { Width: 2560, Height: 1440, RefreshHz: 165 },
            "external resolution change was overwritten instead of becoming the new automatic baseline");
        service.Deactivate();
        Require(platform.Restored.Count == 1 &&
                platform.Restored[0] is { Width: 2560, Height: 1440, RefreshHz: 60 },
            "automatic refresh did not restore the externally selected resolution's baseline");
    }

    private static void ValidateDeactivatePreservesExternalResolution()
    {
        var platform = FakePlatform.OneDisplay();
        platform.Modes["A"] = new[]
        {
            Mode("A", 1920, 1080, 60), Mode("A", 1920, 1080, 144),
            Mode("A", 2560, 1440, 60), Mode("A", 2560, 1440, 165),
        };
        var service = new AutomaticRefreshRateService(platform);
        service.Activate(1, 60);
        platform.Current["A"] = Mode("A", 2560, 1440, 60);

        Require(service.Deactivate() is null && platform.Restored.Count == 0 &&
                platform.Current["A"] is { Width: 2560, Height: 1440, RefreshHz: 60 },
            "deactivation restored stale dimensions over an external resolution change");
        service.Activate(1, 60);
        service.Deactivate();
        Require(platform.Restored.Count == 1 &&
                platform.Restored[0] is { Width: 2560, Height: 1440, RefreshHz: 60 },
            "reactivation did not adopt the external resolution as its restoration baseline");
    }

    private static void ValidateMonitorMovePreservesExternalResolutionOnOldMonitor()
    {
        var platform = FakePlatform.TwoDisplays();
        var service = new AutomaticRefreshRateService(platform);
        service.Activate(1, 60);
        platform.Current["A"] = Mode("A", 1280, 720, 75);
        platform.WindowDevice = "B";

        var activation = service.Activate(1, 60);
        Require(activation is { EffectiveHz: 165, ChangedMode: true, Error: null } &&
                platform.Events.SequenceEqual(new[] { "apply:A:144", "apply:B:165" }) &&
                platform.Current["A"] is { Width: 1280, Height: 720, RefreshHz: 75 },
            "monitor migration restored obsolete dimensions over the old monitor's external resolution");
        service.Deactivate();
        Require(platform.Restored.Count == 1 && platform.Restored[0] is
                { DeviceName: "B", Width: 2560, Height: 1440, RefreshHz: 60 } &&
                platform.Current["A"] is { Width: 1280, Height: 720, RefreshHz: 75 },
            "new monitor automatic refresh did not restore independently of the old monitor");
    }

    private static RefreshDisplayMode Mode(
        string device, int width, int height, int refresh, bool progressive = true) =>
        new(device, width, height, refresh, progressive, refresh);

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }

    private sealed class FakePlatform : IRefreshRatePlatform
    {
        public string WindowDevice { get; set; } = "A";
        public Dictionary<string, RefreshDisplayMode> Current { get; } = new();
        public Dictionary<string, IReadOnlyList<RefreshDisplayMode>> Modes { get; } = new();
        public List<RefreshDisplayMode> Applied { get; } = new();
        public List<RefreshDisplayMode> Restored { get; } = new();
        public List<string> Events { get; } = new();
        public Queue<string?> RestoreErrors { get; } = new();
        public string? ApplyError { get; set; }
        public int SupportedModeReads { get; private set; }

        public static FakePlatform OneDisplay()
        {
            var fake = new FakePlatform();
            fake.Current["A"] = Mode("A", 1920, 1080, 60);
            fake.Modes["A"] = new[] { fake.Current["A"], Mode("A", 1920, 1080, 144) };
            return fake;
        }

        public static FakePlatform TwoDisplays()
        {
            var fake = OneDisplay();
            fake.Current["B"] = Mode("B", 2560, 1440, 60);
            fake.Modes["B"] = new[] { fake.Current["B"], Mode("B", 2560, 1440, 165) };
            return fake;
        }

        public string? FindDisplayForWindow(nint windowHandle) => WindowDevice;
        public RefreshDisplayMode? GetCurrentMode(string deviceName) => Current.GetValueOrDefault(deviceName);
        public IReadOnlyList<RefreshDisplayMode> GetSupportedModes(string deviceName)
        {
            SupportedModeReads++;
            return Modes[deviceName];
        }

        public string? TryApplyTemporary(RefreshDisplayMode mode)
        {
            Events.Add($"apply:{mode.DeviceName}:{mode.RefreshHz}");
            Applied.Add(mode);
            if (ApplyError is null) Current[mode.DeviceName] = mode;
            return ApplyError;
        }

        public string? TryRestore(RefreshDisplayMode originalMode)
        {
            Events.Add($"restore:{originalMode.DeviceName}:{originalMode.RefreshHz}");
            Restored.Add(originalMode);
            var error = RestoreErrors.Count == 0 ? null : RestoreErrors.Dequeue();
            if (error is null) Current[originalMode.DeviceName] = originalMode;
            return error;
        }
    }
}
