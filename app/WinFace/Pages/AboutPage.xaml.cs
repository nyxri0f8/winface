using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;

namespace WinFace.Pages;

public partial class AboutPage : UserControl
{
    public AboutPage()
    {
        InitializeComponent();
        var v = Assembly.GetExecutingAssembly().GetName().Version;
        Version.Text = $"Version {v?.ToString(3)}  -  installed in {Backend.Dir.TrimEnd('\\')}";
        AutoUpdate.IsChecked = Updates.Auto;
    }

    void OnRecovery(object sender, RoutedEventArgs e)
    {
        string p = Path.Combine(Backend.Dir, "RECOVERY.md");
        if (File.Exists(p)) System.Diagnostics.Process.Start("notepad.exe", $"\"{p}\"");
        else SystemInfo.Open("https://github.com/nyxri0f8/winface/blob/main/install/RECOVERY.md");
    }

    void OnPolicy(object sender, RoutedEventArgs e)
    {
        string p = Path.Combine(Backend.Dir, "PRIVACY.md");
        if (File.Exists(p)) System.Diagnostics.Process.Start("notepad.exe", $"\"{p}\"");
        else SystemInfo.Open("https://github.com/nyxri0f8/winface/blob/main/PRIVACY.md");
    }

    void OnCheck(object sender, RoutedEventArgs e) => Updates.CheckNow();
    void OnAuto(object sender, RoutedEventArgs e) => Updates.Auto = AutoUpdate.IsChecked == true;

    void OnUninstall(object sender, RoutedEventArgs e) => SystemInfo.Open("ms-settings:appsfeatures");
    void OnGitHub(object sender, RoutedEventArgs e) => SystemInfo.Open("https://github.com/nyxri0f8/winface");
}
