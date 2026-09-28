using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace WinFaceUpdater;

public partial class UpdateWindow : Window
{
    readonly Release _rel;
    CancellationTokenSource? _cts;

    public UpdateWindow(Release rel)
    {
        _rel = rel;
        InitializeComponent();
        Heading.Text = $"WinFace {rel.Version} is available";
        Sub.Text = $"You have {Release.Current}" + (rel.Published > DateTime.MinValue ? $"  -  released {rel.Published.ToLocalTime():d MMMM yyyy}" : "") +
                   $"  -  {rel.SetupSize / 1048576.0:0.0} MB";
        foreach (var s in Notes.Parse(rel.Notes))
        {
            if (s.Title.Length > 0)
                NotesPanel.Children.Add(new TextBlock { Text = s.Title, Style = (Style)FindResource("H2"), FontSize = 15, Margin = new Thickness(0, NotesPanel.Children.Count == 0 ? 0 : 14, 0, 6) });
            foreach (var item in s.Items)
            {
                var row = new DockPanel { Margin = new Thickness(0, 2, 0, 2) };
                var dot = new TextBlock { Text = "•", Style = (Style)FindResource("Body"), Foreground = (Brush)FindResource("Accent"), Width = 18 };
                DockPanel.SetDock(dot, Dock.Left);
                row.Children.Add(dot);
                row.Children.Add(new TextBlock { Text = item, Style = (Style)FindResource("Body"), FontSize = 13.5 });
                NotesPanel.Children.Add(row);
            }
        }
    }

    void OnLater(object sender, RoutedEventArgs e) { _cts?.Cancel(); Close(); }

    void OnSkip(object sender, RoutedEventArgs e)
    {
        Prefs.SkipVersion = _rel.Tag;   // background checks stay quiet until a newer version appears
        Close();
    }

    async void OnUpdate(object sender, RoutedEventArgs e)
    {
        UpdateButton.IsEnabled = SkipButton.IsEnabled = false;
        LaterButton.Content = "Cancel";
        ProgressPanel.Visibility = Visibility.Visible;
        ProgressText.Text = "Downloading...";
        _cts = new CancellationTokenSource();
        try
        {
            string setup = await _rel.Download(new Progress<double>(p => { Bar.Value = p; ProgressText.Text = $"Downloading... {p:P0}"; }), _cts.Token);
            ProgressText.Text = "Verified. Starting the installer - Windows will ask for permission.";
            // silent update: keeps faces, password and settings; the installer closes and reopens WinFace
            Process.Start(new ProcessStartInfo(setup, "/SILENT /SUPPRESSMSGBOXES /NORESTART /UPDATE") { UseShellExecute = true });
            Close();
        }
        catch (OperationCanceledException) { Close(); }
        catch (Exception ex)
        {
            ProgressText.Text = ex.Message.Contains("canceled by the user", StringComparison.OrdinalIgnoreCase)
                ? "The update was cancelled (permission not given)." : "Update failed: " + ex.Message;
            ProgressText.Foreground = (Brush)FindResource("Bad");
            UpdateButton.IsEnabled = SkipButton.IsEnabled = true;
            UpdateButton.Content = "Try again";
            LaterButton.Content = "Later";
        }
    }
}
