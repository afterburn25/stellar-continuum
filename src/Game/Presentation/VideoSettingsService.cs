using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using Godot;

namespace Game.Presentation;

public sealed class VideoSettingsService : IDisposable
{
    public readonly record struct Resolution(int Width, int Height)
    {
        public override string ToString() => $"{Width} × {Height}";
    }

    public enum DisplayMode { Windowed, Borderless, Fullscreen }
    public enum FrameCap { Automatic, Fps60, Fps120, Fps144, Unlimited }

    public readonly record struct Settings(Resolution Resolution, DisplayMode DisplayMode,
        DisplayServer.VSyncMode VSync, Viewport.Msaa Msaa, float RenderScale, FrameCap FrameCap = FrameCap.Automatic);

    private const string DefaultPath = "user://video_settings.cfg";
    private const int DevModeWSize = 220;
    private readonly string _path;
    private readonly AutomaticRefreshRateService _automaticRefresh = new(new WindowsRefreshRatePlatform());
    private int _appliedFrameCap = int.MinValue;
    private bool _runtimeApplyPending;

    public static Settings Current { get; private set; } = new(
        new Resolution(1280, 720), DisplayMode.Borderless, DisplayServer.VSyncMode.Enabled, Viewport.Msaa.Msaa4X, 1f);
    public IReadOnlyList<Resolution> Modes { get; }
    public string AdapterName { get; }
    public string RendererName { get; }
    /// <summary>Current output timing on the screen containing the game window.</summary>
    public int ActiveMonitorRefreshHz
    {
        get
        {
            var window = (Engine.GetMainLoop() as SceneTree)?.Root?.GetWindow();
            return RefreshRatePolicy.Normalize(DisplayServer.ScreenGetRefreshRate(window?.CurrentScreen ?? -1));
        }
    }
    public string? RefreshRateError => _automaticRefresh.LastError;
    public bool RefreshRestorePending => _runtimeApplyPending || _automaticRefresh.HasPendingRestore;
    public bool IsNvidiaAdapter => AdapterName.Contains("NVIDIA", StringComparison.OrdinalIgnoreCase);

    public VideoSettingsService(string path = DefaultPath)
    {
        _path = path;
        Modes = DetectModes();
        AdapterName = RenderingServer.GetVideoAdapterName();
        RendererName = RenderingServer.GetVideoAdapterApiVersion();
    }

    public Settings LoadAndApply()
    {
        Current = Load();
        ApplyRuntime(Current);
        return Current;
    }

    public Settings Load()
    {
        var defaults = Defaults();
        var config = new ConfigFile();
        if (config.Load(_path) != Error.Ok) return defaults;
        var loaded = Validate(new Settings(
            new Resolution(ReadInt(config, "width", defaults.Resolution.Width), ReadInt(config, "height", defaults.Resolution.Height)),
            ReadEnum(config, "display_mode", defaults.DisplayMode),
            ReadEnum(config, "vsync", defaults.VSync),
            ReadEnum(config, "msaa", defaults.Msaa),
            ReadFloat(config, "render_scale", defaults.RenderScale),
            ReadEnum(config, "frame_cap", defaults.FrameCap)), defaults);
        return loaded with { DisplayMode = loaded.DisplayMode == DisplayMode.Windowed ? DisplayMode.Borderless : loaded.DisplayMode };
    }

    public string ApplyAndSave(Settings settings)
    {
        ApplyPreview(settings);
        return SaveCurrent();
    }

    public Settings ApplyPreview(Settings settings)
    {
        var previous = Current;
        Current = Validate(settings, Defaults());
        if (Current.DisplayMode == DisplayMode.Windowed)
            Current = Current with { DisplayMode = DisplayMode.Borderless };
        ApplyRuntime(Current, previous);
        return previous;
    }

    public void Revert(Settings settings)
    {
        var previous = Current;
        Current = Validate(settings, Defaults());
        ApplyRuntime(Current, previous);
    }

    public string SaveCurrent()
    {
        var settings = Current;
        var config = new ConfigFile();
        config.SetValue("video", "width", settings.Resolution.Width);
        config.SetValue("video", "height", settings.Resolution.Height);
        config.SetValue("video", "display_mode", (int)settings.DisplayMode);
        config.SetValue("video", "vsync", (int)settings.VSync);
        config.SetValue("video", "msaa", (int)settings.Msaa);
        config.SetValue("video", "render_scale", settings.RenderScale);
        config.SetValue("video", "frame_cap", (int)settings.FrameCap);
        var error = config.Save(_path);
        return error == Error.Ok ? string.Empty : $"Video settings were applied but could not be saved ({error}).";
    }

    public void ApplyRuntime(Settings settings) => ApplyRuntime(settings, previous: null);

    private void ApplyRuntime(Settings settings, Settings? previous)
    {
        var root = (Engine.GetMainLoop() as SceneTree)?.Root;
        var window = root?.GetWindow();
        if (window is null || root is null) return;
        var desiredMode = WindowModeFor(settings.DisplayMode);
        var desiredBorderless = settings.DisplayMode != DisplayMode.Fullscreen;
        var changesWindowMode = window.Mode != desiredMode || window.Borderless != desiredBorderless;
        var leavesAutomatic = previous is { FrameCap: FrameCap.Automatic } &&
            settings.FrameCap != FrameCap.Automatic;
        if ((changesWindowMode || leavesAutomatic) && _automaticRefresh.Deactivate() is { } restoreError)
        {
            // Keep ownership of the exact original mode for the next lifecycle poll. Window
            // mutations must wait, otherwise Godot can replace the mode we still owe Windows.
            _runtimeApplyPending = true;
            ApplyFrameCapIfChanged(RefreshRatePolicy.ResolveFrameCap(settings.FrameCap, ActiveMonitorRefreshHz), force: true);
            ApplyToViewport(root, settings);
            return;
        }
        _runtimeApplyPending = false;
        if (changesWindowMode)
        {
            if (desiredMode == Window.ModeEnum.ExclusiveFullscreen)
            {
                window.Borderless = false;
                window.Mode = desiredMode;
            }
            else
            {
                window.Mode = desiredMode;
                window.Borderless = true;
            }
        }
        DisplayServer.WindowSetVsyncMode(settings.VSync, window.GetWindowId());
        var activeHz = ActiveMonitorRefreshHz;
        var targetHz = activeHz;
        if (CanUseWindowsAutomaticRefresh && settings.FrameCap == FrameCap.Automatic && window.HasFocus() &&
            window.Mode != Window.ModeEnum.Minimized)
        {
            var nativeHandle = (nint)DisplayServer.WindowGetNativeHandle(
                DisplayServer.HandleType.WindowHandle, window.GetWindowId());
            targetHz = _automaticRefresh.Activate(nativeHandle, activeHz).EffectiveHz;
        }
        else
        {
            _automaticRefresh.Deactivate();
        }
        ApplyFrameCapIfChanged(RefreshRatePolicy.ResolveFrameCap(settings.FrameCap, targetHz), force: true);
        ApplyToViewport(root, settings);
    }

    public void PollWindowState(Window window)
    {
        var activeHz = ActiveMonitorRefreshHz;
        if (_runtimeApplyPending)
        {
            var restoreError = _automaticRefresh.Deactivate();
            if (restoreError is not null)
            {
                ApplyFrameCapIfChanged(RefreshRatePolicy.ResolveFrameCap(Current.FrameCap, activeHz));
                return;
            }
            if (!window.HasFocus() || window.Mode == Window.ModeEnum.Minimized)
            {
                ApplyFrameCapIfChanged(RefreshRatePolicy.ResolveFrameCap(Current.FrameCap, ActiveMonitorRefreshHz));
                return;
            }
            ApplyRuntime(Current);
            return;
        }
        if (CanUseWindowsAutomaticRefresh && Current.FrameCap == FrameCap.Automatic &&
            window.HasFocus() && window.Mode != Window.ModeEnum.Minimized)
        {
            var nativeHandle = (nint)DisplayServer.WindowGetNativeHandle(
                DisplayServer.HandleType.WindowHandle, window.GetWindowId());
            var activation = _automaticRefresh.Activate(nativeHandle, activeHz);
            ApplyFrameCapIfChanged(RefreshRatePolicy.ResolveFrameCap(Current.FrameCap, activation.EffectiveHz));
            return;
        }

        _automaticRefresh.Deactivate();
        ApplyFrameCapIfChanged(RefreshRatePolicy.ResolveFrameCap(Current.FrameCap, activeHz));
    }

    public void Dispose() => _automaticRefresh.Dispose();

    private static bool CanUseWindowsAutomaticRefresh =>
        OperatingSystem.IsWindows() && DisplayServer.GetName() != "headless";

    private void ApplyFrameCapIfChanged(int frameCap, bool force = false)
    {
        if (!force && _appliedFrameCap == frameCap)
            return;
        Engine.MaxFps = frameCap;
        _appliedFrameCap = frameCap;
    }

    public static Window.ModeEnum WindowModeFor(DisplayMode mode) => mode switch
    {
        DisplayMode.Windowed => Window.ModeEnum.Fullscreen,
        DisplayMode.Borderless => Window.ModeEnum.Fullscreen,
        DisplayMode.Fullscreen => Window.ModeEnum.ExclusiveFullscreen,
        _ => Window.ModeEnum.Fullscreen,
    };

    public static void ApplyToViewport(Viewport viewport) => ApplyToViewport(viewport, Current);
    public static void ApplyToViewport(Viewport viewport, Settings settings)
    {
        viewport.Msaa3D = settings.Msaa;
        viewport.Scaling3DScale = settings.RenderScale;
    }

    public string? FindNvidiaControlPanel()
    {
        if (!OperatingSystem.IsWindows() || !IsNvidiaAdapter) return null;
        var candidates = new[]
        {
            Path.Combine(System.Environment.GetFolderPath(System.Environment.SpecialFolder.Windows), "System32", "nvcplui.exe"),
            Path.Combine(System.Environment.GetFolderPath(System.Environment.SpecialFolder.ProgramFiles), "NVIDIA Corporation", "Control Panel Client", "nvcplui.exe"),
            Path.Combine(System.Environment.GetFolderPath(System.Environment.SpecialFolder.ProgramFilesX86), "NVIDIA Corporation", "Control Panel Client", "nvcplui.exe"),
        };
        foreach (var candidate in candidates) if (File.Exists(candidate)) return candidate;
        try
        {
            using var process = System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = "powershell.exe",
                Arguments = "-NoProfile -NonInteractive -Command \"(Get-AppxPackage -Name NVIDIACorp.NVIDIAControlPanel).PackageFamilyName\"",
                UseShellExecute = false,
                RedirectStandardOutput = true,
                CreateNoWindow = true,
                WindowStyle = System.Diagnostics.ProcessWindowStyle.Hidden,
            });
            if (process is not null && process.WaitForExit(2000) && !string.IsNullOrWhiteSpace(process.StandardOutput.ReadToEnd()))
                return "shell:AppsFolder\\NVIDIACorp.NVIDIAControlPanel_56jybvy8sckqj!NVIDIACorp.NVIDIAControlPanel";
        }
        catch { /* Optional OS integration must never prevent the settings screen opening. */ }
        return null;
    }

    public static void OpenNvidiaControlPanel(string location)
    {
        if (location.StartsWith("shell:", StringComparison.OrdinalIgnoreCase))
        {
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("explorer.exe", location) { UseShellExecute = true });
            return;
        }
        System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(location) { UseShellExecute = true });
    }

    private Settings Defaults()
    {
        var window = (Engine.GetMainLoop() as SceneTree)?.Root?.GetWindow();
        var size = window?.Size ?? DisplayServer.ScreenGetSize();
        var resolution = ClosestMode(new Resolution(Math.Max(1280, size.X), Math.Max(720, size.Y)));
        var displayMode = window is null ? DisplayMode.Borderless : window.Mode switch
        {
            Window.ModeEnum.ExclusiveFullscreen => DisplayMode.Fullscreen,
            Window.ModeEnum.Fullscreen => DisplayMode.Borderless,
            _ => DisplayMode.Borderless,
        };
        return new Settings(resolution, displayMode, DisplayServer.VSyncMode.Enabled, Viewport.Msaa.Msaa4X, 1f, FrameCap.Automatic);
    }

    private Settings Validate(Settings value, Settings fallback)
    {
        var resolution = ContainsMode(value.Resolution) ? value.Resolution : fallback.Resolution;
        var displayMode = Enum.IsDefined(value.DisplayMode) ? value.DisplayMode : fallback.DisplayMode;
        if (displayMode == DisplayMode.Windowed) displayMode = DisplayMode.Borderless;
        var vsync = Enum.IsDefined(value.VSync) ? value.VSync : fallback.VSync;
        var msaa = value.Msaa is Viewport.Msaa.Disabled or Viewport.Msaa.Msaa2X or Viewport.Msaa.Msaa4X or Viewport.Msaa.Msaa8X ? value.Msaa : fallback.Msaa;
        var renderScale = value.RenderScale is .75f or 1f or 1.25f ? value.RenderScale : fallback.RenderScale;
        var frameCap = Enum.IsDefined(value.FrameCap) ? value.FrameCap : fallback.FrameCap;
        return new Settings(resolution, displayMode, vsync, msaa, renderScale, frameCap);
    }

    private Resolution ClosestMode(Resolution requested)
    {
        var best = Modes[0];
        var bestDistance = long.MaxValue;
        foreach (var mode in Modes)
        {
            var distance = Math.Abs((long)mode.Width - requested.Width) + Math.Abs((long)mode.Height - requested.Height);
            if (distance < bestDistance) { best = mode; bestDistance = distance; }
        }
        return best;
    }

    private bool ContainsMode(Resolution requested)
    {
        foreach (var mode in Modes) if (mode == requested) return true;
        return false;
    }

    private static int ReadInt(ConfigFile config, string key, int fallback)
    {
        var value = config.GetValue("video", key, fallback);
        return value.VariantType is Variant.Type.Int or Variant.Type.Float ? value.AsInt32() : fallback;
    }

    private static float ReadFloat(ConfigFile config, string key, float fallback)
    {
        var value = config.GetValue("video", key, fallback);
        return value.VariantType is Variant.Type.Int or Variant.Type.Float ? value.AsSingle() : fallback;
    }

    private static T ReadEnum<T>(ConfigFile config, string key, T fallback) where T : struct, Enum
    {
        var number = ReadInt(config, key, Convert.ToInt32(fallback));
        var candidate = (T)Enum.ToObject(typeof(T), number);
        return Enum.IsDefined(candidate) ? candidate : fallback;
    }

    private static IReadOnlyList<Resolution> DetectModes()
    {
        var comparer = Comparer<Resolution>.Create((a, b) => a.Width != b.Width ? a.Width.CompareTo(b.Width) : a.Height.CompareTo(b.Height));
        var modes = new SortedSet<Resolution>(comparer) { new(1280, 720) };
        var screen = DisplayServer.ScreenGetSize();
        if (screen.X >= 1280 && screen.Y >= 720) modes.Add(new(screen.X, screen.Y));
        if (OperatingSystem.IsWindows())
        {
            var buffer = Marshal.AllocHGlobal(DevModeWSize);
            try
            {
                for (var index = 0; ; index++)
                {
                    Marshal.Copy(new byte[DevModeWSize], 0, buffer, DevModeWSize);
                    Marshal.WriteInt16(buffer, 68, DevModeWSize);
                    if (!EnumDisplaySettingsW(null, index, buffer)) break;
                    var width = Marshal.ReadInt32(buffer, 172);
                    var height = Marshal.ReadInt32(buffer, 176);
                    if (width >= 1280 && height >= 720) modes.Add(new(width, height));
                }
            }
            finally { Marshal.FreeHGlobal(buffer); }
        }
        return new List<Resolution>(modes);
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumDisplaySettingsW(string? deviceName, int modeNum, IntPtr devMode);
}
