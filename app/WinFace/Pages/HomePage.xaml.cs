using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace WinFace.Pages;

public partial class HomePage : UserControl
{
    Status? _st;
    bool _systemOk;

    public HomePage()
    {
        InitializeComponent();
        Loaded += async (_, _) => await Refresh();
    }

    async Task Refresh()
    {
        _st = await Status.Load();
        await RunChecks();
        ShowState();
    }

    void ShowState()
    {
        var st = _st;
        if (st == null)
        {
            HeroTitle.Text = "WinFace is not installed correctly";
            HeroSub.Text = "fgsetup.exe did not answer. Reinstall WinFace.";
            return;
        }
        ModeOff.IsChecked = st.Mode == "off";
        ModeTest.IsChecked = st.Mode == "test";
        ModeLock.IsChecked = st.Mode == "lock";
        (HeroTitle.Text, HeroSub.Text, HeroIcon.Foreground) = st.Mode switch
        {
            "lock" => ("Face unlock is on", "Lock your PC (Win + L) and just look at the camera.", (Brush)FindResource("Good")),
            "test" => ("Test mode", "Face unlock only appears in the Windows Security test prompt, not on the lock screen.", (Brush)FindResource("Warn")),
            _ => ("Face unlock is off", "Finish the steps below, then switch it on for the lock screen.", (Brush)FindResource("SubText")),
        };

        bool hasFace = st.Faces.Count > 0, hasPw = st.Password && st.Linked;
        ModeLock.IsEnabled = ModeTest.IsEnabled = hasFace && hasPw;
        ModeHint.Visibility = ModeLock.IsEnabled ? Visibility.Collapsed : Visibility.Visible;
        ModeHint.Text = "Add your face and save your Windows password first.";

        Steps.Children.Clear();
        AddStep("System is ready for face unlock", _systemOk, null, null);
        AddStep("Add your face", hasFace, "Add face", "faces");
        AddStep("Save your Windows password (protected by the TPM)", hasPw, "Save password", "password");
        AddStep("Try it without locking your PC", false, "Run a test", "test", optional: true);
        AddStep("Turn on face unlock for the lock screen", st.Mode == "lock", null, null);
        SetupCard.Visibility = st.Mode == "lock" && hasFace && hasPw && _systemOk ? Visibility.Collapsed : Visibility.Visible;
    }

    void AddStep(string text, bool done, string? action, string? page, bool optional = false)
    {
        var row = new DockPanel { Margin = new Thickness(0, 6, 0, 6) };
        var mark = new TextBlock
        {
            Style = (Style)FindResource("Icon"),
            Text = done ? "" : optional ? "" : "",
            Foreground = (Brush)FindResource(done ? "Good" : "Faint"),
            Width = 28,
        };
        DockPanel.SetDock(mark, Dock.Left);
        row.Children.Add(mark);
        if (action != null && !done)
        {
            var b = new Button { Content = action, Padding = new Thickness(12, 5, 12, 5), FontSize = 12.5 };
            b.Click += (_, _) => MainWindow.Instance?.Go(page!);
            DockPanel.SetDock(b, Dock.Right);
            row.Children.Add(b);
        }
        row.Children.Add(new TextBlock
        {
            Text = text + (optional ? "  (recommended)" : ""),
            Style = (Style)FindResource("Body"),
            Foreground = (Brush)FindResource(done ? "SubText" : "Text"),
            VerticalAlignment = VerticalAlignment.Center,
        });
        Steps.Children.Add(row);
    }

    async Task RunChecks()
    {
        Checks.Children.Clear();
        var sac = SystemInfo.SmartAppControl();
        AddCheck("Smart App Control", sac, sac.Item1 != Check.Ok ? ("Open Windows Security", SystemInfo.OpenSmartAppControlSettings) : null);
        AddCheck("Face unlock component", SystemInfo.Provider(), null);
        AddCheck("Face models", SystemInfo.Models(), null);
        var tpmRow = AddCheck("TPM 2.0", (Check.Unknown, "Checking..."), null);
        var camRow = AddCheck("Camera", (Check.Unknown, "Checking..."), null);

        var tpm = await SystemInfo.Tpm();
        SetCheck(tpmRow, tpm);
        (Check, string) cam = (Check.Bad, "No camera found");
        try
        {
            var e = await Backend.Run("cameras");
            if (e is { } j && j.TryGetProperty("list", out var list))
            {
                var usable = list.EnumerateArray().Where(c => c.Bool("usable")).Select(c => c.Str("name")).ToList();
                bool infraredOnly = usable.Count == 0 && list.EnumerateArray().Any(c => c.Bool("infrared"));
                cam = usable.Count > 0 ? (Check.Ok, string.Join(", ", usable))
                    : infraredOnly ? (Check.Bad, "Only an infrared (Windows Hello) sensor was found - WinFace needs a normal colour camera.")
                    : (Check.Bad, "Only virtual cameras found - a real USB/built-in camera is required.");
            }
        }
        catch (Exception ex) { cam = (Check.Bad, ex.Message); }
        SetCheck(camRow, cam);
        _systemOk = new[] { sac.Item1, SystemInfo.Provider().Item1, SystemInfo.Models().Item1, tpm.Item1, cam.Item1 }.All(c => c != Check.Bad);
    }

    DockPanel AddCheck(string name, (Check, string) state, (string, Action)? fix)
    {
        var row = new DockPanel { Margin = new Thickness(0, 7, 0, 7) };
        var dot = new System.Windows.Shapes.Ellipse { Width = 9, Height = 9, Margin = new Thickness(2, 5, 14, 0), VerticalAlignment = VerticalAlignment.Top };
        DockPanel.SetDock(dot, Dock.Left);
        row.Children.Add(dot);
        if (fix is { } f)
        {
            var b = new Button { Content = f.Item1, Padding = new Thickness(12, 5, 12, 5), FontSize = 12.5, VerticalAlignment = VerticalAlignment.Center };
            b.Click += (_, _) => f.Item2();
            DockPanel.SetDock(b, Dock.Right);
            row.Children.Add(b);
        }
        var sp = new StackPanel();
        sp.Children.Add(new TextBlock { Text = name, Style = (Style)FindResource("Body") });
        sp.Children.Add(new TextBlock { Style = (Style)FindResource("Sub") });
        row.Children.Add(sp);
        Checks.Children.Add(row);
        SetCheck(row, state);
        return row;
    }

    void SetCheck(DockPanel row, (Check, string) state)
    {
        var dot = row.Children.OfType<System.Windows.Shapes.Ellipse>().First();
        dot.Fill = (Brush)FindResource(state.Item1 switch { Check.Ok => "Good", Check.Warn => "Warn", Check.Bad => "Bad", _ => "Faint" });
        ((TextBlock)row.Children.OfType<StackPanel>().First().Children[1]).Text = state.Item2;
    }

    async void OnMode(object sender, RoutedEventArgs e)
    {
        if (sender is not RadioButton { Tag: string mode }) return;
        if (mode == "lock" && SystemInfo.SmartAppControl().Item1 == Check.Bad)
        {
            MessageBox.Show("Smart App Control is on and will block face unlock on the lock screen.\n\nTurn it off in Windows Security > App & browser control > Smart App Control, then try again.",
                            "WinFace", MessageBoxButton.OK, MessageBoxImage.Warning);
            ShowState();
            return;
        }
        var r = await Backend.Run("mode", mode);
        if (r is { } j && j.IsError()) MessageBox.Show(j.Str("msg"), "WinFace", MessageBoxButton.OK, MessageBoxImage.Error);
        _st = await Status.Load();
        ShowState();
        if (MainWindow.Instance != null) await MainWindow.Instance.RefreshMode();
    }

    async void OnRecheck(object sender, RoutedEventArgs e) => await Refresh();

    void OnGuide(object sender, RoutedEventArgs e) => MainWindow.Instance?.ShowSetup();

    async void OnOff(object sender, RoutedEventArgs e)
    {
        await Backend.Run("mode", "off");
        EraseText.Text = "Face unlock is off. Your faces and password are kept - switch it back on above.";
        EraseText.Foreground = (Brush)FindResource("SubText");
        _st = await Status.Load();
        ShowState();
        if (MainWindow.Instance != null) await MainWindow.Instance.RefreshMode();
    }

    async void OnErase(object sender, RoutedEventArgs e)
    {
        if (MessageBox.Show(
                "This permanently deletes from this PC:\n\n" +
                "  •  all enrolled faces\n" +
                "  •  the stored (encrypted) Windows password and its TPM keys\n" +
                "  •  the face unlock log\n" +
                "  •  all WinFace settings\n\n" +
                "Face unlock is switched off. Your Windows PIN and password are not affected, and WinFace stays installed.\n\nErase everything?",
                "Erase all face data", MessageBoxButton.YesNo, MessageBoxImage.Warning, MessageBoxResult.No) != MessageBoxResult.Yes) return;
        EraseText.Text = "Erasing...";
        EraseText.Foreground = (Brush)FindResource("SubText");
        var r = await Backend.Run("erase");
        AppState.Clear();
        if (r is { } j && j.Str("t") == "done")
        {
            int left = (int)j.Num("tpm_keys_left");
            EraseText.Text = left == 0 ? "✓  Everything was erased. Nothing of your face or password is left on this PC."
                                       : $"Faces, password, log and settings erased; {left} old TPM key(s) could not be removed (they cannot decrypt anything without the deleted password file).";
            EraseText.Foreground = (Brush)FindResource(left == 0 ? "Good" : "Warn");
        }
        else
        {
            EraseText.Text = r is { } er && er.IsError() ? er.Str("msg") : "Erase failed.";
            EraseText.Foreground = (Brush)FindResource("Bad");
        }
        _st = await Status.Load();
        ShowState();
        if (MainWindow.Instance != null) await MainWindow.Instance.RefreshMode();
    }
}
