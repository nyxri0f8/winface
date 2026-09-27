using System.Text.Json;
using System.Windows.Controls;
using Microsoft.Win32;

namespace WinFace;

/// <summary>Face enrolment and face tests, shared by the setup guide, the Faces page and the Test page.</summary>
public static class Runs
{
    public record Result(bool Ok, bool Cancelled, string Message, JsonElement? Last);

    /// <summary>Capture a face (5 poses). <paramref name="onPose"/> gets (pose index, captured, per pose).</summary>
    public static async Task<Result> Enroll(string name, MeshView mesh, TextBlock say, TextBlock msg, Action<int, int, int>? onPose,
                                            CancellationToken ct)
    {
        mesh.Reset();
        say.Text = "Starting camera...";
        msg.Text = "";
        JsonElement? last = null;
        int code;
        try
        {
            code = await Backend.Stream(["enroll", name], el =>
            {
                last = el;
                if (el.Str("t") == "camera") { msg.Text = $"Camera: {el.Str("name")}"; return; }
                if (el.Str("t") != "frame") return;
                int pose = (int)el.Num("pose"), poses = (int)el.Num("poses", 5), got = (int)el.Num("got"), per = (int)el.Num("per", 15);
                say.Text = el.Str("say");
                msg.Text = el.Str("msg");
                mesh.Set(el.Mesh(), (pose * per + got) / (double)(poses * per));
                onPose?.Invoke(pose, got, per);
            }, ct);
        }
        catch (Exception ex) { return new Result(false, false, ex.Message, null); }
        if (code == -1) return new Result(false, true, "Cancelled", last);
        bool ok = last is { } d && d.Str("t") == "done" && d.Bool("ok");
        mesh.SetResult(ok ? 1 : -1);
        string m = ok
            ? (last!.Value.Str("similar_to") is { Length: > 0 } s ? $"Note: this face looks like \"{s}\" - fine if it is you." : $"{(int)last!.Value.Num("captures")} captures saved.")
            : last is { } e && e.IsError() ? e.Str("msg") : "The camera stopped.";
        return new Result(ok, false, m, last);
    }

    /// <summary>The lock screen's face check (never signs in). Ok = it would have unlocked.</summary>
    public static async Task<Result> Test(MeshView mesh, TextBlock hint, TextBlock? arrow, TextBlock? numbers, CancellationToken ct)
    {
        mesh.Reset();
        hint.Text = "Starting camera...";
        if (arrow != null) arrow.Text = "";
        if (numbers != null) numbers.Text = "";
        JsonElement? last = null;
        int code;
        try
        {
            code = await Backend.Stream(["test"], el =>
            {
                last = el;
                if (el.Str("t") != "frame") return;
                hint.Text = el.Str("hint");
                int dir = (int)el.Num("direction");
                if (arrow != null) arrow.Text = dir < 0 ? "" : dir > 0 ? "" : "";   // mirrored: LEFT = arrow left
                mesh.Set(el.Mesh(), el.Num("progress"));
                if (numbers != null)
                    numbers.Text = $"match   {el.Num("score"):0.00}   (needs 0.42)\ntexture {el.Num("texture"):0.00}   (needs 0.60)";
            }, ct);
        }
        catch (Exception ex) { return new Result(false, false, ex.Message, null); }
        if (arrow != null) arrow.Text = "";
        if (code == -1) return new Result(false, true, "Stopped", last);
        if (last is { } d && d.Str("t") == "done")
        {
            bool ok = d.Bool("ok");
            mesh.SetResult(ok ? 1 : -1);
            return new Result(ok, false, d.Str("reason"), last);
        }
        mesh.SetResult(-1);
        return new Result(false, false, last is { } e && e.IsError() ? e.Str("msg") : "The test could not run.", last);
    }
}

/// <summary>App-only preferences (HKCU\Software\WinFace). Face data never goes here.</summary>
public static class AppState
{
    const string Key = @"Software\WinFace";

    public static bool SetupDone
    {
        get => Registry.CurrentUser.OpenSubKey(Key)?.GetValue("SetupDone") is 1;
        set
        {
            using var k = Registry.CurrentUser.CreateSubKey(Key);
            k.SetValue("SetupDone", value ? 1 : 0, RegistryValueKind.DWord);
        }
    }

    /// <summary>The privacy policy + warning version the user agreed to (bump PolicyVersion to ask again).</summary>
    const int PolicyVersion = 1;
    public static bool PolicyAccepted
    {
        get => Registry.CurrentUser.OpenSubKey(Key)?.GetValue("PolicyAccepted") is int v && v >= PolicyVersion;
        set
        {
            using var k = Registry.CurrentUser.CreateSubKey(Key);
            k.SetValue("PolicyAccepted", value ? PolicyVersion : 0, RegistryValueKind.DWord);
        }
    }

    public static void Clear() => Registry.CurrentUser.DeleteSubKeyTree(Key, false);
}
