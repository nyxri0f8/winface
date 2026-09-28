# Security Policy

WinFace signs people in to Windows, so security reports are very welcome - including "I fooled it with X".

## Reporting a vulnerability

**Please report privately, not in a public issue.**

1. Go to the repository's **Security** tab and choose **Report a vulnerability** (GitHub private vulnerability reporting).
2. Describe what you did, what happened, and what should have happened. Include the WinFace version (Home > About), Windows version, camera model, and - if relevant - the lines from `C:\ProgramData\WinFace\log.txt` around the attempt (the log never contains passwords or images).
3. For spoofing reports, a short description of the setup is enough (for example "printed A4 photo, matte paper, 40 cm, office light"); videos are welcome but please do not send images of other people without their consent.

If private reporting is not available on the repository, open an issue titled **"Security contact request"** with no details, and a maintainer will reply with a private channel.

What to expect:
- an acknowledgement within **7 days**,
- a first assessment within **14 days**,
- a fix or mitigation for confirmed issues as soon as practical, with credit in the release notes (unless you prefer to stay anonymous).

## Scope

In scope - please try to break these:
- **Presentation attacks**: photos, screens, videos, replays, masks, deepfakes, virtual or capture-card cameras getting a sign-in.
- **The liveness checks**: texture model, random head turn with the 3D-parallax test, random blink / open-mouth action, the screen-flash check (in *Enforce* mode).
- **The lock-screen component** (`WinFaceCP.dll` in `LogonUI.exe`): anything that gets a sign-in without a matching live face, crashes or hangs the lock screen, or blocks the PIN / password.
- **Stored secrets**: reading or using the stored password, the face templates or intruder photos without administrator rights; tampering with the lockout state as a standard user.
- **The PIN rules**: getting a face sign-in when the PIN should be required (after a restart, after 48 hours, after failed attempts, after a camera change).
- **The installer, updater and helper tools** (`fgsetup.exe`, `fgsound.exe`, the sign-in events task): privilege escalation, DLL planting, unsafe file or registry permissions.

Known limitations (reports are still welcome, but these are documented trade-offs, not surprises):
- WinFace uses an ordinary colour webcam. It is **not as strong as Windows Hello** with an infrared camera; realistic 3D masks, identical twins and very similar relatives may be accepted.
- A user who is **already an administrator** on the PC can read or change anything WinFace stores (as with any software on Windows).
- The screen-flash check is new and ships in *Measure only* mode until it has been calibrated on more hardware.
- Builds are not yet code-signed, so Smart App Control has to be off.

Out of scope: attacks that need an administrator account or physical access to the unlocked PC, social engineering, denial of service by covering the camera, and issues in Windows itself (report those to Microsoft).

## Safe harbour

Testing WinFace on **your own devices** and accounts, in good faith, to find and report vulnerabilities is welcome. We will not pursue or support legal action for such research. Please do not test on other people's computers or accounts, and give us a reasonable time to fix an issue before you publish details.

## Supported versions

Only the latest release receives security fixes. Update from the [Releases](https://github.com/nyxri0f8/winface/releases/latest) page.
