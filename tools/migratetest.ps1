# Tests fgsetup's FaceGate -> WinFace migration with the real code (fgsetup_migratetest.exe), on scratch folders and a
# scratch HKCU key - never on real data. Run from the repo root after building:  powershell -File tools\migratetest.ps1
$ErrorActionPreference = 'Stop'
$exe = Resolve-Path "$PSScriptRoot\..\build\Release\fgsetup_migratetest.exe"
$root = Join-Path $env:TEMP ("wf-migrate-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
$reg = 'HKCU:\Software\WinFaceMigrateTest'
$fails = 0
function Check($cond, $msg) { if ($cond) { "  [PASS] $msg" } else { "  [FAIL] $msg"; $script:fails++ } }
function Hash($p) { if (Test-Path $p) { (Get-FileHash $p).Hash } else { 'missing' } }
function Bytes($p, $n) { $b = New-Object byte[] $n; (New-Object Random).NextBytes($b); [IO.File]::WriteAllBytes($p, $b) }

function Undeny {   # the test's deny rule also blocks Set-Acl, so reopen the key with ChangePermissions to drop it
    $k = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\WinFaceMigrateTest\WinFace',
        [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,
        [Security.AccessControl.RegistryRights]::ChangePermissions -bor [Security.AccessControl.RegistryRights]::ReadPermissions)
    if (-not $k) { return }
    $a = $k.GetAccessControl()
    foreach ($r in @($a.GetAccessRules($true, $false, [Security.Principal.NTAccount]))) { if ($r.AccessControlType -eq 'Deny') { [void]$a.RemoveAccessRule($r) } }
    $k.SetAccessControl($a); $k.Close()
}

function Setup {
    Remove-Item -Recurse -Force $root, $reg -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force "$root\pd\FaceGate", "$root\pd\WinFace", "$root\la\FaceGate" | Out-Null
    Bytes "$root\pd\FaceGate\profiles.bin" 153617    # like your real files (sizes only - random contents)
    Bytes "$root\pd\FaceGate\secret.tpm" 320
    Set-Content "$root\pd\FaceGate\log.txt" 'old log line'
    Bytes "$root\la\FaceGate\secret.dpapi" 272
    New-Item -Force "$reg\FaceGate" | Out-Null
    New-ItemProperty "$reg\FaceGate" Enabled -Value 1 -PropertyType DWord | Out-Null
    New-ItemProperty "$reg\FaceGate" UserSid -Value 'S-1-5-21-test' -PropertyType String | Out-Null
    New-ItemProperty "$reg\FaceGate" Scenarios -Value 2 -PropertyType DWord | Out-Null
    New-ItemProperty "$reg\FaceGate" TestMode -Value 0 -PropertyType DWord | Out-Null
    New-Item -Force "$reg\WinFace" | Out-Null                     # what the installer creates first
    New-ItemProperty "$reg\WinFace" Enabled -Value 0 -PropertyType DWord | Out-Null
}
function Migrate {
    $env:ProgramData = "$root\pd"; $env:LOCALAPPDATA = "$root\la"
    $out = & $exe migrate --json
    $env:ProgramData = [Environment]::GetEnvironmentVariable('ProgramData', 'Machine')
    $env:LOCALAPPDATA = [Environment]::GetFolderPath('LocalApplicationData')
    return ($out | Select-Object -Last 1 | ConvertFrom-Json)
}

try {
    "1. your situation: FaceGate data + settings, installer's default WinFace Enabled=0"
    Setup
    $h = @{ p = Hash "$root\pd\FaceGate\profiles.bin"; s = Hash "$root\pd\FaceGate\secret.tpm"; l = Hash "$root\pd\FaceGate\log.txt"; d = Hash "$root\la\FaceGate\secret.dpapi" }
    $r = Migrate
    Check ($r.ok -and $r.files -eq 4) "4 files moved ($($r.files)), $($r.settings) settings"
    Check ((Hash "$root\pd\WinFace\profiles.bin") -eq $h.p) "face profiles identical after the move"
    Check ((Hash "$root\pd\WinFace\secret.tpm") -eq $h.s) "encrypted password identical after the move"
    Check ((Hash "$root\pd\WinFace\log.txt") -eq $h.l) "log identical after the move"
    Check ((Hash "$root\la\WinFace\secret.dpapi") -eq $h.d) "test-prompt password copy identical after the move"
    Check (-not (Test-Path "$root\pd\FaceGate") -and -not (Test-Path "$root\la\FaceGate")) "old folders removed once empty"
    $w = Get-ItemProperty "$reg\WinFace"
    Check ($w.Enabled -eq 1) "face unlock stays ON (old setting wins over the installer default)"
    Check ($w.UserSid -eq 'S-1-5-21-test' -and $w.Scenarios -eq 2 -and $w.TestMode -eq 0) "account link and lock-screen mode copied"
    Check (-not (Test-Path "$reg\FaceGate")) "old settings key removed after a complete copy"

    "2. running it again changes nothing"
    $r = Migrate
    Check ($r.files -eq 0 -and $r.settings -eq 0) "nothing moved the second time"
    Check ((Hash "$root\pd\WinFace\profiles.bin") -eq $h.p) "profiles still identical"

    "3. WinFace already has a face file: never overwritten, old one kept"
    Setup
    Set-Content "$root\pd\WinFace\profiles.bin" 'newer profiles'
    $old = Hash "$root\pd\FaceGate\profiles.bin"; $new = Hash "$root\pd\WinFace\profiles.bin"
    $r = Migrate
    Check ((Hash "$root\pd\WinFace\profiles.bin") -eq $new) "existing WinFace profiles not overwritten"
    Check ((Hash "$root\pd\FaceGate\profiles.bin") -eq $old) "old FaceGate profiles left untouched (not deleted)"
    Check ((Hash "$root\pd\WinFace\secret.tpm") -ne 'missing') "the other files still moved"

    "4. settings cannot be written: old settings are kept"
    Setup
    $acl = Get-Acl "$reg\WinFace"
    $deny = New-Object Security.AccessControl.RegistryAccessRule ([Security.Principal.WindowsIdentity]::GetCurrent().Name), 'SetValue', 'Deny'
    $acl.AddAccessRule($deny); Set-Acl "$reg\WinFace" $acl
    $r = Migrate
    Check (Test-Path "$reg\FaceGate") "old FaceGate settings kept when the copy fails"
    Check ((Get-ItemProperty "$reg\FaceGate").UserSid -eq 'S-1-5-21-test') "old settings intact"
    Undeny

    "5. fresh PC (no FaceGate data at all)"
    Remove-Item -Recurse -Force $root, $reg -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force "$root\pd\WinFace", "$root\la" | Out-Null
    $r = Migrate
    Check ($r.ok -and $r.files -eq 0) "nothing to do, no error"
} finally {
    Undeny
    Remove-Item -Recurse -Force $root, $reg -ErrorAction SilentlyContinue
}
if ($fails) { "`n$fails check(s) FAILED"; exit 1 } else { "`nAll migration checks passed." }
