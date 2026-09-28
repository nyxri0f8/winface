# WinFace updates

Every WinFace release, newest first. The section for a version is also its GitHub release notes and what the in-app
**Update now** dialog shows, so write it for users.

To publish an update: add a section below, commit, then run `installer\release.ps1 -Version x.y.z`. Everyone with
WinFace installed gets the update dialog within a day (at sign-in or at noon), or right away via About > Check for updates.

---

## 1.1.0 - 2026-09-28

### New
- Automatic updates: WinFace checks for new versions at sign-in and once a day and shows what changed, with Update now, Later and Skip this version. Your faces, password and settings are kept. Turn it off in About.
- Random extra checks against replays: 2-3 times a day, at unpredictable unlocks, and always on the attempt after 2 failures, you are asked to blink or open your mouth, and the screen briefly flashes two random colours that must reflect off your skin. Normal unlocks stay fast.
- Security page: see whether face unlock is available or paused and why, choose how often the extra checks run, the PIN rules, and intruder photos.
- PIN rules like a phone: your PIN is required after 3 failed attempts, after a restart, and when the PC was not unlocked for 48 hours (24, 48, 72 hours or never).
- Camera pinning: face unlock pauses if a different camera appears in place of yours until you confirm it; HDMI capture dongles are refused like virtual cameras.
- Intruder photos (off by default): an encrypted photo of whoever fails a face check, viewable and deletable in the app, at most 20 kept for 30 days.
- SECURITY.md: how to report security problems privately.

### Fixed
- The setup guide no longer opens every time WinFace starts: a PC that is already set up is recognised, and Finish later is remembered.
- When the privacy policy changes you now see a short agreement window instead of the whole setup guide.

## 1.0.1 - 2026-09-28

### Fixed
- Works with any camera: automatic camera selection (1.0.0 only accepted one specific camera and showed "whitelisted camera not found").
- Infrared (Windows Hello) sensors are skipped; built-in MIPI / Intel IPU cameras are supported; cameras without a 1280x720 mode are scaled.
- Existing FaceGate installs are moved to WinFace automatically (faces, password, settings).

## 1.0.0 - 2026-09-28

### New
- First release: the WinFace app, WinFace-Setup.exe, a guided setup with a 4-test security check, a Face ID-style lock-screen animation, an unlock sound, privacy policy and erase.
