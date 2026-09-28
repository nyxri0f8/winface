# WinFace developer installer - run from an ADMINISTRATOR PowerShell (or double-click install.cmd, which asks for admin).
# End users: use WinFace-Setup.exe instead (installer\build.ps1), which also installs the WinFace app.
# Installs to C:\Program Files\WinFace (admin-only writable, required because LogonUI loads the DLL as SYSTEM)
# and registers the credential provider. A fresh install is registered DISABLED (turn it on with: fgsetup mode test);
# an update keeps your enrolled faces, stored password and settings exactly as they are.
#
#   install.ps1              install / update
#   install.ps1 -Check       only run the pre-flight checks (no admin needed, changes nothing)
#   install.ps1 -StageTo D   copy + verify into folder D instead (no admin, no registry) - a dry run of the file part
param([switch]$Check, [string]$StageTo)
$ErrorActionPreference = 'Stop'
$repo  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo 'build\Release'
$dest  = if ($StageTo) { $StageTo } else { Join-Path $env:ProgramFiles 'WinFace' }
$oldDest = Join-Path $env:ProgramFiles 'FaceGate'   # install folder before v1.0 (old name)
$data  = Join-Path $env:ProgramData 'WinFace'
$clsid = '{C188DC15-E41E-4CCF-9DA9-8238E1D0BBDF}'
$fails = 0
function Ok($msg)   { Write-Host "  [ OK ] $msg" -ForegroundColor Green }
function Bad($msg)  { Write-Host "  [FAIL] $msg" -ForegroundColor Red; $script:fails++ }
function Test($cond, $msg) { if ($cond) { Ok $msg } else { Bad $msg } }

# Everything that gets installed: source -> path under $dest
$files = [ordered]@{
    (Join-Path $build 'WinFaceCP.dll')   = 'WinFaceCP.dll'
    (Join-Path $build 'fgsetup.exe')      = 'fgsetup.exe'
    (Join-Path $build 'fgcredtest.exe')   = 'fgcredtest.exe'
    (Join-Path $build 'fgsound.exe')      = 'fgsound.exe'
    (Join-Path $repo 'third_party\onnxruntime-win-x64-1.24.4\lib\onnxruntime.dll') = 'onnxruntime.dll'
    (Join-Path $repo 'assets\sfx_unlock.wav') = 'assets\sfx_unlock.wav'
    (Join-Path $repo 'assets\tile.bmp')       = 'assets\tile.bmp'
    (Join-Path $repo 'install\RECOVERY.md')   = 'RECOVERY.md'
}
foreach ($m in 'face_detector.onnx', 'face_landmarks_detector.onnx', 'arcface_int8.onnx', 'fas_v1se_s4.0.onnx',
               'fas_v2_s2.7.onnx', 'canonical_face.bin') {
    $files[(Join-Path $repo "models\runtime\$m")] = "models\$m"
}

# ---------------------------------------------------------------- pre-flight (changes nothing)
Write-Host "Pre-flight checks"
foreach ($src in $files.Keys) { Test (Test-Path $src) "found $($files[$src])" }
if ($fails) { throw "missing files - build first (see README) and re-run" }

# the build must be newer than the provider sources, or you would install an old DLL
$dllTime = (Get-Item (Join-Path $build 'WinFaceCP.dll')).LastWriteTime
$newer = Get-ChildItem (Join-Path $repo 'cp'), (Join-Path $repo 'engine') -File | Where-Object { $_.LastWriteTime -gt $dllTime }
Test (-not $newer) "WinFaceCP.dll is up to date with the source$(if ($newer) { ' (rebuild: ' + ($newer.Name -join ', ') + ')' })"

# the only sound must be a PCM WAV (PlaySound cannot play MP3)
$wav = [IO.File]::ReadAllBytes((Join-Path $repo 'assets\sfx_unlock.wav'))
$isPcm = [Text.Encoding]::ASCII.GetString($wav, 0, 4) -eq 'RIFF' -and [Text.Encoding]::ASCII.GetString($wav, 8, 4) -eq 'WAVE' -and
         [BitConverter]::ToUInt16($wav, 20) -eq 1
Test $isPcm "sfx_unlock.wav is a PCM WAV ($([math]::Round($wav.Length / 1KB)) KB)"

# 64-bit DLL (LogonUI is 64-bit)
$pe = [IO.File]::ReadAllBytes((Join-Path $build 'WinFaceCP.dll'))
$peOff = [BitConverter]::ToInt32($pe, 0x3C)
Test ([BitConverter]::ToUInt16($pe, $peOff + 4) -eq 0x8664) "WinFaceCP.dll is x64"

# optional deep check with the dev tool: load the DLL and create the provider exactly like LogonUI would
$probe = Join-Path $build 'fgoverlaytest.exe'
if (Test-Path $probe) {
    $out = & $probe dll (Join-Path $build 'WinFaceCP.dll') 2>&1
    Test ($LASTEXITCODE -eq 0) "provider DLL loads and creates its COM object"
    if ($LASTEXITCODE -ne 0) { $out | ForEach-Object { Write-Host "         $_" } }
}
if ($fails) { throw "pre-flight failed - nothing was changed" }
if ($Check) { Write-Host "`nPre-flight passed. Nothing was changed."; return }

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $StageTo -and -not $admin) { throw "run this from an Administrator PowerShell (or double-click install\install.cmd)" }

# ---------------------------------------------------------------- copy
Write-Host "`nInstalling to $dest"
New-Item -ItemType Directory -Force -Path $dest, "$dest\models", "$dest\assets" | Out-Null
# if LogonUI currently has the old DLL loaded, move it aside instead of failing (it is cleaned up on a later install)
$dll = Join-Path $dest 'WinFaceCP.dll'
Get-ChildItem $dest -Filter 'WinFaceCP.old.*.dll' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
if (Test-Path $dll) {
    try { Remove-Item $dll -Force } catch { Rename-Item $dll ("WinFaceCP.old." + [guid]::NewGuid().ToString('N') + ".dll") }
}
# older versions: scan/fail sounds (now silent) and the neon mesh outline (no longer drawn)
Remove-Item "$dest\assets\sfx_scan.wav", "$dest\assets\sfx_fail.wav", "$dest\models\mesh_edges.bin" -Force -ErrorAction SilentlyContinue
foreach ($src in $files.Keys) { Copy-Item $src (Join-Path $dest $files[$src]) -Force }

# ---------------------------------------------------------------- verify the copy
Write-Host "Verifying installed files"
foreach ($src in $files.Keys) {
    $dst = Join-Path $dest $files[$src]
    $same = (Test-Path $dst) -and (Get-FileHash $src).Hash -eq (Get-FileHash $dst).Hash
    Test $same "$($files[$src])"
}
Test (-not (Test-Path "$dest\assets\sfx_scan.wav") -and -not (Test-Path "$dest\assets\sfx_fail.wav")) "old scan/fail sounds removed"
if ($StageTo) {
    if ($fails) { throw "staging verification failed" }
    Write-Host "`nStaged and verified in $dest (no registry changes)."
    return
}

# ---------------------------------------------------------------- data folder + registration
# before v1.0 the product was called FaceGate: bring its faces, password and settings over to the WinFace locations
& (Join-Path $dest 'fgsetup.exe') migrate
# the SYSTEM task that reports PIN / password sign-ins (for the PIN-after-restart / after-48-hours rules)
& (Join-Path $dest 'fgsetup.exe') events-task install
# Data folder: SYSTEM + Administrators full control; Users may read (the CredUI test runs as you) and append the log.
New-Item -ItemType Directory -Force -Path $data | Out-Null
icacls $data /inheritance:r /grant:r 'SYSTEM:(OI)(CI)F' 'Administrators:(OI)(CI)F' 'Users:(OI)(CI)RX' | Out-Null
$log = Join-Path $data 'log.txt'
if (-not (Test-Path $log)) { New-Item -ItemType File -Path $log | Out-Null }
icacls $log /grant 'Users:M' | Out-Null

# COM registration + credential provider registration
New-Item -Force -Path "HKLM:\SOFTWARE\Classes\CLSID\$clsid" -Value 'WinFace' | Out-Null
New-Item -Force -Path "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32" -Value $dll | Out-Null
Set-ItemProperty -Path "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32" -Name ThreadingModel -Value 'Apartment'
New-Item -Force -Path "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$clsid" -Value 'WinFace' | Out-Null

# settings: a fresh install is registered but OFF until you choose a mode; an update keeps your settings
$fresh = -not (Test-Path 'HKLM:\SOFTWARE\WinFace')
if ($fresh) { New-Item -Path 'HKLM:\SOFTWARE\WinFace' | Out-Null }
if ($null -eq (Get-ItemProperty 'HKLM:\SOFTWARE\WinFace' -Name Enabled -ErrorAction SilentlyContinue)) {
    Set-ItemProperty 'HKLM:\SOFTWARE\WinFace' -Name Enabled -Value 0 -Type DWord
}

Write-Host "Verifying registration"
Test ((Get-Item "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32").GetValue('') -eq $dll) "COM server points at $dll"
Test ((Get-ItemProperty "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32").ThreadingModel -eq 'Apartment') "threading model"
Test (Test-Path "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$clsid") "registered as a credential provider"
# the registration now points at $dest: remove the pre-1.0 folder (the DLL may still be loaded until a restart)
if ((Test-Path $oldDest) -and ($oldDest -ne $dest)) {
    Remove-Item -Recurse -Force $oldDest -ErrorAction SilentlyContinue
    if (Test-Path $oldDest) { Write-Host "  (old folder $oldDest is still in use - delete it after a restart)" }
}
$cfg = Get-ItemProperty 'HKLM:\SOFTWARE\WinFace'
Test ((Get-Acl $data).Access | Where-Object { $_.IdentityReference -match 'SYSTEM' }) "data folder permissions"

Write-Host ""
if ($fails) { Write-Host "Installed with $fails problem(s) - see [FAIL] lines above." -ForegroundColor Red; exit 1 }
$enabled = $cfg.Enabled -eq 1
$mode = if (-not $enabled) { 'DISABLED' } elseif (($cfg.Scenarios -band 2) -ne 0) { 'ON for the lock screen' } else { 'ON for the test prompt only' }
Write-Host "WinFace installed and verified. Face unlock is $mode." -ForegroundColor Green
if (-not $enabled -or $fresh) {
    Write-Host "Next (same admin terminal):"
    Write-Host "  & '$dest\fgsetup.exe' enroll $env:USERNAME"
    Write-Host "  & '$dest\fgsetup.exe' password"
    Write-Host "  & '$dest\fgsetup.exe' mode test"
    Write-Host "Then in a NORMAL terminal:  & '$dest\fgcredtest.exe'"
} else {
    Write-Host "Your faces, password and settings were kept. The new version is used from the next lock (Win+L) or restart."
}
