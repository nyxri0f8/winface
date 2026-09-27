using System.Diagnostics;
using System.IO;
using Microsoft.Win32;

namespace WinFace;

public enum Check { Ok, Warn, Bad, Unknown }

/// <summary>Read-only checks of the things face unlock depends on. Nothing here changes the system.</summary>
public static class SystemInfo
{
    public const string Clsid = "{C188DC15-E41E-4CCF-9DA9-8238E1D0BBDF}";

    /// <summary>Smart App Control: 0 off, 1 on (blocks the unsigned lock-screen DLL), 2 evaluation.</summary>
    public static (Check, string) SmartAppControl()
    {
        using var k = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Control\CI\Policy");
        return k?.GetValue("VerifiedAndReputablePolicyState") switch
        {
            0 => (Check.Ok, "Off"),
            1 => (Check.Bad, "On - it blocks the face unlock component. Turn it off in Windows Security."),
            2 => (Check.Warn, "Evaluation - Windows may switch it on later and block face unlock."),
            _ => (Check.Ok, "Not present on this version of Windows"),
        };
    }

    public static (Check, string) Provider()
    {
        using var cp = Registry.LocalMachine.OpenSubKey($@"SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{Clsid}");
        using var com = Registry.LocalMachine.OpenSubKey($@"SOFTWARE\Classes\CLSID\{Clsid}\InprocServer32");
        var dll = com?.GetValue("") as string;
        if (cp == null || dll == null) return (Check.Bad, "Not registered - reinstall WinFace.");
        return File.Exists(dll) ? (Check.Ok, "Registered") : (Check.Bad, $"Registered, but {dll} is missing - reinstall WinFace.");
    }

    public static (Check, string) Models()
    {
        string md = Path.Combine(Backend.Dir, "models");
        string[] need = ["face_detector.onnx", "face_landmarks_detector.onnx", "fas_v1se_s4.0.onnx", "fas_v2_s2.7.onnx", "canonical_face.bin"];
        var missing = need.Where(n => !File.Exists(Path.Combine(md, n))).ToList();
        bool arc = File.Exists(Path.Combine(md, "w600k_r50.onnx")) || File.Exists(Path.Combine(md, "arcface_int8.onnx"));
        if (!arc) missing.Add("face recognition model (w600k_r50.onnx)");
        return missing.Count == 0 ? (Check.Ok, "All installed") : (Check.Bad, "Missing: " + string.Join(", ", missing) + " - reinstall WinFace.");
    }

    /// <summary>TPM 2.0 holds the key that protects the stored password. Uses PowerShell Get-Tpm (needs admin).</summary>
    public static async Task<(Check, string)> Tpm()
    {
        try
        {
            var psi = new ProcessStartInfo("powershell.exe")
            { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true };
            foreach (var a in new[] { "-NoProfile", "-NonInteractive", "-Command",
                                      "$t = Get-Tpm; Write-Output ([string]$t.TpmPresent + ' ' + [string]$t.TpmReady)" })
                psi.ArgumentList.Add(a);
            using var p = Process.Start(psi)!;
            string o = (await p.StandardOutput.ReadToEndAsync()).Trim();
            await p.WaitForExitAsync();
            return o switch
            {
                "True True" => (Check.Ok, "Ready"),
                "True False" => (Check.Warn, "Present but not ready - open tpm.msc to prepare it."),
                "False False" or "False True" => (Check.Bad, "No TPM found - the password cannot be stored securely."),
                _ => (Check.Unknown, "Could not check"),
            };
        }
        catch { return (Check.Unknown, "Could not check"); }
    }

    public static void Open(string target)
    {
        try { Process.Start(new ProcessStartInfo(target) { UseShellExecute = true }); } catch { }
    }

    public static void OpenSmartAppControlSettings() => Open("windowsdefender://appbrowser/");
}
