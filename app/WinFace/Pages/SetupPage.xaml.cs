using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace WinFace.Pages;

/// <summary>First-run guide: welcome + privacy, system check, face, security tests, password, trial, turn on.</summary>
public partial class SetupPage : UserControl, IDisposable
{
    static readonly string[] StepNames = ["Welcome", "Check", "Face", "Security", "Password", "Try it", "Turn on", "Done"];
    static readonly string[] Poses = ["Straight", "Left", "Right", "Up", "Down"];

    record SecTest(string Title, string How, string Icon, bool ShouldUnlock);
    static readonly SecTest[] SecTests =
    [
        new("Your face", "Look at the camera and turn your head when asked. This must unlock.", "", true),
        new("A photo of you", "Hold a printed photo of your face, or one on your phone, in front of the camera.", "", false),
        new("A video of you", "Play a video of your face on a phone or tablet in front of the camera.", "", false),
        new("Someone else, or you moving", "Ask another person to sit in front of the camera - or keep moving and turning away.", "", false),
    ];

    FrameworkElement[] _steps = [];
    int _step;
    bool _busy, _systemOk, _faceDone, _pwDone, _tryDone, _onDone;
    readonly bool?[] _passed = new bool?[4];
    readonly (TextBlock Status, Button Start)[] _cards = new (TextBlock, Button)[4];
    CancellationTokenSource? _cts;

    public SetupPage()
    {
        InitializeComponent();
        _steps = [S0, S1, S2, S3, S4, S5, S6, S7];
        BuildTests();
        BuildPoses(-1, 0, 15);
        Agree.IsChecked = AppState.PolicyAccepted;
        Loaded += (_, _) => Show(0);
    }

    public void Dispose() => _cts?.Cancel();

    // ---------------------------------------------------------------- navigation
    async void Show(int step)
    {
        _step = step;
        for (int i = 0; i < _steps.Length; ++i) _steps[i].Visibility = i == step ? Visibility.Visible : Visibility.Collapsed;
        BuildRail();
        UpdateButtons();   // right away: the step's own checks below take a moment (a double-click must not skip a step)
        switch (step)
        {
            case 1: await RunChecks(); break;
            case 2:
                var st = await Status.Load();
                if (st?.Faces.Count > 0 && !_faceDone)
                {
                    _faceDone = true;
                    FaceSay.Text = $"You already have {st.Faces.Count} face(s) saved. Continue, or capture again with Start.";
                    FaceButton.Content = "Capture again";
                }
                break;
            case 4:
                var s4 = await Status.Load();
                if (s4 is { Password: true, Linked: true }) { _pwDone = true; PwSaved.Visibility = Visibility.Visible; }
                break;
            case 7:
                var s7 = await Status.Load();
                DoneSub.Text = s7?.Mode == "lock" ? "Face unlock is on for the lock screen." : "Face unlock is not on yet - switch it on from Home whenever you like.";
                break;
        }
        UpdateButtons();
    }

    void UpdateButtons()
    {
        BackButton.Visibility = _step is > 0 and < 7 ? Visibility.Visible : Visibility.Collapsed;
        SkipButton.Visibility = _step < 7 ? Visibility.Visible : Visibility.Collapsed;
        BackButton.IsEnabled = !_busy;
        NextButton.IsEnabled = !_busy && _step switch
        {
            0 => Agree.IsChecked == true,
            1 => _systemOk,
            2 => _faceDone,
            3 => _passed[0] == true,
            4 => _pwDone,
            _ => true,
        };
        NextButton.Content = _step switch
        {
            0 => "Start setup",
            5 => _tryDone ? "Next" : "Skip this step",
            6 => _onDone ? "Next" : "Not now",
            7 => "Open WinFace",
            _ => "Next",
        };
    }

    void BuildRail()
    {
        Rail.Children.Clear();
        for (int i = 0; i < StepNames.Length; ++i)
        {
            bool done = i < _step, now = i == _step;
            var sp = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(i == 0 ? 0 : 6, 0, 0, 0) };
            sp.Children.Add(new Border
            {
                Width = 20, Height = 20, CornerRadius = new CornerRadius(10),
                Background = (Brush)FindResource(now ? "Accent" : done ? "Surface2" : "Surface"),
                BorderBrush = (Brush)FindResource(done ? "Good" : "Line"), BorderThickness = new Thickness(1),
                Child = new TextBlock
                {
                    Text = done ? "" : (i + 1).ToString(), FontSize = done ? 10 : 11,
                    FontFamily = (FontFamily)FindResource(done ? "Icons" : "Ui"),
                    Foreground = (Brush)FindResource(now ? "Text" : done ? "Good" : "Faint"),
                    HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
                },
            });
            if (now) sp.Children.Add(new TextBlock { Text = StepNames[i], Style = (Style)FindResource("Body"), FontSize = 12.5, Margin = new Thickness(6, 0, 4, 0), VerticalAlignment = VerticalAlignment.Center });
            Rail.Children.Add(sp);
        }
    }

    void OnAgree(object sender, RoutedEventArgs e) => UpdateButtons();

    void OnPolicy(object sender, RoutedEventArgs e)
    {
        string p = Path.Combine(Backend.Dir, "PRIVACY.md");
        if (File.Exists(p)) Process.Start("notepad.exe", $"\"{p}\"");
        else SystemInfo.Open("https://github.com/nyxri0f8/winface/blob/main/PRIVACY.md");
    }

    void OnNext(object sender, RoutedEventArgs e)
    {
        if (_step == 0) AppState.PolicyAccepted = true;
        if (_step == 7) { AppState.SetupDone = true; MainWindow.Instance?.FinishSetup(); return; }
        Show(_step + 1);
    }

    void OnBack(object sender, RoutedEventArgs e) { if (_step > 0) Show(_step - 1); }

    void OnSkip(object sender, RoutedEventArgs e)
    {
        _cts?.Cancel();
        MainWindow.Instance?.FinishSetup();
    }

    void SetBusy(bool busy)
    {
        _busy = busy;
        UpdateButtons();
    }

    // ---------------------------------------------------------------- 1 system check
    async Task RunChecks()
    {
        _systemOk = false;
        UpdateButtons();
        Checks.Children.Clear();
        CheckNote.Text = "";
        var sac = SystemInfo.SmartAppControl();
        var prov = SystemInfo.Provider();
        var models = SystemInfo.Models();
        AddCheck("Smart App Control", sac, sac.Item1 != Check.Ok ? ("Open Windows Security", SystemInfo.OpenSmartAppControlSettings) : null);
        AddCheck("Face unlock component", prov, null);
        AddCheck("Face models", models, null);
        var tpmRow = AddCheck("TPM 2.0 security chip", (Check.Unknown, "Checking..."), null);
        var camRow = AddCheck("Camera", (Check.Unknown, "Checking..."), null);
        var tpm = await SystemInfo.Tpm();
        SetCheck(tpmRow, tpm);
        (Check, string) cam = (Check.Bad, "No camera found");
        try
        {
            var r = await Backend.Run("cameras");
            if (r is { } j && j.TryGetProperty("list", out var list))
            {
                var usable = list.EnumerateArray().Where(c => c.Bool("usable")).Select(c => c.Str("name")).ToList();
                bool infraredOnly = usable.Count == 0 && list.EnumerateArray().Any(c => c.Bool("infrared"));
                cam = usable.Count > 0 ? (Check.Ok, string.Join(", ", usable))
                    : infraredOnly ? (Check.Bad, "Only an infrared (Windows Hello) sensor was found - WinFace needs a normal colour camera.")
                    : (Check.Bad, "Only virtual cameras found - a real USB or built-in camera is needed.");
            }
        }
        catch (Exception ex) { cam = (Check.Bad, ex.Message); }
        SetCheck(camRow, cam);
        _systemOk = new[] { sac.Item1, prov.Item1, models.Item1, tpm.Item1, cam.Item1 }.All(c => c != Check.Bad);
        if (!_systemOk) CheckNote.Text = "Fix the red items, then click \"Check again\".";
        UpdateButtons();
    }

    async void OnRecheck(object sender, RoutedEventArgs e) => await RunChecks();

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
        row.Children.OfType<System.Windows.Shapes.Ellipse>().First().Fill =
            (Brush)FindResource(state.Item1 switch { Check.Ok => "Good", Check.Warn => "Warn", Check.Bad => "Bad", _ => "Faint" });
        ((TextBlock)row.Children.OfType<StackPanel>().First().Children[1]).Text = state.Item2;
    }

    // ---------------------------------------------------------------- 2 face
    async void OnFace(object sender, RoutedEventArgs e)
    {
        if (_busy) { _cts?.Cancel(); return; }
        _cts = new CancellationTokenSource();
        SetBusy(true);
        FaceButton.Content = "Cancel";
        string name = Environment.UserName.ToLowerInvariant();
        if (name.Length > 32) name = name[..32];
        var r = await Runs.Enroll(name, FaceMesh, FaceSay, FaceMsg, BuildPoses, _cts.Token);
        SetBusy(false);
        if (r.Cancelled) { FaceSay.Text = "Cancelled. Press Start when you are ready."; FaceButton.Content = "Start"; return; }
        _faceDone = _faceDone || r.Ok;
        FaceSay.Text = r.Ok ? "Face saved." : "That did not work.";
        FaceMsg.Text = r.Message;
        FaceButton.Content = r.Ok ? "Capture again" : "Try again";
        if (r.Ok) BuildPoses(5, 0, 15);
        UpdateButtons();
    }

    void BuildPoses(int current, int got, int per)
    {
        PoseList.Children.Clear();
        for (int i = 0; i < Poses.Length; ++i)
        {
            bool done = i < current, now = i == current;
            var row = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 3, 0, 3) };
            row.Children.Add(new TextBlock
            {
                Style = (Style)FindResource("Icon"), FontSize = 14, Width = 24,
                Text = done ? "" : now ? "" : "",
                Foreground = (Brush)FindResource(done ? "Good" : now ? "Accent" : "Faint"),
            });
            row.Children.Add(new TextBlock { Text = Poses[i] + (now ? $"   {got}/{per}" : ""), Style = (Style)FindResource("Body"), Foreground = (Brush)FindResource(now ? "Text" : "SubText") });
            PoseList.Children.Add(row);
        }
    }

    // ---------------------------------------------------------------- 3 security tests
    void BuildTests()
    {
        for (int i = 0; i < SecTests.Length; ++i)
        {
            var t = SecTests[i];
            int idx = i;
            var card = new Border { Style = (Style)FindResource("Card"), Padding = new Thickness(16), Margin = new Thickness(0, 0, 0, 10) };
            var dp = new DockPanel();
            var icon = new Border
            {
                Width = 38, Height = 38, CornerRadius = new CornerRadius(10), Background = (Brush)FindResource("Surface2"), VerticalAlignment = VerticalAlignment.Top,
                Child = new TextBlock { Style = (Style)FindResource("Icon"), Text = t.Icon, HorizontalAlignment = HorizontalAlignment.Center },
            };
            DockPanel.SetDock(icon, Dock.Left);
            dp.Children.Add(icon);
            var start = new Button { Content = "Start", Padding = new Thickness(14, 6, 14, 6), VerticalAlignment = VerticalAlignment.Top, FontSize = 13 };
            start.Click += async (_, _) => await RunTest(idx);
            DockPanel.SetDock(start, Dock.Right);
            dp.Children.Add(start);
            var text = new StackPanel { Margin = new Thickness(12, 0, 10, 0) };
            text.Children.Add(new TextBlock { Text = $"{i + 1}. {t.Title}" + (i == 0 ? "" : "  (should NOT unlock)"), Style = (Style)FindResource("Body"), FontWeight = FontWeights.SemiBold });
            text.Children.Add(new TextBlock { Text = t.How, Style = (Style)FindResource("Sub"), FontSize = 12.5, Margin = new Thickness(0, 2, 0, 6) });
            var status = new TextBlock { Text = "Not run yet", Style = (Style)FindResource("Sub"), FontSize = 12.5, Foreground = (Brush)FindResource("Faint") };
            text.Children.Add(status);
            dp.Children.Add(text);
            card.Child = dp;
            Tests.Children.Add(card);
            _cards[i] = (status, start);
        }
    }

    async Task RunTest(int i)
    {
        if (_busy) { _cts?.Cancel(); return; }
        _cts = new CancellationTokenSource();
        SetBusy(true);
        foreach (var c in _cards) c.Start.IsEnabled = false;
        _cards[i].Start.IsEnabled = true;
        _cards[i].Start.Content = "Stop";
        _cards[i].Status.Text = "Running... " + SecTests[i].How;
        _cards[i].Status.Foreground = (Brush)FindResource("Accent");
        var r = await Runs.Test(SecMesh, SecHint, SecArrow, null, _cts.Token);
        foreach (var c in _cards) { c.Start.IsEnabled = true; c.Start.Content = "Run again"; }
        SetBusy(false);
        if (r.Cancelled) { _cards[i].Status.Text = "Stopped"; _cards[i].Status.Foreground = (Brush)FindResource("Faint"); return; }

        bool expected = SecTests[i].ShouldUnlock;
        bool pass = r.Ok == expected;
        _passed[i] = pass;
        (_cards[i].Status.Text, string brush) = (expected, r.Ok) switch
        {
            (true, true) => ("✓  Unlocked - correct.", "Good"),
            (true, false) => ($"✗  Did not unlock ({r.Message}). Try again in good light, straight in front of the camera.", "Warn"),
            (false, false) => ($"✓  Rejected - correct. ({r.Message})", "Good"),
            (false, true) => ("✗  It UNLOCKED - this should not happen. See below.", "Bad"),
        };
        _cards[i].Status.Foreground = (Brush)FindResource(brush);
        SecHint.Text = pass ? "Correct result" : expected ? "Not recognised" : "This should not have unlocked";
        SecWarn.Visibility = Enumerable.Range(1, 3).Any(k => _passed[k] == false) ? Visibility.Visible : Visibility.Collapsed;
        UpdateButtons();
    }

    async void OnStrict(object sender, RoutedEventArgs e)
    {
        await Backend.Run("set", "Strictness", "1");
        for (int i = 0; i < 4; ++i)
        {
            _passed[i] = null;
            _cards[i].Status.Text = "Not run yet (Strict)";
            _cards[i].Status.Foreground = (Brush)FindResource("Faint");
        }
        SecWarn.Visibility = Visibility.Collapsed;
        UpdateButtons();
    }

    // ---------------------------------------------------------------- 4 password
    async void OnPassword(object sender, RoutedEventArgs e)
    {
        if (Pw1.Password.Length == 0 || Pw1.Password != Pw2.Password)
        {
            PwText.Text = "The two passwords are empty or different.";
            PwText.Foreground = (Brush)FindResource("Bad");
            return;
        }
        SetBusy(true);
        PwButton.IsEnabled = false;
        PwText.Foreground = (Brush)FindResource("SubText");
        PwText.Text = "Checking with Windows...";
        string pw = Pw1.Password;
        Pw1.Clear();
        Pw2.Clear();
        System.Text.Json.JsonElement? last = null;
        try { await Backend.Stream(["password", "--stdin"], el => last = el, default, pw); }
        catch (Exception ex) { PwText.Text = ex.Message; }
        pw = "";
        PwButton.IsEnabled = true;
        SetBusy(false);
        if (last is { } j && j.Str("t") == "done" && j.Bool("ok"))
        {
            _pwDone = true;
            PwText.Text = $"Saved for {j.Str("account")}.";
            PwText.Foreground = (Brush)FindResource("Good");
        }
        else
        {
            PwText.Text = last is { } er && er.IsError() ? er.Str("msg") : "Not saved.";
            PwText.Foreground = (Brush)FindResource("Bad");
        }
        UpdateButtons();
    }

    // ---------------------------------------------------------------- 5 try it
    async void OnTry(object sender, RoutedEventArgs e)
    {
        string exe = Path.Combine(Backend.Dir, "fgcredtest.exe");
        if (!File.Exists(exe)) { TryText.Text = "fgcredtest.exe is missing - reinstall WinFace."; return; }
        SetBusy(true);
        TryButton.IsEnabled = false;
        TryText.Foreground = (Brush)FindResource("SubText");
        TryText.Text = "Waiting for the Windows Security prompt...";
        await Backend.Run("mode", "test");   // face unlock appears only in this prompt for now
        string output = "";
        int code = -1;
        try
        {
            var psi = new ProcessStartInfo(exe) { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, WorkingDirectory = Backend.Dir };
            using var p = Process.Start(psi)!;
            output = await p.StandardOutput.ReadToEndAsync();
            await p.WaitForExitAsync();
            code = p.ExitCode;
        }
        catch (Exception ex) { output = ex.Message; }
        TryButton.IsEnabled = true;
        SetBusy(false);
        string result = output.Split('\n').Select(l => l.Trim()).LastOrDefault(l => l.StartsWith("RESULT:"))?[7..].Trim() ?? "";
        (TryText.Text, string brush) = code switch
        {
            0 => ("It works: Windows accepted the sign-in. " + result, "Good"),
            1 => ("The prompt was closed. Try again and choose Face unlock.", "Warn"),
            2 => ("A PIN or Windows Hello was used - choose Face unlock instead.", "Warn"),
            _ => (result.Length > 0 ? result : "It did not work. Check the password step, then try again.", "Bad"),
        };
        TryText.Foreground = (Brush)FindResource(brush);
        _tryDone = code == 0;
        UpdateButtons();
    }

    // ---------------------------------------------------------------- 6 turn on
    async void OnTurnOn(object sender, RoutedEventArgs e)
    {
        if (SystemInfo.SmartAppControl().Item1 == Check.Bad)
        {
            OnText.Text = "Smart App Control is on and would block face unlock - turn it off in Windows Security first.";
            OnText.Foreground = (Brush)FindResource("Bad");
            return;
        }
        var r = await Backend.Run("mode", "lock");
        _onDone = !(r is { } j && j.IsError());
        OnText.Text = _onDone ? "✓  Face unlock is on for the lock screen." : "Could not turn it on.";
        OnText.Foreground = (Brush)FindResource(_onDone ? "Good" : "Bad");
        if (MainWindow.Instance != null) await MainWindow.Instance.RefreshMode();
        UpdateButtons();
    }
}
