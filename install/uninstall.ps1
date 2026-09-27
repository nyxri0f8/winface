# FaceGate uninstaller - run from an ADMINISTRATOR PowerShell.
#Requires -RunAsAdministrator
param([switch]$KeepData)
$clsid = '{C188DC15-E41E-4CCF-9DA9-8238E1D0BBDF}'
$dest  = Join-Path $env:ProgramFiles 'FaceGate'
$data  = Join-Path $env:ProgramData 'FaceGate'

# 1. unregister first: from this moment LogonUI no longer loads FaceGate
Remove-Item -Force -Recurse "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$clsid" -ErrorAction SilentlyContinue
Remove-Item -Force -Recurse "HKLM:\SOFTWARE\Classes\CLSID\$clsid" -ErrorAction SilentlyContinue
Remove-Item -Force -Recurse 'HKLM:\SOFTWARE\FaceGate' -ErrorAction SilentlyContinue
Write-Host "Credential provider unregistered."

# 2. files (the DLL may still be loaded by LogonUI until the next lock/reboot)
if (Test-Path $dest) {
    Remove-Item -Recurse -Force $dest -ErrorAction SilentlyContinue
    if (Test-Path $dest) { Write-Host "Some files are in use; they will be removable after a reboot: $dest" }
}
if (-not $KeepData -and (Test-Path $data)) {
    Remove-Item -Recurse -Force $data -ErrorAction SilentlyContinue
    Write-Host "Face profiles and encrypted password removed."
}
$dp = Join-Path $env:LOCALAPPDATA 'FaceGate'
if (Test-Path $dp) { Remove-Item -Recurse -Force $dp -ErrorAction SilentlyContinue }
Write-Host "Done."
