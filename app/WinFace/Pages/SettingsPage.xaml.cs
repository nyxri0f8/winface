using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace WinFace.Pages;

public partial class SettingsPage : UserControl
{
    record Cam(string Name, string Id, bool Usable);
    Status? _st;

    public SettingsPage()
    {
        InitializeComponent();
        for (int i = 1; i <= 5; ++i)
            Fails.Children.Add(new RadioButton { Style = (Style)FindResource("Seg"), GroupName = "fails", Content = i.ToString(), Tag = i });
        Search.ValueChanged += (_, _) => SearchVal.Text = $"{Search.Value:0.0} s";
        Challenge.ValueChanged += (_, _) => ChallengeVal.Text = $"{Challenge.Value:0.0} s";
        Loaded += async (_, _) => await Load();
    }

    async Task Load()
    {
        _st = await Status.Load();
        if (_st == null) { SaveText.Text = "Could not read the settings."; return; }
        Search.Value = _st.SearchMs / 1000.0;
        Challenge.Value = _st.ChallengeMs / 1000.0;
        StrictBalanced.IsChecked = _st.Strictness == 0;
        StrictStrict.IsChecked = _st.Strictness == 1;
        StrictRelaxed.IsChecked = _st.Strictness == 2;
        foreach (RadioButton rb in Fails.Children) rb.IsChecked = (int)rb.Tag == _st.MaxFails;
        Sounds.IsChecked = _st.Sounds;
        SearchVal.Text = $"{Search.Value:0.0} s";
        ChallengeVal.Text = $"{Challenge.Value:0.0} s";

        Cameras.Items.Clear();
        var auto = new Cam("Automatic", "auto", true);
        Cameras.Items.Add(CamItem(auto, "The first colour camera that works; remembered when you add a face", true));
        bool any = false;
        var e = await Backend.Run("cameras");
        if (e is { } j && j.TryGetProperty("list", out var list))
            foreach (var c in list.EnumerateArray())
            {
                any = true;
                var cam = new Cam(c.Str("name"), c.Str("id"), c.Bool("usable"));
                string note = cam.Usable ? "Colour camera" : c.Bool("infrared") ? "Infrared (Windows Hello) sensor - not supported, needs a colour camera"
                                                                              : "Virtual camera - not allowed";
                var item = CamItem(cam, note, cam.Usable);
                Cameras.Items.Add(item);
                if (cam.Usable && _st.Camera.Length > 0 && cam.Id.Contains(_st.Camera, StringComparison.OrdinalIgnoreCase)) item.IsSelected = true;
            }
        if (Cameras.SelectedItem == null)
        {
            if (_st.Camera.Length == 0) ((ListBoxItem)Cameras.Items[0]).IsSelected = true;
            else SaveText.Text = "The camera chosen earlier is not connected - pick one above and Save.";
        }
        if (!any) Cameras.Items.Add(new ListBoxItem { Content = new TextBlock { Text = "No cameras found", Style = (Style)FindResource("Sub") }, IsEnabled = false });
    }

    ListBoxItem CamItem(Cam cam, string note, bool enabled)
    {
        var sp = new StackPanel();
        sp.Children.Add(new TextBlock { Text = cam.Name, Style = (Style)FindResource("Body") });
        sp.Children.Add(new TextBlock { Text = note, Style = (Style)FindResource("Sub"), FontSize = 12 });
        return new ListBoxItem { Content = sp, Tag = cam, IsEnabled = enabled };
    }

    async void OnSave(object sender, RoutedEventArgs e)
    {
        if (_st == null) return;
        SaveButton.IsEnabled = false;
        var changes = new List<(string, string)>();
        int strict = StrictStrict.IsChecked == true ? 1 : StrictRelaxed.IsChecked == true ? 2 : 0;
        int fails = Fails.Children.OfType<RadioButton>().FirstOrDefault(r => r.IsChecked == true)?.Tag as int? ?? _st.MaxFails;
        int search = (int)Math.Round(Search.Value * 1000), challenge = (int)Math.Round(Challenge.Value * 1000);
        if (search != _st.SearchMs) changes.Add(("SearchMs", search.ToString()));
        if (challenge != _st.ChallengeMs) changes.Add(("ChallengeMs", challenge.ToString()));
        if (strict != _st.Strictness) changes.Add(("Strictness", strict.ToString()));
        if (fails != _st.MaxFails) changes.Add(("MaxFails", fails.ToString()));
        if ((Sounds.IsChecked == true) != _st.Sounds) changes.Add(("Sounds", Sounds.IsChecked == true ? "1" : "0"));
        if (Cameras.SelectedItem is ListBoxItem { Tag: Cam cam })
        {
            if (cam.Id == "auto") { if (_st.Camera.Length > 0) changes.Add(("Camera", "auto")); }
            else if (_st.Camera.Length == 0 || !cam.Id.Contains(_st.Camera, StringComparison.OrdinalIgnoreCase)) changes.Add(("Camera", cam.Id));
        }

        string? error = null;
        foreach (var (name, value) in changes)
        {
            var r = await Backend.Run("set", name, value);
            if (r is { } j && j.IsError()) { error = $"{name}: {j.Str("msg")}"; break; }
        }
        SaveButton.IsEnabled = true;
        SaveText.Text = error ?? (changes.Count == 0 ? "Nothing changed." : "Saved.");
        SaveText.Foreground = (Brush)FindResource(error != null ? "Bad" : "Good");
        await Load();
    }

    void OnPlay(object sender, RoutedEventArgs e)
    {
        // exactly how the lock screen plays it
        string exe = Path.Combine(Backend.Dir, "fgsound.exe");
        if (File.Exists(exe)) Process.Start(new ProcessStartInfo(exe) { UseShellExecute = false, CreateNoWindow = true });
    }
}
