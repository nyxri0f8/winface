using System.Diagnostics;
using System.IO;
using Microsoft.Win32;

namespace WinFace;

/// <summary>Updates are handled by WinFaceUpdater.exe (installed next to the app; also run by a per-user scheduled task
/// at sign-in and once a day). It checks GitHub Releases and shows "Update now" with the release notes.</summary>
public static class Updates
{
    static string Exe => Path.Combine(Backend.Dir, "WinFaceUpdater.exe");

    public static bool Auto
    {
        get => Registry.CurrentUser.OpenSubKey(@"Software\WinFace")?.GetValue("AutoUpdate") is not 0;
        set
        {
            using var k = Registry.CurrentUser.CreateSubKey(@"Software\WinFace");
            k.SetValue("AutoUpdate", value ? 1 : 0, RegistryValueKind.DWord);
        }
    }

    /// <summary>Quiet check (the updater itself limits this to once every 6 hours).</summary>
    public static void CheckInBackground() => Start("--background");

    /// <summary>"Check for updates": always checks and also reports "up to date".</summary>
    public static void CheckNow() => Start("");

    static void Start(string args)
    {
        try { if (File.Exists(Exe)) Process.Start(new ProcessStartInfo(Exe, args) { UseShellExecute = false }); } catch { }
    }
}
