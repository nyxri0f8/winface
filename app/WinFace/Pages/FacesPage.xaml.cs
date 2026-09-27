using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace WinFace.Pages;

public partial class FacesPage : UserControl, IDisposable
{
    static readonly string[] Poses = ["Straight", "Left", "Right", "Up", "Down"];
    CancellationTokenSource? _cts;
    bool _running;

    public FacesPage()
    {
        InitializeComponent();
        NameBox.Text = Environment.UserName.ToLowerInvariant();
        Loaded += async (_, _) => await LoadFaces();
    }

    public void Dispose() => _cts?.Cancel();

    async Task LoadFaces()
    {
        var st = await Status.Load();
        FaceList.Children.Clear();
        var faces = st?.Faces ?? [];
        Empty.Visibility = faces.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        foreach (var (name, captures) in faces) FaceList.Children.Add(FaceRow(name, captures));
        AddCard.IsEnabled = faces.Count < 3;
        if (faces.Count >= 3) NameBox.Text = "";
        else if (faces.Any(f => f.Name == NameBox.Text)) NameBox.Text = faces.Count == 1 ? "glasses" : "";
    }

    UIElement FaceRow(string name, int captures)
    {
        var row = new DockPanel { Margin = new Thickness(12, 10, 12, 10) };
        var icon = new Border
        {
            Width = 40, Height = 40, CornerRadius = new CornerRadius(12), Background = (Brush)FindResource("Surface2"),
            Child = new TextBlock { Style = (Style)FindResource("Icon"), Text = "", HorizontalAlignment = HorizontalAlignment.Center },
        };
        DockPanel.SetDock(icon, Dock.Left);
        row.Children.Add(icon);
        var del = new Button { Content = "Delete", Style = (Style)FindResource("DangerButton"), Margin = new Thickness(8, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center };
        del.Click += async (_, _) => await Delete(name);
        DockPanel.SetDock(del, Dock.Right);
        row.Children.Add(del);
        var upd = new Button { Content = "Update", VerticalAlignment = VerticalAlignment.Center };
        upd.Click += async (_, _) => await Enroll(name, update: true);
        DockPanel.SetDock(upd, Dock.Right);
        row.Children.Add(upd);
        var text = new StackPanel { Margin = new Thickness(14, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center };
        text.Children.Add(new TextBlock { Text = name, Style = (Style)FindResource("Body"), FontWeight = FontWeights.SemiBold });
        text.Children.Add(new TextBlock { Text = $"{captures} captures", Style = (Style)FindResource("Sub"), FontSize = 12 });
        row.Children.Add(text);
        return row;
    }

    async Task Delete(string name)
    {
        if (MessageBox.Show($"Delete the face \"{name}\"?", "WinFace", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
        var r = await Backend.Run("remove", name);
        if (r is { } j && j.IsError()) MessageBox.Show(j.Str("msg"), "WinFace", MessageBoxButton.OK, MessageBoxImage.Error);
        await LoadFaces();
    }

    async void OnAdd(object sender, RoutedEventArgs e)
    {
        string name = NameBox.Text.Trim();
        if (name.Length is < 1 or > 32) { MessageBox.Show("Please enter a name (1-32 characters).", "WinFace"); return; }
        await Enroll(name, update: false);
    }

    async Task Enroll(string name, bool update)
    {
        if (_running) return;
        _running = true;
        _cts = new CancellationTokenSource();
        ListPane.Visibility = Visibility.Collapsed;
        EnrollView.Visibility = Visibility.Visible;
        EnrollTitle.Text = update ? $"Updating \"{name}\"" : $"Adding \"{name}\"";
        EnrollButton.Content = "Cancel";
        Say.Text = "Starting camera...";
        Msg.Text = "";
        Mesh.Reset();
        BuildPoses(0, 0, 15);

        JsonElement? last = null;
        int code;
        try
        {
            code = await Backend.Stream(["enroll", name], el =>
            {
                last = el;
                if (el.Str("t") != "frame") return;
                int pose = (int)el.Num("pose"), poses = (int)el.Num("poses", 5), got = (int)el.Num("got"), per = (int)el.Num("per", 15);
                Say.Text = el.Str("say");
                Msg.Text = el.Str("msg");
                Mesh.Set(el.Mesh(), (pose * per + got) / (double)(poses * per));
                BuildPoses(pose, got, per);
            }, _cts.Token);
        }
        catch (Exception ex) { code = 1; last = null; Msg.Text = ex.Message; }

        _running = false;
        if (code == -1) { ShowList(); return; }   // cancelled
        if (last is { } d && d.Str("t") == "done" && d.Bool("ok"))
        {
            Mesh.SetResult(1);
            Say.Text = "Face saved";
            string similar = d.Str("similar_to");
            Msg.Text = similar.Length > 0 ? $"Note: this face looks like \"{similar}\" - that is fine if it is you." : $"{(int)d.Num("captures")} captures.";
            BuildPoses(5, 0, 15);
        }
        else
        {
            Mesh.SetResult(-1);
            Say.Text = "Could not add the face";
            if (last is { } er && er.IsError()) Msg.Text = er.Str("msg");
        }
        EnrollButton.Content = "Done";
    }

    void BuildPoses(int current, int got, int per)
    {
        PoseList.Children.Clear();
        for (int i = 0; i < Poses.Length; ++i)
        {
            bool done = i < current, now = i == current;
            var row = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 4, 0, 4) };
            row.Children.Add(new TextBlock
            {
                Style = (Style)FindResource("Icon"), FontSize = 14, Width = 24,
                Text = done ? "" : now ? "" : "",
                Foreground = (Brush)FindResource(done ? "Good" : now ? "Accent" : "Faint"),
            });
            row.Children.Add(new TextBlock
            {
                Text = Poses[i] + (now ? $"   {got}/{per}" : ""),
                Style = (Style)FindResource("Body"),
                Foreground = (Brush)FindResource(now ? "Text" : "SubText"),
            });
            PoseList.Children.Add(row);
        }
    }

    async void OnCancel(object sender, RoutedEventArgs e)
    {
        if (_running) { _cts?.Cancel(); return; }
        ShowList();
        await LoadFaces();
    }

    void ShowList()
    {
        EnrollView.Visibility = Visibility.Collapsed;
        ListPane.Visibility = Visibility.Visible;
        _ = LoadFaces();
    }
}
