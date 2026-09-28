using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Windows;

namespace WinFace;

/// <summary>Runs fgsetup.exe (installed next to WinFace.exe) in --json mode: one JSON object per stdout line.</summary>
public static class Backend
{
    public static string Dir => AppContext.BaseDirectory;
    public static string Exe => Path.Combine(Dir, "fgsetup.exe");
    public static string DataDir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "WinFace");
    public static string LogPath => Path.Combine(DataDir, "log.txt");

    /// <summary>Run a command and stream every JSON line to <paramref name="onLine"/> on the UI thread.
    /// Returns the process exit code. Cancelling kills fgsetup (which closes the camera).</summary>
    public static async Task<int> Stream(IEnumerable<string> args, Action<JsonElement>? onLine, CancellationToken ct = default,
                                         string? stdinLine = null)
    {
        if (!File.Exists(Exe)) throw new FileNotFoundException("fgsetup.exe is missing - reinstall WinFace.", Exe);
        var psi = new ProcessStartInfo(Exe)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            RedirectStandardInput = stdinLine != null,
            StandardOutputEncoding = Encoding.UTF8,
            WorkingDirectory = Dir,
        };
        foreach (var a in args) psi.ArgumentList.Add(a);
        psi.ArgumentList.Add("--json");

        using var p = Process.Start(psi) ?? throw new InvalidOperationException("could not start fgsetup.exe");
        if (stdinLine != null)
        {
            // the password travels only over this private pipe, never on a command line
            var bytes = new UTF8Encoding(false).GetBytes(stdinLine + "\n");
            await p.StandardInput.BaseStream.WriteAsync(bytes, CancellationToken.None);
            Array.Clear(bytes);
            p.StandardInput.Close();
        }
        using var reg = ct.Register(() => { try { if (!p.HasExited) p.Kill(true); } catch { } });
        _ = p.StandardError.ReadToEndAsync();
        string? line;
        while ((line = await p.StandardOutput.ReadLineAsync()) != null)
        {
            if (line.Length == 0 || line[0] != '{' || onLine == null) continue;
            JsonElement el;
            try { el = JsonDocument.Parse(line).RootElement.Clone(); } catch (JsonException) { continue; }
            Application.Current.Dispatcher.Invoke(() => onLine(el));
        }
        await p.WaitForExitAsync(CancellationToken.None);
        return ct.IsCancellationRequested ? -1 : p.ExitCode;
    }

    /// <summary>Run a command and return its last JSON object (status / cameras / done / error).</summary>
    public static async Task<JsonElement?> Run(params string[] args)
    {
        JsonElement? last = null;
        await Stream(args, el => last = el);
        return last;
    }

    public static string Str(this JsonElement e, string name, string def = "") =>
        e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? def : def;
    public static bool Bool(this JsonElement e, string name) =>
        e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.True;
    public static double Num(this JsonElement e, string name, double def = 0) =>
        e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.Number ? v.GetDouble() : def;
    public static bool IsError(this JsonElement e) => e.Str("t") == "error";

    /// <summary>Landmarks as (x, y) pairs, centred on the face, face height = 1.</summary>
    public static float[] Mesh(this JsonElement e)
    {
        if (!e.TryGetProperty("mesh", out var m) || m.ValueKind != JsonValueKind.Array) return [];
        var a = new float[m.GetArrayLength()];
        int i = 0;
        foreach (var v in m.EnumerateArray()) a[i++] = v.GetSingle();
        return a;
    }
}

public record Status(string Mode, bool Linked, bool Password, string Camera, int SearchMs, int ChallengeMs, int MaxFails,
                     int Strictness, bool Sounds, List<(string Name, int Captures)> Faces)
{
    // security (WinFace app -> Security)
    public bool Action { get; init; } = true;
    public int Flash { get; init; } = 1;              // 0 off, 1 measure, 2 enforce
    public int ExtraChecks { get; init; } = 1;        // 0 never, 1 randomly 2-3 a day + after 2 failures, 2 every unlock
    public bool PinAfterRestart { get; init; } = true;
    public int PinAfterHours { get; init; } = 48;     // 0 = never
    public bool IntruderPhotos { get; init; }
    public bool EventsTask { get; init; }
    public bool CameraPinned { get; init; }
    public int Fails { get; init; }
    public int Intruders { get; init; }
    public string Locked { get; init; } = "";         // why face unlock waits for the PIN right now ("" = it doesn't)
    public string CameraChanged { get; init; } = "";

    public static Status From(JsonElement e)
    {
        var faces = new List<(string, int)>();
        if (e.TryGetProperty("faces", out var f) && f.ValueKind == JsonValueKind.Array)
            foreach (var x in f.EnumerateArray()) faces.Add((x.Str("name"), (int)x.Num("captures")));
        return new Status(e.Str("mode", "off"), e.Bool("linked"), e.Bool("password"), e.Str("camera"), (int)e.Num("search_ms", 7000),
                          (int)e.Num("challenge_ms", 3000), (int)e.Num("max_fails", 3), (int)e.Num("strictness"), e.Bool("sounds"), faces)
        {
            Action = !e.TryGetProperty("action", out _) || e.Bool("action"),
            Flash = (int)e.Num("flash", 1),
            ExtraChecks = (int)e.Num("extra_checks", 1),
            PinAfterRestart = !e.TryGetProperty("pin_after_restart", out _) || e.Bool("pin_after_restart"),
            PinAfterHours = (int)e.Num("pin_after_hours", 48),
            IntruderPhotos = e.Bool("intruder_photos"),
            EventsTask = e.Bool("events_task"),
            CameraPinned = e.Bool("camera_pinned"),
            Fails = (int)e.Num("fails"),
            Intruders = (int)e.Num("intruders"),
            Locked = e.Str("locked"),
            CameraChanged = e.Str("camera_changed"),
        };
    }

    public static async Task<Status?> Load()
    {
        var e = await Backend.Run("status");
        return e is { } j && j.Str("t") == "status" ? From(j) : null;
    }
}
