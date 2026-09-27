using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace WinFace.Pages;

public partial class TestPage : UserControl, IDisposable
{
    CancellationTokenSource? _cts;
    bool _running;

    public TestPage() => InitializeComponent();

    public void Dispose() => _cts?.Cancel();

    async void OnStart(object sender, RoutedEventArgs e)
    {
        if (_running) { _cts?.Cancel(); return; }
        _running = true;
        _cts = new CancellationTokenSource();
        StartButton.Content = "Stop";
        Detail.Text = "";
        Detail.Foreground = (Brush)FindResource("SubText");

        var r = await Runs.Test(Mesh, Hint, Arrow, Numbers, _cts.Token);

        _running = false;
        StartButton.Content = "Start again";
        if (r.Cancelled) { Hint.Text = "Stopped."; return; }
        if (r.Last is { } d && d.Str("t") == "done")
        {
            Hint.Text = r.Ok ? "It works - that would unlock." : "Not recognised";
            Detail.Text = $"{r.Message}\n{d.Num("ms") / 1000:0.0} s including camera start ({d.Num("camera_ms"):0} ms)";
            Detail.Foreground = (Brush)FindResource(r.Ok ? "Good" : "SubText");
        }
        else
        {
            Hint.Text = "The test could not run";
            Detail.Text = r.Message;
        }
    }
}
