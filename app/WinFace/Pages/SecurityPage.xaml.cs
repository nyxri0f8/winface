using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace WinFace.Pages;

public partial class SecurityPage : UserControl
{
    static readonly (string Label, int Hours)[] HourChoices = [("24 hours", 24), ("48 hours", 48), ("72 hours", 72), ("Never", 0)];
    Status? _st;
    bool _loading;

    public SecurityPage()
    {
        InitializeComponent();
        foreach (var (label, h) in HourChoices)
        {
            var rb = new RadioButton { Style = (Style)FindResource("Seg"), GroupName = "hours", Content = label, Tag = h };
            rb.Click += OnSave;
            Hours.Children.Add(rb);
        }
        Loaded += async (_, _) => await Load();
    }

    async Task Load()
    {
        _loading = true;
        _st = await Status.Load();
        var st = _st;
        if (st == null) { StatusTitle.Text = "Could not read the settings."; _loading = false; return; }

        bool paused = st.Locked.Length > 0;
        StatusDot.Fill = (Brush)FindResource(st.Mode != "lock" ? "Faint" : paused ? "Warn" : "Good");
        StatusTitle.Text = st.Mode != "lock" ? "Face unlock is not on for the lock screen" : paused ? "Face unlock is paused" : "Face unlock is available";
        StatusSub.Text = paused ? st.Locked + ". Signing in with your PIN or password makes it available again." :
                         $"{st.Fails} failed attempt(s) since the last successful sign-in.";
        ResetButton.Visibility = paused ? Visibility.Visible : Visibility.Collapsed;

        CameraCard.Visibility = st.CameraChanged.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
        CameraText.Text = $"Face unlock found \"{st.CameraChanged}\" instead of the camera your face was added with, and is paused until you confirm it. If you did not plug in or change a camera, do not confirm - check the PC.";

        Action.IsChecked = st.Action;
        ExtraRandom.IsChecked = st.ExtraChecks == 1;
        ExtraAlways.IsChecked = st.ExtraChecks == 2;
        ExtraNever.IsChecked = st.ExtraChecks == 0;
        FlashOff.IsChecked = st.Flash == 0;
        FlashMeasure.IsChecked = st.Flash == 1;
        FlashEnforce.IsChecked = st.Flash == 2;
        PinRestart.IsChecked = st.PinAfterRestart;
        foreach (RadioButton rb in Hours.Children) rb.IsChecked = (int)rb.Tag == st.PinAfterHours;
        FailsText.Text = $"After {st.MaxFails} failed attempts (change in Settings) face unlock pauses until you use your PIN.";
        TaskWarn.Visibility = st.EventsTask ? Visibility.Collapsed : Visibility.Visible;
        Intruders.IsChecked = st.IntruderPhotos;
        _loading = false;
        await LoadPhotos();
    }

    async Task LoadPhotos()
    {
        Photos.Children.Clear();
        var r = await Backend.Run("intruders");
        var ids = new List<(string Id, string Time)>();
        if (r is { } j && j.TryGetProperty("list", out var list))
            foreach (var p in list.EnumerateArray()) ids.Add((p.Str("id"), p.Str("time")));
        PhotosText.Text = ids.Count == 0 ? "No intruder photos." : $"{ids.Count} photo(s), newest first:";
        DeleteAll.Visibility = ids.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
        foreach (var (id, time) in ids) Photos.Children.Add(await PhotoCard(id, time));
    }

    async Task<UIElement> PhotoCard(string id, string time)
    {
        var sp = new StackPanel { Width = 220, Margin = new Thickness(0, 0, 12, 12) };
        var img = new Image { Height = 124, Stretch = Stretch.UniformToFill };
        var r = await Backend.Run("intruders", "get", id);
        string reason = "";
        if (r is { } j && j.Str("t") == "photo")
        {
            reason = j.Str("reason");
            var bytes = Convert.FromBase64String(j.Str("jpeg"));
            var bmp = new BitmapImage();
            using (var ms = new MemoryStream(bytes))
            {
                bmp.BeginInit();
                bmp.CacheOption = BitmapCacheOption.OnLoad;   // decoded in memory only; never written to disk
                bmp.StreamSource = ms;
                bmp.EndInit();
            }
            Array.Clear(bytes);
            img.Source = bmp;
        }
        else if (r is { } e && e.IsError()) reason = e.Str("msg");
        sp.Children.Add(new Border { CornerRadius = new CornerRadius(10), ClipToBounds = true, Background = (Brush)FindResource("Surface2"), Child = img });
        var row = new DockPanel { Margin = new Thickness(0, 6, 0, 0) };
        var del = new Button { Content = "Delete", Style = (Style)FindResource("LinkButton"), VerticalAlignment = VerticalAlignment.Center };
        del.Click += async (_, _) => { await Backend.Run("intruders", "delete", id); await LoadPhotos(); };
        DockPanel.SetDock(del, Dock.Right);
        row.Children.Add(del);
        row.Children.Add(new TextBlock { Text = time, Style = (Style)FindResource("Body"), FontSize = 12.5 });
        sp.Children.Add(row);
        sp.Children.Add(new TextBlock { Text = reason, Style = (Style)FindResource("Sub"), FontSize = 11.5, TextTrimming = TextTrimming.CharacterEllipsis });
        return sp;
    }

    async void OnSave(object sender, RoutedEventArgs e)
    {
        if (_loading || _st == null) return;
        int flash = FlashEnforce.IsChecked == true ? 2 : FlashMeasure.IsChecked == true ? 1 : 0;
        int hours = Hours.Children.OfType<RadioButton>().FirstOrDefault(r => r.IsChecked == true)?.Tag as int? ?? _st.PinAfterHours;
        var changes = new List<(string, string)>();
        if ((Action.IsChecked == true) != _st.Action) changes.Add(("Action", Action.IsChecked == true ? "1" : "0"));
        int extra = ExtraAlways.IsChecked == true ? 2 : ExtraNever.IsChecked == true ? 0 : 1;
        if (extra != _st.ExtraChecks) changes.Add(("ExtraChecks", extra.ToString()));
        if (flash != _st.Flash) changes.Add(("FlashCheck", flash.ToString()));
        if ((PinRestart.IsChecked == true) != _st.PinAfterRestart) changes.Add(("PinAfterRestart", PinRestart.IsChecked == true ? "1" : "0"));
        if (hours != _st.PinAfterHours) changes.Add(("PinAfterHours", hours.ToString()));
        await Apply(changes);
    }

    async Task Apply(List<(string, string)> changes)
    {
        foreach (var (name, value) in changes)
        {
            var r = await Backend.Run("set", name, value);
            if (r is { } j && j.IsError()) { Say(j.Str("msg"), "Bad"); await Load(); return; }
        }
        if (changes.Count > 0) Say("Saved. Applies from the next lock screen.", "Good");
        await Load();
    }

    void Say(string text, string brush)
    {
        SaveText.Text = text;
        SaveText.Foreground = (Brush)FindResource(brush);
    }

    async void OnIntruders(object sender, RoutedEventArgs e)
    {
        if (_loading || _st == null) return;
        bool on = Intruders.IsChecked == true;
        if (on && MessageBox.Show(
                "When face unlock fails, the lock screen will keep a photo of whoever was in front of the camera - which may be someone else. " +
                "Photos are encrypted and never leave this PC, but in some places photographing other people needs their consent.\n\nTurn intruder photos on?",
                "Intruder photos", MessageBoxButton.YesNo, MessageBoxImage.Question, MessageBoxResult.No) != MessageBoxResult.Yes)
        {
            Intruders.IsChecked = false;
            return;
        }
        await Apply([("IntruderPhotos", on ? "1" : "0")]);
    }

    async void OnDeleteAll(object sender, RoutedEventArgs e)
    {
        if (MessageBox.Show("Delete all intruder photos?", "WinFace", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
        await Backend.Run("intruders", "delete", "all");
        await LoadPhotos();
    }

    async void OnReset(object sender, RoutedEventArgs e)
    {
        await Backend.Run("unlock-reset");
        Say("Face unlock is available again.", "Good");
        await Load();
    }

    async void OnUseCamera(object sender, RoutedEventArgs e)
    {
        // find the connected camera with that name and pin it
        var r = await Backend.Run("cameras");
        string? id = null;
        if (r is { } j && j.TryGetProperty("list", out var list))
            id = list.EnumerateArray().Where(c => c.Bool("usable") && c.Str("name") == _st?.CameraChanged).Select(c => c.Str("id")).FirstOrDefault();
        if (id == null) { Say("That camera is not connected right now - choose one in Settings.", "Warn"); return; }
        await Apply([("Camera", id)]);
        if (MainWindow.Instance != null) await MainWindow.Instance.RefreshMode();
    }

    void OnSettings(object sender, RoutedEventArgs e) => MainWindow.Instance?.Go("settings");

    async void OnInstallTask(object sender, RoutedEventArgs e)
    {
        var r = await Backend.Run("events-task", "install");
        Say(r is { } j && j.IsError() ? j.Str("msg") : "Installed.", r is { } k && k.IsError() ? "Bad" : "Good");
        await Load();
    }
}
