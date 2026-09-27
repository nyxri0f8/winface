# FaceGate emergency recovery

Your PIN and password tiles are **never** hidden by FaceGate. If face unlock misbehaves, click
**Sign-in options** on the lock screen and use your PIN or password.

## Level 1 - turn FaceGate off (you can still sign in)
Admin terminal:
```
& "C:\Program Files\FaceGate\fgsetup.exe" mode off
```
or uninstall: `powershell -ExecutionPolicy Bypass -File <repo>\install\uninstall.ps1`

## Level 2 - you cannot sign in at all (lock screen broken)
1. On the sign-in screen hold **Shift** and click **Power > Restart**.
2. **Troubleshoot > Advanced options > Command Prompt**.
3. BitLocker is ON: when asked, enter your **BitLocker recovery key**
   (from https://account.microsoft.com/devices/recoverykey - keep it on your phone/paper).
   If not asked but C: is locked: `manage-bde -unlock C: -RecoveryPassword YOUR-48-DIGIT-KEY`
4. Find the Windows drive (in recovery it is often **D:**): `dir C:\Windows` / `dir D:\Windows`
5. Remove FaceGate from the sign-in system (replace C: with the drive you found):
```
reg load HKLM\OFFSOFT C:\Windows\System32\config\SOFTWARE
reg delete "HKLM\OFFSOFT\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{C188DC15-E41E-4CCF-9DA9-8238E1D0BBDF}" /f
reg unload HKLM\OFFSOFT
```
6. `exit` > **Continue** to boot Windows. The normal sign-in screen is back.
