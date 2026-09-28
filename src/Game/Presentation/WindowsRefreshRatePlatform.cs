using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Game.Presentation;

internal sealed class WindowsRefreshRatePlatform : IRefreshRatePlatform
{
    private const int DevModeSize = 220;
    private const int DevModeSizeOffset = 68;
    private const int DevModeFieldsOffset = 72;
    private const int DevModeWidthOffset = 172;
    private const int DevModeHeightOffset = 176;
    private const int DevModeDisplayFlagsOffset = 180;
    private const int DevModeRefreshOffset = 184;
    private const int EnumCurrentSettings = -1;
    private const int MonitorDefaultToNearest = 2;
    private const int CdsFullscreen = 0x4;
    private const int CdsTest = 0x2;
    private const int DispChangeSuccessful = 0;
    private const int DmDisplayFrequency = 0x00400000;
    private const int DmInterlaced = 0x2;

    public string? FindDisplayForWindow(nint windowHandle)
    {
        if (!OperatingSystem.IsWindows() || windowHandle == 0)
            return null;
        var monitor = MonitorFromWindow(windowHandle, MonitorDefaultToNearest);
        if (monitor == 0)
            return null;
        var info = new MonitorInfoEx { Size = Marshal.SizeOf<MonitorInfoEx>() };
        return GetMonitorInfoW(monitor, ref info) ? info.DeviceName : null;
    }

    public RefreshDisplayMode? GetCurrentMode(string deviceName) => ReadMode(deviceName, EnumCurrentSettings);

    public IReadOnlyList<RefreshDisplayMode> GetSupportedModes(string deviceName)
    {
        var modes = new List<RefreshDisplayMode>();
        for (var index = 0; ; index++)
        {
            var mode = ReadMode(deviceName, index);
            if (mode is null)
                break;
            modes.Add(mode.Value);
        }
        return modes;
    }

    public string? TryApplyTemporary(RefreshDisplayMode mode)
    {
        if (mode.PlatformState is not byte[] bytes)
            return "Windows returned an unusable display mode.";

        var requested = (byte[])bytes.Clone();
        var fields = BitConverter.ToInt32(requested, DevModeFieldsOffset) | DmDisplayFrequency;
        Buffer.BlockCopy(BitConverter.GetBytes(fields), 0, requested, DevModeFieldsOffset, sizeof(int));
        Buffer.BlockCopy(BitConverter.GetBytes(mode.RefreshHz), 0, requested, DevModeRefreshOffset, sizeof(int));

        var testResult = Change(mode.DeviceName, requested, CdsTest);
        if (testResult != DispChangeSuccessful)
            return $"Windows rejected {mode.Width} × {mode.Height} at {mode.RefreshHz} Hz during validation ({testResult}).";
        var applyResult = Change(mode.DeviceName, requested, CdsFullscreen);
        return applyResult == DispChangeSuccessful
            ? null
            : $"Windows could not apply {mode.Width} × {mode.Height} at {mode.RefreshHz} Hz ({applyResult}).";
    }

    public string? TryRestore(RefreshDisplayMode originalMode)
    {
        if (originalMode.PlatformState is not byte[] bytes)
            return "The original Windows display mode is unavailable for restoration.";
        var result = Change(originalMode.DeviceName, bytes, CdsFullscreen);
        return result == DispChangeSuccessful
            ? null
            : $"Windows could not restore {originalMode.Width} × {originalMode.Height} at {originalMode.RefreshHz} Hz ({result}).";
    }

    private static RefreshDisplayMode? ReadMode(string deviceName, int index)
    {
        var pointer = Marshal.AllocHGlobal(DevModeSize);
        try
        {
            Marshal.Copy(new byte[DevModeSize], 0, pointer, DevModeSize);
            Marshal.WriteInt16(pointer, DevModeSizeOffset, DevModeSize);
            if (!EnumDisplaySettingsW(deviceName, index, pointer))
                return null;

            var bytes = new byte[DevModeSize];
            Marshal.Copy(pointer, bytes, 0, bytes.Length);
            var displayFlags = BitConverter.ToInt32(bytes, DevModeDisplayFlagsOffset);
            return new RefreshDisplayMode(
                deviceName,
                BitConverter.ToInt32(bytes, DevModeWidthOffset),
                BitConverter.ToInt32(bytes, DevModeHeightOffset),
                BitConverter.ToInt32(bytes, DevModeRefreshOffset),
                (displayFlags & DmInterlaced) == 0,
                bytes);
        }
        finally
        {
            Marshal.FreeHGlobal(pointer);
        }
    }

    private static int Change(string deviceName, byte[] mode, int flags)
    {
        var pointer = Marshal.AllocHGlobal(DevModeSize);
        try
        {
            Marshal.Copy(mode, 0, pointer, DevModeSize);
            return ChangeDisplaySettingsExW(deviceName, pointer, 0, flags, 0);
        }
        finally
        {
            Marshal.FreeHGlobal(pointer);
        }
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct MonitorInfoEx
    {
        public int Size;
        public int MonitorLeft;
        public int MonitorTop;
        public int MonitorRight;
        public int MonitorBottom;
        public int WorkLeft;
        public int WorkTop;
        public int WorkRight;
        public int WorkBottom;
        public uint Flags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string DeviceName;
    }

    [DllImport("user32.dll")]
    private static extern nint MonitorFromWindow(nint windowHandle, uint flags);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetMonitorInfoW(nint monitor, ref MonitorInfoEx info);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumDisplaySettingsW(string deviceName, int modeNumber, nint devMode);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int ChangeDisplaySettingsExW(
        string deviceName, nint devMode, nint windowHandle, int flags, nint parameter);
}
