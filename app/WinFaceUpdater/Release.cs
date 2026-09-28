using System.IO;
using System.Net.Http;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using Microsoft.Win32;

namespace WinFaceUpdater;

/// <summary>The latest WinFace release on GitHub, and how to install it.</summary>
public sealed record Release(Version Version, string Tag, string Title, DateTime Published, string Notes, string SetupUrl, long SetupSize,
                             string? ShaUrl)
{
    const string Api = "https://api.github.com/repos/nyxri0f8/winface/releases/latest";

    public static Version Current => Assembly.GetExecutingAssembly().GetName().Version is { } v ? new Version(v.Major, v.Minor, v.Build) : new Version(0, 0, 0);

    static HttpClient Client()
    {
        var c = new HttpClient { Timeout = TimeSpan.FromMinutes(10) };
        // GitHub requires a user agent; nothing about the PC or the user is sent
        c.DefaultRequestHeaders.UserAgent.ParseAdd($"WinFace-Updater/{Current}");
        return c;
    }

    public static async Task<Release?> Latest(CancellationToken ct = default)
    {
        using var c = Client();
        c.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
        using var doc = JsonDocument.Parse(await c.GetStringAsync(Api, ct));
        var r = doc.RootElement;
        string tag = r.GetProperty("tag_name").GetString() ?? "";
        if (!Version.TryParse(tag.TrimStart('v', 'V'), out var v)) return null;
        string? setup = null, sha = null;
        long size = 0;
        foreach (var a in r.GetProperty("assets").EnumerateArray())
        {
            string name = a.GetProperty("name").GetString() ?? "";
            string url = a.GetProperty("browser_download_url").GetString() ?? "";
            if (name.StartsWith("WinFace-Setup", StringComparison.OrdinalIgnoreCase) && name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
            { setup = url; size = a.GetProperty("size").GetInt64(); }
            else if (name.EndsWith(".sha256", StringComparison.OrdinalIgnoreCase)) sha = url;
        }
        if (setup == null) return null;
        DateTime.TryParse(r.TryGetProperty("published_at", out var p) ? p.GetString() : null, out var published);
        return new Release(new Version(v.Major, v.Minor, Math.Max(0, v.Build)), tag, r.GetProperty("name").GetString() ?? tag,
                           published, r.GetProperty("body").GetString() ?? "", setup, size, sha);
    }

    /// <summary>Download the setup to %TEMP%, check where it came from and its SHA-256, return the file path.</summary>
    public async Task<string> Download(IProgress<double> progress, CancellationToken ct)
    {
        static void OnlyGitHub(string url)
        {
            var host = new Uri(url).Host;
            if (host != "github.com" && !host.EndsWith(".githubusercontent.com", StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException($"refusing to download from {host}");
        }
        OnlyGitHub(SetupUrl);
        using var c = Client();
        string expected = "";
        if (ShaUrl != null)
        {
            OnlyGitHub(ShaUrl);
            expected = (await c.GetStringAsync(ShaUrl, ct)).Trim().Split(' ', '\t', '\r', '\n')[0];
        }
        else
        {   // fallback: the hash printed in the release notes
            var m = System.Text.RegularExpressions.Regex.Match(Notes, "[0-9a-fA-F]{64}");
            if (m.Success) expected = m.Value;
        }
        if (expected.Length != 64) throw new InvalidOperationException("the release has no SHA-256 fingerprint - not installing it");

        string dir = Path.Combine(Path.GetTempPath(), "WinFaceUpdate");
        Directory.CreateDirectory(dir);
        string file = Path.Combine(dir, Path.GetFileName(new Uri(SetupUrl).LocalPath));
        using (var resp = await c.GetAsync(SetupUrl, HttpCompletionOption.ResponseHeadersRead, ct))
        {
            resp.EnsureSuccessStatusCode();
            long total = resp.Content.Headers.ContentLength ?? SetupSize;
            await using var src = await resp.Content.ReadAsStreamAsync(ct);
            await using var dst = File.Create(file);
            var buf = new byte[81920];
            long done = 0;
            int n;
            while ((n = await src.ReadAsync(buf, ct)) > 0)
            {
                await dst.WriteAsync(buf.AsMemory(0, n), ct);
                done += n;
                if (total > 0) progress.Report((double)done / total);
            }
        }
        string actual;
        await using (var fs = File.OpenRead(file)) actual = Convert.ToHexString(await SHA256.HashDataAsync(fs, ct));
        if (!actual.Equals(expected, StringComparison.OrdinalIgnoreCase))
        {
            File.Delete(file);
            throw new InvalidOperationException("the download does not match the release's SHA-256 fingerprint - not installing it");
        }
        return file;
    }
}

/// <summary>Per-user updater preferences (HKCU\Software\WinFace).</summary>
public static class Prefs
{
    const string Key = @"Software\WinFace";
    static object? Get(string n) => Registry.CurrentUser.OpenSubKey(Key)?.GetValue(n);
    static void Set(string n, object v, RegistryValueKind k)
    {
        using var key = Registry.CurrentUser.CreateSubKey(Key);
        key.SetValue(n, v, k);
    }

    public static bool AutoUpdate { get => Get("AutoUpdate") is not 0; set => Set("AutoUpdate", value ? 1 : 0, RegistryValueKind.DWord); }
    public static string SkipVersion { get => Get("SkipVersion") as string ?? ""; set => Set("SkipVersion", value, RegistryValueKind.String); }
    public static DateTime LastCheck
    {
        get => Get("LastUpdateCheck") is long t ? DateTime.FromBinary(t) : DateTime.MinValue;
        set => Set("LastUpdateCheck", value.ToBinary(), RegistryValueKind.QWord);
    }
}

/// <summary>Turns the release notes (Markdown) into sections of plain bullet points for the dialog.</summary>
public static class Notes
{
    public record Section(string Title, List<string> Items);

    public static List<Section> Parse(string md)
    {
        var list = new List<Section>();
        Section? cur = null;
        foreach (var raw in md.Replace("\r", "").Replace("﻿", "").Split('\n'))   // a stray BOM would hide the first heading
        {
            string line = raw.Trim();
            if (line.Length == 0 || line.StartsWith("SHA-256", StringComparison.OrdinalIgnoreCase)) continue;
            // plain text: [text](url) -> text, no bold / code marks
            string clean = System.Text.RegularExpressions.Regex.Replace(line, @"\[([^\]]+)\]\([^)]+\)", "$1").Replace("**", "").Replace("`", "");
            if (line.StartsWith('#'))
            {
                cur = new Section(clean.TrimStart('#', ' '), []);
                list.Add(cur);
            }
            else
            {
                if (cur == null) { cur = new Section("", []); list.Add(cur); }
                cur.Items.Add(clean.TrimStart('-', '*', ' '));
            }
        }
        return list.Where(s => s.Items.Count > 0).ToList();
    }
}
