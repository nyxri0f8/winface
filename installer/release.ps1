# Publishes a WinFace update: builds the setup, then creates the GitHub release with the setup, its SHA-256 file and the
# notes from UPDATES.md. Every installed WinFace then offers "Update now" (at sign-in / daily / About > Check for updates).
#   powershell -ExecutionPolicy Bypass -File installer\release.ps1 -Version 1.2.0
# Before running: add a "## 1.2.0 - <date>" section to UPDATES.md and commit + push your changes.
param([Parameter(Mandatory)][string]$Version, [switch]$Draft)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# 1. release notes = this version's section of UPDATES.md
$md = Get-Content UPDATES.md -Raw
$m = [regex]::Match($md, "(?ms)^## $([regex]::Escape($Version))\b[^\n]*\n(.*?)(?=^## |\z)")
if (-not $m.Success) { throw "UPDATES.md has no '## $Version' section - add one first" }
$notes = $m.Groups[1].Value.Trim()

# 2. nothing uncommitted, and the tag must be new
if (git status --porcelain) { throw 'commit (and push) your changes first' }
if (git tag -l "v$Version") { throw "v$Version already exists" }

# 3. build + checks
& powershell -NoProfile -ExecutionPolicy Bypass -File installer\build.ps1 -Version $Version
if ($LASTEXITCODE) { throw 'build failed' }
$setup = "dist\WinFace-Setup-$Version.exe"
$sha = (Get-Content "$setup.sha256").Split(' ')[0]

# 4. publish
$body = @"
$notes

## Install
Download **WinFace-Setup-$Version.exe** below and run it (turn Smart App Control off first). Already using WinFace? It offers this update by itself - or open WinFace > About > Check for updates.

SHA-256 of WinFace-Setup-${Version}.exe: ``$sha``
"@
$notesFile = Join-Path $env:TEMP "winface-notes-$Version.md"
Set-Content $notesFile $body -Encoding utf8
git push origin HEAD
$ghArgs = @("release", "create", "v$Version", $setup, "$setup.sha256", "--target", "main", "--title", "WinFace $Version", "--notes-file", $notesFile)
if ($Draft) { $ghArgs += "--draft" }
gh @ghArgs
if ($LASTEXITCODE) { throw 'gh release create failed' }
Remove-Item $notesFile
Write-Host "Released WinFace $Version - installed copies will offer the update." -ForegroundColor Green
