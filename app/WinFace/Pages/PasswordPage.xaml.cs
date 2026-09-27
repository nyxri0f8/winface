using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace WinFace.Pages;

public partial class PasswordPage : UserControl
{
    public PasswordPage()
    {
        InitializeComponent();
        Loaded += async (_, _) => await Refresh();
    }

    async Task Refresh()
    {
        var st = await Status.Load();
        bool saved = st is { Password: true, Linked: true };
        StateDot.Fill = (Brush)FindResource(saved ? "Good" : "Warn");
        StateText.Text = saved ? "A password is saved and linked to this account." : "No password saved yet.";
        VerifyButton.Visibility = saved ? Visibility.Visible : Visibility.Collapsed;
        FormTitle.Text = saved ? "Update your password" : "Save your password";
    }

    async void OnSave(object sender, RoutedEventArgs e)
    {
        if (Pw1.Password.Length == 0 || Pw1.Password != Pw2.Password)
        {
            SaveText.Text = "The two passwords are empty or different.";
            SaveText.Foreground = (Brush)FindResource("Bad");
            return;
        }
        SaveButton.IsEnabled = false;
        SaveText.Foreground = (Brush)FindResource("SubText");
        SaveText.Text = "Checking with Windows...";
        string pw = Pw1.Password;
        Pw1.Clear();
        Pw2.Clear();
        System.Text.Json.JsonElement? last = null;
        try { await Backend.Stream(["password", "--stdin"], el => last = el, default, pw); }
        catch (Exception ex) { SaveText.Text = ex.Message; }
        pw = "";
        SaveButton.IsEnabled = true;
        if (last is { } j && j.Str("t") == "done" && j.Bool("ok"))
        {
            SaveText.Text = $"Saved for {j.Str("account")}.";
            SaveText.Foreground = (Brush)FindResource("Good");
        }
        else
        {
            SaveText.Text = last is { } er && er.IsError() ? er.Str("msg") : "Not saved.";
            SaveText.Foreground = (Brush)FindResource("Bad");
        }
        await Refresh();
        if (MainWindow.Instance != null) await MainWindow.Instance.RefreshMode();
    }

    async void OnVerify(object sender, RoutedEventArgs e)
    {
        VerifyButton.IsEnabled = false;
        VerifyText.Text = "Asking Windows...";
        var r = await Backend.Run("verify");
        VerifyButton.IsEnabled = true;
        if (r is { } j && j.Str("t") == "done")
            VerifyText.Text = j.Bool("ok") ? $"Windows accepted it ({j.Str("account")})." : "Windows rejected it - save your current password again.";
        else VerifyText.Text = r is { } er && er.IsError() ? er.Str("msg") : "Could not check.";
    }
}
