# FaceGate installer - run from an ADMINISTRATOR PowerShell.
# Installs to C:\Program Files\FaceGate (admin-only writable, required because LogonUI loads the DLL as SYSTEM)
# and registers the credential provider DISABLED. Turn it on with: fgsetup mode test
#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'
$repo  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo 'build\Release'
$dest  = Join-Path $env:ProgramFiles 'FaceGate'
$data  = Join-Path $env:ProgramData 'FaceGate'
$clsid = '{C188DC15-E41E-4CCF-9DA9-8238E1D0BBDF}'

foreach ($f in 'FaceGateCP.dll', 'fgsetup.exe') {
    if (-not (Test-Path (Join-Path $build $f))) { throw "missing $f - build first" }
}

Write-Host "Installing to $dest"
New-Item -ItemType Directory -Force -Path $dest, "$dest\models", "$dest\assets", $data | Out-Null
# if LogonUI currently has the old DLL loaded, move it aside instead of failing
$dll = Join-Path $dest 'FaceGateCP.dll'
if (Test-Path $dll) {
    try { Remove-Item $dll -Force } catch { Rename-Item $dll ("FaceGateCP.old." + [guid]::NewGuid().ToString('N') + ".dll") }
}
Copy-Item (Join-Path $build 'FaceGateCP.dll'), (Join-Path $build 'fgsetup.exe'), (Join-Path $build 'fgcredtest.exe') $dest -Force
Copy-Item (Join-Path $repo 'third_party\onnxruntime-win-x64-1.24.4\lib\onnxruntime.dll') $dest -Force
Copy-Item (Join-Path $repo 'models\runtime\*') "$dest\models" -Force
Copy-Item (Join-Path $repo 'assets\*.wav'), (Join-Path $repo 'assets\tile.bmp') "$dest\assets" -Force
Copy-Item (Join-Path $repo 'install\RECOVERY.md') $dest -Force

# Data folder: SYSTEM + Administrators full control; Users may read (the CredUI test runs as you) and append the log.
icacls $data /inheritance:r /grant:r 'SYSTEM:(OI)(CI)F' 'Administrators:(OI)(CI)F' 'Users:(OI)(CI)RX' | Out-Null
$log = Join-Path $data 'log.txt'
if (-not (Test-Path $log)) { New-Item -ItemType File -Path $log | Out-Null }
icacls $log /grant 'Users:M' | Out-Null

# COM registration + credential provider registration
New-Item -Force -Path "HKLM:\SOFTWARE\Classes\CLSID\$clsid" -Value 'FaceGate' | Out-Null
New-Item -Force -Path "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32" -Value $dll | Out-Null
Set-ItemProperty -Path "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32" -Name ThreadingModel -Value 'Apartment'
New-Item -Force -Path "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$clsid" -Value 'FaceGate' | Out-Null

# settings: registered but OFF until you choose a mode
if (-not (Test-Path 'HKLM:\SOFTWARE\FaceGate')) { New-Item -Path 'HKLM:\SOFTWARE\FaceGate' | Out-Null }
if ($null -eq (Get-ItemProperty 'HKLM:\SOFTWARE\FaceGate' -Name Enabled -ErrorAction SilentlyContinue)) {
    Set-ItemProperty 'HKLM:\SOFTWARE\FaceGate' -Name Enabled -Value 0 -Type DWord
}

Write-Host ""
Write-Host "Installed. FaceGate is registered but DISABLED."
Write-Host "Next (same admin terminal):"
Write-Host "  & '$dest\fgsetup.exe' enroll $env:USERNAME"
Write-Host "  & '$dest\fgsetup.exe' password"
Write-Host "  & '$dest\fgsetup.exe' mode test"
Write-Host "Then in a NORMAL terminal:  & '$dest\fgcredtest.exe'"
