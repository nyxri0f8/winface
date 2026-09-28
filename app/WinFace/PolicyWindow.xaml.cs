using System.IO;
using System.Windows;

namespace WinFace;

/// <summary>Shown once when the privacy policy / warning version changed after setup was already done.</summary>
public partial class PolicyWindow : Window
{
    public PolicyWindow()
    {
        InitializeComponent();
        string p = Path.Combine(Backend.Dir, "PRIVACY.md");
        Policy.Text = File.Exists(p) ? File.ReadAllText(p) : "See https://github.com/nyxri0f8/winface/blob/main/PRIVACY.md";
    }

    void OnAgree(object sender, RoutedEventArgs e) => Continue.IsEnabled = Agree.IsChecked == true;

    void OnContinue(object sender, RoutedEventArgs e)
    {
        AppState.PolicyAccepted = true;
        DialogResult = true;
    }

    void OnQuit(object sender, RoutedEventArgs e) => DialogResult = false;
}
