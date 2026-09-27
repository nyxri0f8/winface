# WinFace uninstaller - run from an ADMINISTRATOR PowerShell.
#Requires -RunAsAdministrator
param([switch]$KeepData)
$clsid = '{C188DC15-E41E-4CCF-9DA9-8238E1D0BBDF}'
$dest  = Join-Path $env:ProgramFiles 'WinFace'
# installed with WinFace-Setup.exe? then use its uninstaller (it also removes the Start menu entry and Apps listing)
$unins = Join-Path $dest 'unins000.exe'
if (Test-Path $unins) { Start-Process $unins -Wait; exit }
$data  = Join-Path $env:ProgramData 'WinFace'

# 1. unregister first: from this moment LogonUI no longer loads WinFace
Remove-Item -Force -Recurse "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$clsid" -ErrorAction SilentlyContinue
Remove-Item -Force -Recurse "HKLM:\SOFTWARE\Classes\CLSID\$clsid" -ErrorAction SilentlyContinue
Remove-Item -Force -Recurse 'HKLM:\SOFTWARE\WinFace' -ErrorAction SilentlyContinue
Write-Host "Credential provider unregistered."

# 2. files (the DLL may still be loaded by LogonUI until the next lock/reboot)
foreach ($d in $dest, (Join-Path $env:ProgramFiles 'FaceGate')) {   # current + pre-1.0 folder (old name)
    if (Test-Path $d) {
        Remove-Item -Recurse -Force $d -ErrorAction SilentlyContinue
        if (Test-Path $d) { Write-Host "Some files are in use; they will be removable after a reboot: $d" }
    }
}
if (-not $KeepData -and (Test-Path $data)) {
    Remove-Item -Recurse -Force $data -ErrorAction SilentlyContinue
    Write-Host "Face profiles and encrypted password removed."
}
$dp = Join-Path $env:LOCALAPPDATA 'WinFace'
if (Test-Path $dp) { Remove-Item -Recurse -Force $dp -ErrorAction SilentlyContinue }
Write-Host "Done."
