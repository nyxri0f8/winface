WinFace - Privacy Policy and Warning
Version 1.0, effective 28 September 2026

Please read this before installing or using WinFace. You must agree to it to continue.


=====================================================================
PRIVACY POLICY
=====================================================================

1. Everything stays on this PC

WinFace works completely offline on your computer. It has no account, no
cloud service, no servers, no telemetry, no analytics and no advertising.
It never uploads, sells or shares your face data, your password or your
logs - there is nowhere for it to send them.

2. What WinFace stores, and where

- Face templates: C:\ProgramData\WinFace\profiles.bin
  Lists of numbers (512 per capture) computed from your face. They are not
  pictures, and a picture of your face cannot be rebuilt from them. The
  folder can only be read by administrators and by the lock screen.

- Your Windows password, encrypted: C:\ProgramData\WinFace\secret.tpm
  Encrypted with a key that lives inside this PC's TPM security chip and can
  only be used by the lock screen (SYSTEM). The file is useless on any
  other computer. For the optional test prompt, a second copy is encrypted
  for your Windows account only: %LOCALAPPDATA%\WinFace\secret.dpapi

- A log: C:\ProgramData\WinFace\log.txt
  Times, results and reasons (for example "face not recognised"). It never
  contains images, face templates or passwords.

- Settings: registry HKLM\SOFTWARE\WinFace (and app preferences in
  HKCU\Software\WinFace).

3. What WinFace never stores

Camera images and video are processed in memory, a few frames at a time,
and immediately discarded. They are never saved to disk and never shown -
the app only draws landmark dots.

4. The camera

The camera is switched on only while a face check runs: on the lock screen,
while adding a face, or during a test. It is switched off right after.
Only real USB or built-in cameras are used; virtual cameras are refused.

5. Internet use

Only the installer uses the internet, once:
- to download the face recognition model from InsightFace's official
  release on GitHub, and
- to download the .NET 8 Desktop Runtime from Microsoft, if it is missing.
Those sites see a normal download request (such as your IP address) under
their own privacy policies. WinFace itself never connects to the internet.

6. You are in control

- Turn face unlock off at any time: WinFace > Home > Turn face unlock off.
- Erase everything WinFace stored at any time: WinFace > Home > Erase all
  face data. This deletes the face templates, the encrypted password and
  its TPM keys, the log and all settings.
- Uninstall from Settings > Apps > WinFace, and choose to erase your data.

7. Other people

Only add your own face, or the face of someone who has agreed to it.
Do not use WinFace to identify or monitor people.

8. Changes

If this policy changes, the new version is shown in the app and has to be
accepted again. The current version is always in the project repository:
https://github.com/nyxri0f8/winface


=====================================================================
WARNING - PLEASE READ
=====================================================================

- WinFace is NOT as secure as Windows Hello. Windows Hello uses special
  infrared cameras; WinFace uses a normal webcam. Its checks (texture
  analysis, a random head turn and a 3D-motion test) block ordinary photos
  and videos, but NO face recognition on a normal webcam can guarantee it
  will stop every attack. Very realistic masks, identical twins or close
  family members might be accepted.

- Do not rely on WinFace where strong security matters: shared or public
  PCs, work computers with sensitive data, or if someone you do not trust
  looks like you. Face unlock is only an extra sign-in option - your PIN and
  password always keep working, and you should keep them.

- WinFace needs Smart App Control switched off, because it is not signed by
  a commercial certificate. This lowers Windows' protection against
  untrusted apps, and Windows only lets you switch Smart App Control back
  on by resetting or reinstalling Windows.

- Your Windows password is stored on this PC in encrypted form so the lock
  screen can sign you in. Anyone who already has administrator rights on
  this PC could obtain it. If you change your Windows password, update it
  in WinFace (Password page).

- The face recognition model is provided by InsightFace for non-commercial
  research use only. Do not use WinFace commercially.

- WinFace is free, open-source, experimental software, provided "AS IS",
  WITHOUT WARRANTY OF ANY KIND, express or implied (Apache License 2.0,
  sections 7 and 8). You use it at your own risk. If the lock screen ever
  misbehaves, sign in with your PIN or password, or follow RECOVERY.md in
  the WinFace folder.

By continuing you confirm that you have read and agree to this privacy
policy and warning.
