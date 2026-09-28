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
        ["security"] = () => new SecurityPage(),
        ["settings"] = () => new SettingsPage(),
        ["logs"] = () => new LogsPage(),
        ["about"] = () => new AboutPage(),
    };

    public MainWindow()
    {
        Instance = this;
        InitializeComponent();
        Loaded += async (_, _) => await Start();
    }

    // The guide opens only on a PC that is not set up yet (and not after "Finish later"). A PC that is already set up
    // counts as done even if the guide was never clicked to the end. A newer privacy policy only asks for agreement.
    async Task Start()
    {
        var st = await Status.Load();
        bool configured = st is { Password: true, Linked: true } && st.Faces.Count > 0 && st.Mode != "off";
        if (!AppState.SetupDone && configured) AppState.SetupDone = true;
        if (!AppState.SetupDone && !AppState.SetupSkipped)
        {
            ShowSetup();
        }
        else
        {
            Show("home");
            if (!AppState.PolicyAccepted && new PolicyWindow { Owner = this }.ShowDialog() != true) { Close(); return; }
        }
        _ = RefreshMode();
        Updates.CheckInBackground();
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

    /// <summary>Show the flash colours on the engine's timeline (from fgsetup's "flash" event) over the whole window.
    /// The window is maximised for the flash, so more light reaches the face.</summary>
    public async void Flash(double startInMs, double baseMs, double onMs, double gapMs, string c1, string c2)
    {
        var prev = WindowState;
        WindowState = WindowState.Maximized;
        var bc = new BrushConverter();
        await Task.Delay(TimeSpan.FromMilliseconds(Math.Max(0, startInMs + baseMs)));
        FlashLayer.Background = (Brush)bc.ConvertFromString(c1)!;
        FlashLayer.Visibility = Visibility.Visible;
        await Task.Delay(TimeSpan.FromMilliseconds(onMs));
        FlashLayer.Visibility = Visibility.Collapsed;
        await Task.Delay(TimeSpan.FromMilliseconds(gapMs));
        FlashLayer.Background = (Brush)bc.ConvertFromString(c2)!;
        FlashLayer.Visibility = Visibility.Visible;
        await Task.Delay(TimeSpan.FromMilliseconds(onMs));
        FlashLayer.Visibility = Visibility.Collapsed;
        WindowState = prev;
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
