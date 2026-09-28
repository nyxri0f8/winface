using System.Windows;

namespace WinFaceUpdater;

/// <summary>
///   WinFaceUpdater --background   from the scheduled task / when WinFace starts: at most every 6 hours, quiet unless
///                                 there is an update the user has not skipped
///   WinFaceUpdater                "Check for updates" in the app: always checks, also says "you're up to date"
/// </summary>
public partial class App : Application
{
    protected override async void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        bool background = e.Args.Contains("--background", StringComparer.OrdinalIgnoreCase);
        try
        {
            if (background && (!Prefs.AutoUpdate || DateTime.UtcNow - Prefs.LastCheck < TimeSpan.FromHours(6))) { Shutdown(); return; }
            var rel = await Release.Latest();
            Prefs.LastCheck = DateTime.UtcNow;
            bool newer = rel != null && rel.Version > Release.Current;
            if (newer && !(background && Prefs.SkipVersion == rel!.Tag))
            {
                new UpdateWindow(rel!).ShowDialog();
            }
            else if (!background)
            {
                MessageBox.Show($"WinFace {Release.Current} is up to date.", "WinFace", MessageBoxButton.OK, MessageBoxImage.Information);
            }
        }
        catch (Exception ex)
        {
            // offline or GitHub unreachable: stay quiet in the background, explain when the user asked
            if (!background) MessageBox.Show("Could not check for updates:\n" + ex.Message, "WinFace", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
        Shutdown();
    }
}
