using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;

namespace WinFace.Pages;

public partial class LogsPage : UserControl, IDisposable
{
    const int MaxLines = 600;
    readonly DispatcherTimer _timer = new() { Interval = TimeSpan.FromSeconds(2) };
    long _lastSize = -1;

    public LogsPage()
    {
        InitializeComponent();
        _timer.Tick += (_, _) => { if (Auto.IsChecked == true) Load(); };
        Loaded += (_, _) => { Load(); _timer.Start(); };
    }

    public void Dispose() => _timer.Stop();

    void Load(bool force = false)
    {
        try
        {
            var fi = new FileInfo(Backend.LogPath);
            if (!fi.Exists) { Log.Text = "No log yet - it starts when face unlock is first used."; return; }
            if (!force && fi.Length == _lastSize) return;
            _lastSize = fi.Length;
            using var fs = new FileStream(fi.FullName, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            fs.Seek(Math.Max(0, fs.Length - 256 * 1024), SeekOrigin.Begin);   // the tail is enough
            var lines = new StreamReader(fs).ReadToEnd().Split('\n');
            bool atEnd = Log.VerticalOffset + Log.ViewportHeight >= Log.ExtentHeight - 4;
            Log.Text = string.Join("\n", lines.Skip(Math.Max(0, lines.Length - MaxLines))).TrimEnd();
            if (atEnd || force) Log.ScrollToEnd();
        }
        catch (Exception ex) { Log.Text = "Cannot read the log: " + ex.Message; }
    }

    void OnRefresh(object sender, RoutedEventArgs e) => Load(true);
    void OnCopy(object sender, RoutedEventArgs e) => Clipboard.SetText(Log.Text);
    void OnOpen(object sender, RoutedEventArgs e) => SystemInfo.Open(Backend.DataDir);
}
