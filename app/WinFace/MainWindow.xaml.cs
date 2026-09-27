using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using WinFace.Pages;

namespace WinFace;

public partial class MainWindow : Window
{
    public static MainWindow? Instance { get; private set; }
    readonly Dictionary<string, Func<UserControl>> _pages = new()
    {
        ["home"] = () => new HomePage(),
        ["faces"] = () => new FacesPage(),
        ["password"] = () => new PasswordPage(),
        ["test"] = () => new TestPage(),
        ["settings"] = () => new SettingsPage(),
        ["logs"] = () => new LogsPage(),
        ["about"] = () => new AboutPage(),
    };

    public MainWindow()
    {
        Instance = this;
        InitializeComponent();
        Loaded += (_, _) =>
        {
            if (AppState.SetupDone) Show("home"); else ShowSetup();
            _ = RefreshMode();
        };
    }

    void OnNav(object sender, RoutedEventArgs e)
    {
        if (IsLoaded && sender is RadioButton { Tag: string tag }) Show(tag);
    }

    /// <summary>Switch page (also selects the sidebar entry). Leaving a page stops anything it was running.</summary>
    public void Go(string tag)
    {
        foreach (var rb in Nav.Children.OfType<RadioButton>())
            if ((string)rb.Tag == tag) { rb.IsChecked = true; return; }
    }

    /// <summary>The guided setup, full window (first launch, after an erase, or from Home).</summary>
    public void ShowSetup()
    {
        if (Page.Content is IDisposable old) old.Dispose();
        Side.Visibility = Visibility.Collapsed;
        SideCol.Width = new GridLength(0);
        Page.Content = new SetupPage();
    }

    public void FinishSetup()
    {
        Side.Visibility = Visibility.Visible;
        SideCol.Width = new GridLength(232);
        if (Nav.Children.OfType<RadioButton>().First() is { } home && home.IsChecked == true) Show("home"); else Go("home");
        _ = RefreshMode();
    }

    void Show(string tag)
    {
        if (Page.Content is IDisposable old) old.Dispose();
        Page.Content = _pages[tag]();
    }

    public async Task RefreshMode()
    {
        var st = await Status.Load();
        (string text, string brush) = st?.Mode switch
        {
            "lock" => ("On", "Good"),
            "test" => ("Test prompt only", "Warn"),
            "off" => ("Off", "Faint"),
            _ => ("Unknown", "Bad"),
        };
        ModeText.Text = text;
        ModeDot.Fill = (Brush)FindResource(brush);
    }
}
