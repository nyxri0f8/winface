; WinFace installer (Inno Setup 6). Build with installer\build.ps1, which compiles everything first.
; Installs the face unlock credential provider + the WinFace app, downloads the InsightFace recognition model after
; the user accepts its non-commercial licence, and registers everything. Faces/password live in
; C:\ProgramData\WinFace and survive updates.

#define AppName "WinFace"
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define Root ".."
#define Build Root + "\build\Release"
#define AppBin Root + "\app\WinFace\bin\publish"
#define Clsid "{{C188DC15-E41E-4CCF-9DA9-8238E1D0BBDF}"
#define ClsidPlain "{C188DC15-E41E-4CCF-9DA9-8238E1D0BBDF}"
; official InsightFace release (non-commercial licence) - pinned by SHA-256
#define ModelUrl "https://github.com/deepinsight/insightface/releases/download/v0.7/buffalo_l.zip"
#define ModelZipSha "80ffe37d8a5940d59a7384c201a2a38d4741f2f3c51eef46ebb28218a7b0ca2f"
#define ModelSha "4c06341c33c2ca1f86781dab0e829f88ad5b64be9fba56e56bc9ebdefc619e43"
#define DotnetUrl "https://aka.ms/dotnet/8.0/windowsdesktop-runtime-win-x64.exe"

[Setup]
AppId={{6F0D5A3E-2B7C-4E9A-9C41-57E1B2A8D0F4}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=WinFace contributors
AppPublisherURL=https://github.com/nyxri0f8/winface
AppSupportURL=https://github.com/nyxri0f8/winface/issues
DefaultDirName={autopf}\WinFace
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
OutputDir={#Root}\dist
OutputBaseFilename=WinFace-Setup-{#AppVersion}
SetupIconFile={#Root}\app\WinFace\winface.ico
UninstallDisplayIcon={app}\WinFace.exe
UninstallDisplayName={#AppName}
WizardStyle=modern
; the privacy policy + warning, with "I agree" required to continue
LicenseFile={#Root}\PRIVACY.md
Compression=lzma2/ultra64
SolidCompression=yes
; LogonUI may hold our DLL: never let the restart manager try to close it
CloseApplications=no
RestartApplications=no
VersionInfoVersion={#AppVersion}
VersionInfoDescription=WinFace setup

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
; lock-screen component + tools
Source: "{#Build}\WinFaceCP.dll";  DestDir: "{app}"; Flags: ignoreversion
Source: "{#Build}\fgsetup.exe";     DestDir: "{app}"; Flags: ignoreversion
Source: "{#Build}\fgcredtest.exe";  DestDir: "{app}"; Flags: ignoreversion
Source: "{#Build}\fgsound.exe";     DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\third_party\onnxruntime-win-x64-1.24.4\lib\onnxruntime.dll"; DestDir: "{app}"; Flags: ignoreversion
; redistributable models (Apache 2.0) - the InsightFace one is downloaded, see [Code]
Source: "{#Root}\models\runtime\face_detector.onnx";           DestDir: "{app}\models"; Flags: ignoreversion
Source: "{#Root}\models\runtime\face_landmarks_detector.onnx"; DestDir: "{app}\models"; Flags: ignoreversion
Source: "{#Root}\models\runtime\fas_v1se_s4.0.onnx";           DestDir: "{app}\models"; Flags: ignoreversion
Source: "{#Root}\models\runtime\fas_v2_s2.7.onnx";             DestDir: "{app}\models"; Flags: ignoreversion
Source: "{#Root}\models\runtime\canonical_face.bin";           DestDir: "{app}\models"; Flags: ignoreversion
Source: "{tmp}\w600k_r50.onnx"; DestDir: "{app}\models"; Flags: external ignoreversion; Check: ModelWasDownloaded
Source: "{#Root}\assets\sfx_unlock.wav"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "{#Root}\assets\tile.bmp";       DestDir: "{app}\assets"; Flags: ignoreversion
Source: "{#Root}\install\RECOVERY.md";   DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\PRIVACY.md";           DestDir: "{app}"; Flags: ignoreversion
; the app
Source: "{#AppBin}\WinFace.exe";                DestDir: "{app}"; Flags: ignoreversion
Source: "{#AppBin}\WinFace.dll";                DestDir: "{app}"; Flags: ignoreversion
Source: "{#AppBin}\WinFace.runtimeconfig.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AppBin}\WinFace.deps.json";          DestDir: "{app}"; Flags: ignoreversion

[Dirs]
Name: "{commonappdata}\WinFace"; Flags: uninsneveruninstall

[Registry]
; COM server + credential provider (removed first on uninstall, so LogonUI stops loading WinFace before files go)
Root: HKLM64; Subkey: "SOFTWARE\Classes\CLSID\{#Clsid}"; ValueType: string; ValueName: ""; ValueData: "WinFace"; Flags: uninsdeletekey
Root: HKLM64; Subkey: "SOFTWARE\Classes\CLSID\{#Clsid}\InprocServer32"; ValueType: string; ValueName: ""; ValueData: "{app}\WinFaceCP.dll"
Root: HKLM64; Subkey: "SOFTWARE\Classes\CLSID\{#Clsid}\InprocServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"
Root: HKLM64; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{#Clsid}"; ValueType: string; ValueName: ""; ValueData: "WinFace"; Flags: uninsdeletekey
; settings: a fresh install starts OFF (the app switches it on after setup); updates keep everything
Root: HKLM64; Subkey: "SOFTWARE\WinFace"; ValueType: dword; ValueName: "Enabled"; ValueData: "0"; Flags: createvalueifdoesntexist uninsdeletekey

[Icons]
Name: "{autoprograms}\WinFace"; Filename: "{app}\WinFace.exe"; Comment: "Face unlock for Windows"
Name: "{autodesktop}\WinFace";  Filename: "{app}\WinFace.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\WinFace.exe"; Description: "Open WinFace to add your face"; Flags: postinstall nowait skipifsilent shellexec

[UninstallDelete]
Type: files; Name: "{app}\WinFaceCP.old.*.dll"
Type: files; Name: "{app}\onnxruntime.old.*.dll"
Type: dirifempty; Name: "{app}\models"
Type: dirifempty; Name: "{app}\assets"
Type: dirifempty; Name: "{app}"

[Messages]
WizardLicense=Privacy policy and warning
LicenseLabel=Please read the privacy policy and warning before continuing.
LicenseLabel3=WinFace keeps everything on this PC. Please also read the security warning at the end. You must agree to continue.
LicenseAccepted=I have read and &agree to the privacy policy and warning
LicenseNotAccepted=I do &not agree
FinishedLabel=WinFace is installed.%n%nOpen WinFace to add your face, save your Windows password and switch face unlock on for the lock screen. Your PIN and password always keep working.

[Code]
var
  SacPage: TWizardPage;
  SacStatus: TNewStaticText;
  TermsPage: TWizardPage;
  TermsAccept: TNewCheckBox;
  DownloadPage: TDownloadWizardPage;
  ModelReady, ModelDownloaded, NeedDotnet: Boolean;

function ModelWasDownloaded: Boolean;
begin
  Result := ModelDownloaded;
end;

{ ---------- Smart App Control ---------- }

function SacState: Integer;   { 0 off, 1 on, 2 evaluation, -1 not present }
var
  V: Cardinal;
begin
  if RegQueryDWordValue(HKLM64, 'SYSTEM\CurrentControlSet\Control\CI\Policy', 'VerifiedAndReputablePolicyState', V) then
    Result := V
  else
    Result := -1;
end;

procedure ShowSac;
begin
  case SacState of
    1: SacStatus.Caption := 'Smart App Control is ON. It blocks the face unlock component, which is not signed by a commercial certificate.' + #13#10#13#10 +
         'Open Windows Security > App & browser control > Smart App Control settings, choose Off, then click "Check again".' + #13#10#13#10 +
         'Note: Windows only lets you switch Smart App Control back on by resetting or reinstalling Windows.';
    2: SacStatus.Caption := 'Smart App Control is in Evaluation mode. Windows may switch it on later, which would block face unlock.' + #13#10#13#10 +
         'You can continue; if face unlock stops working later, turn Smart App Control off in Windows Security.';
  else
    SacStatus.Caption := 'Smart App Control is off. Nothing to do.';
  end;
end;

procedure OpenSecurity(Sender: TObject);
var
  Code: Integer;
begin
  ShellExec('open', 'windowsdefender://appbrowser/', '', '', SW_SHOWNORMAL, ewNoWait, Code);
end;

procedure CheckAgain(Sender: TObject);
begin
  ShowSac;
end;

{ ---------- InsightFace licence ---------- }

procedure TermsClicked(Sender: TObject);
begin
  WizardForm.NextButton.Enabled := TermsAccept.Checked;
end;

function ModelPath: String;
begin
  Result := AddBackslash(WizardDirValue) + 'models\w600k_r50.onnx';
end;

{ ---------- .NET 8 Desktop Runtime ---------- }

function HasDotnetDesktop8: Boolean;
var
  F: TFindRec;
begin
  Result := FindFirst(ExpandConstant('{commonpf64}\dotnet\shared\Microsoft.WindowsDesktop.App\8.*'), F);
  if Result then FindClose(F);
end;

{ ---------- wizard ---------- }

function OnDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  Result := True;
end;

procedure InitializeWizard;
var
  B1, B2: TNewButton;
  Memo: TNewMemo;
begin
  SacPage := CreateCustomPage(wpWelcome, 'Smart App Control', 'Windows security feature that can block face unlock');
  SacStatus := TNewStaticText.Create(SacPage);
  SacStatus.Parent := SacPage.Surface;
  SacStatus.AutoSize := False;
  SacStatus.WordWrap := True;
  SacStatus.Width := SacPage.SurfaceWidth;
  SacStatus.Height := ScaleY(150);
  B1 := TNewButton.Create(SacPage);
  B1.Parent := SacPage.Surface;
  B1.Caption := 'Open Windows Security';
  B1.Width := ScaleX(170);
  B1.Height := ScaleY(26);
  B1.Top := SacStatus.Top + SacStatus.Height + ScaleY(8);
  B1.OnClick := @OpenSecurity;
  B2 := TNewButton.Create(SacPage);
  B2.Parent := SacPage.Surface;
  B2.Caption := 'Check again';
  B2.Width := ScaleX(110);
  B2.Height := ScaleY(26);
  B2.Top := B1.Top;
  B2.Left := B1.Left + B1.Width + ScaleX(8);
  B2.OnClick := @CheckAgain;

  TermsPage := CreateCustomPage(SacPage.ID, 'Face recognition model',
    'WinFace downloads the InsightFace recognition model from its official source');
  Memo := TNewMemo.Create(TermsPage);
  Memo.Parent := TermsPage.Surface;
  Memo.Width := TermsPage.SurfaceWidth;
  Memo.Height := ScaleY(170);
  Memo.ReadOnly := True;
  Memo.ScrollBars := ssVertical;
  Memo.Text :=
    'WinFace recognises faces with the "buffalo_l" (w600k_r50) model by InsightFace.' + #13#10#13#10 +
    'InsightFace provides its pretrained models for NON-COMMERCIAL RESEARCH PURPOSES ONLY. ' +
    'Because of this licence the model is not included in WinFace; setup downloads it (about 290 MB) directly from ' +
    'InsightFace''s official GitHub release and checks its SHA-256 fingerprint:' + #13#10#13#10 +
    '{#ModelUrl}' + #13#10#13#10 +
    'Only the recognition model (w600k_r50.onnx) is kept. By continuing you agree to use it under InsightFace''s ' +
    'terms: https://github.com/deepinsight/insightface#license' + #13#10#13#10 +
    'Everything else in WinFace is Apache 2.0 (MediaPipe, MiniFASNet) or MIT (ONNX Runtime).';
  TermsAccept := TNewCheckBox.Create(TermsPage);
  TermsAccept.Parent := TermsPage.Surface;
  TermsAccept.Top := Memo.Top + Memo.Height + ScaleY(10);
  TermsAccept.Width := TermsPage.SurfaceWidth;
  TermsAccept.Caption := 'I accept the InsightFace model licence (non-commercial use)';
  TermsAccept.OnClick := @TermsClicked;

  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), @OnDownloadProgress);
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if PageID = SacPage.ID then Result := SacState <= 0;
  if PageID = TermsPage.ID then
    { an update that already has the verified model does not download it again }
    Result := FileExists(ModelPath) and (GetSHA256OfFile(ModelPath) = '{#ModelSha}');
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = SacPage.ID then ShowSac;
  if CurPageID = TermsPage.ID then WizardForm.NextButton.Enabled := TermsAccept.Checked;
end;

function Extract(const Zip, Entry, DestDir: String): Boolean;
var
  Code: Integer;
begin
  ForceDirectories(DestDir);
  Result := Exec(ExpandConstant('{sys}\tar.exe'), '-xf "' + Zip + '" -C "' + DestDir + '" ' + Entry, '', SW_HIDE,
                 ewWaitUntilTerminated, Code) and (Code = 0);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Code: Integer;
begin
  Result := True;
  if (CurPageID = SacPage.ID) and (SacState = 1) then
  begin
    ShowSac;
    MsgBox('Smart App Control is still on. Turn it off in Windows Security first, then click "Check again".', mbError, MB_OK);
    Result := False;
  end
  else if CurPageID = wpReady then
  begin
    ModelReady := FileExists(ModelPath) and (GetSHA256OfFile(ModelPath) = '{#ModelSha}');
    NeedDotnet := not HasDotnetDesktop8;
    if ModelReady and not NeedDotnet then Exit;
    DownloadPage.Clear;
    if not ModelReady then DownloadPage.Add('{#ModelUrl}', 'buffalo_l.zip', '{#ModelZipSha}');
    if NeedDotnet then DownloadPage.Add('{#DotnetUrl}', 'windowsdesktop-runtime.exe', '');
    DownloadPage.Show;
    try
      try
        DownloadPage.Download;
        if not ModelReady then
        begin
          DownloadPage.SetText('Unpacking the recognition model...', '');
          if not Extract(ExpandConstant('{tmp}\buffalo_l.zip'), 'w600k_r50.onnx', ExpandConstant('{tmp}')) or
             (GetSHA256OfFile(ExpandConstant('{tmp}\w600k_r50.onnx')) <> '{#ModelSha}') then
            RaiseException('The downloaded model could not be unpacked or failed its SHA-256 check.');
          DeleteFile(ExpandConstant('{tmp}\buffalo_l.zip'));
          ModelDownloaded := True;
        end;
        if NeedDotnet then
        begin
          DownloadPage.SetText('Installing the .NET 8 Desktop Runtime (Microsoft)...', '');
          if not Exec(ExpandConstant('{tmp}\windowsdesktop-runtime.exe'), '/install /quiet /norestart', '', SW_HIDE,
                      ewWaitUntilTerminated, Code) or ((Code <> 0) and (Code <> 3010) and (Code <> 1638)) then
            RaiseException('The .NET 8 Desktop Runtime could not be installed (code ' + IntToStr(Code) + ').');
        end;
      except
        if DownloadPage.AbortedByUser then
          Log('Download aborted by user.')
        else
          SuppressibleMsgBox(AddPeriod(GetExceptionMessage), mbCriticalError, MB_OK, IDOK);
        Result := False;
      end;
    finally
      DownloadPage.Hide;
    end;
  end;
end;

{ LogonUI may have the old DLLs loaded: move them aside instead of failing (same as install.ps1) }
procedure MoveAsideIfLocked(const Name: String);
var
  P, Stem: String;
begin
  P := ExpandConstant('{app}\') + Name;
  if FileExists(P) and not DeleteFile(P) then
  begin
    Stem := ChangeFileExt(Name, '');
    RenameFile(P, ExpandConstant('{app}\') + Stem + '.old.' + GetDateTimeString('yyyymmddhhnnsszzz', #0, #0) + '.dll');
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  F: TFindRec;
begin
  Result := '';
  if FindFirst(ExpandConstant('{app}\*.old.*.dll'), F) then
  begin
    repeat
      DeleteFile(ExpandConstant('{app}\') + F.Name);
    until not FindNext(F);
    FindClose(F);
  end;
  MoveAsideIfLocked('WinFaceCP.dll');
  MoveAsideIfLocked('onnxruntime.dll');
end;

procedure Icacls(const Params: String);
var
  Code: Integer;
begin
  if not Exec(ExpandConstant('{sys}\icacls.exe'), Params, '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
    Log('icacls ' + Params + ' failed: ' + IntToStr(Code));
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Old, Data, LogFile: String;
  Code: Integer;
begin
  if CurStep = ssPostInstall then
  begin
    { before v1.0 the product was called FaceGate: bring its faces, password and settings over }
    if not Exec(ExpandConstant('{app}\fgsetup.exe'), 'migrate', '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
      Log('migrate failed: ' + IntToStr(Code));
    { data folder: SYSTEM + Administrators full control; Users read (the test prompt runs as the user) + append the log }
    Data := ExpandConstant('{commonappdata}\WinFace');
    LogFile := Data + '\log.txt';
    if not FileExists(LogFile) then SaveStringToFile(LogFile, '', False);
    Icacls('"' + Data + '" /inheritance:r /grant:r *S-1-5-18:(OI)(CI)F *S-1-5-32-544:(OI)(CI)F *S-1-5-32-545:(OI)(CI)RX');
    Icacls('"' + LogFile + '" /grant *S-1-5-32-545:M');
    { a developer install from install.ps1 lived in Program Files\WinFace: the registration now points here }
    Old := ExpandConstant('{commonpf64}\FaceGate');   { the product's name before v1.0 }
    if DirExists(Old) and (CompareText(Old, ExpandConstant('{app}')) <> 0) then
      if not DelTree(Old, True, True, True) then
        Log('Old install folder still in use, remove it after a restart: ' + Old);
  end;
end;

var
  EraseData: Boolean;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Code: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    { ask first, while fgsetup.exe still exists: its erase also removes the password's TPM keys }
    EraseData := SuppressibleMsgBox('Also erase your enrolled faces, the stored (encrypted) password with its TPM keys, the log and all settings?' + #13#10#13#10 +
                                    'Choose No to keep them for a later reinstall.', mbConfirmation, MB_YESNO, IDNO) = IDYES;
    if EraseData then
      Exec(ExpandConstant('{app}\fgsetup.exe'), 'erase', '', SW_HIDE, ewWaitUntilTerminated, Code);
  end;
  if (CurUninstallStep = usPostUninstall) and EraseData then
  begin
    DelTree(ExpandConstant('{commonappdata}\WinFace'), True, True, True);
    DelTree(ExpandConstant('{localappdata}\WinFace'), True, True, True);
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\WinFace');
  end;
end;
