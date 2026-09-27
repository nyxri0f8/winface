# Builds WinFace-Setup-<version>.exe into dist\ : C++ components, the WinFace app, then the Inno Setup installer.
#   powershell -ExecutionPolicy Bypass -File installer\build.ps1 [-Version 1.0.0] [-SkipTests]
# Needs: Visual Studio 2022 C++ tools, CMake, .NET 8 SDK, Inno Setup 6 (winget install JRSoftware.InnoSetup).
param([string]$Version = '1.0.0', [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Step($msg) { Write-Host "`n== $msg" -ForegroundColor Cyan }
function Find-Tool($name, [string[]]$candidates) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($c in $candidates) { if ($c -and (Test-Path $c)) { return $c } }
    throw "$name not found"
}

$cmake = Find-Tool 'cmake' @("$env:ProgramFiles\CMake\bin\cmake.exe")
$iscc  = Find-Tool 'ISCC' @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe")

Step "C++ components (WinFaceCP.dll, fgsetup, fgsound, ...)"
& $cmake -S . -B build | Out-Null
& $cmake --build build --config Release
if ($LASTEXITCODE) { throw 'C++ build failed' }

Step "WinFace app"
dotnet publish app\WinFace\WinFace.csproj -c Release -o app\WinFace\bin\publish -p:Version=$Version --nologo
if ($LASTEXITCODE) { throw 'app build failed' }

if (-not $SkipTests) {
    Step "Checks"
    & .\build\Release\fgcli.exe selftest | Select-Object -Last 1
    if ($LASTEXITCODE) { throw 'engine self-test failed' }
    & .\build\Release\fgoverlaytest.exe dll (Resolve-Path .\build\Release\WinFaceCP.dll) | Select-Object -Last 1
    if ($LASTEXITCODE) { throw 'provider DLL check failed' }
    & .\build\Release\fgoverlaytest.exe wav .\assets\sfx_unlock.wav | Select-Object -Last 1
    if ($LASTEXITCODE) { throw 'sound check failed' }
    & powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\migratetest.ps1 | Select-Object -Last 1
    if ($LASTEXITCODE) { throw 'FaceGate -> WinFace migration test failed' }
}

Step "Installer"
& $iscc /Q "/DAppVersion=$Version" installer\winface.iss
if ($LASTEXITCODE) { throw 'Inno Setup failed' }
$out = Get-Item "dist\WinFace-Setup-$Version.exe"
Write-Host ("`nBuilt {0} ({1:N1} MB)" -f $out.FullName, ($out.Length / 1MB)) -ForegroundColor Green
Write-Host ("SHA-256 {0}" -f (Get-FileHash $out).Hash)
