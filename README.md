<p align="center">
  <img src="assets/banner.jpg" alt="FaceGate Banner" width="100%">
</p>

# FaceGate (winface)

Biometric Face Unlock Credential Provider for Windows 10 and 11.

FaceGate provides fast, secure facial recognition logon for standard RGB webcams without requiring proprietary infrared (IR) sensors. It integrates natively into Windows `LogonUI.exe` using a custom C++20 Credential Provider, backed by hardware TPM 2.0 key isolation, on-device neural network inference, and multi-layered anti-spoofing based on 3D geometric parallax.

---

> **IMPORTANT WARNING AND SAFETY GUARANTEE**
>
> FaceGate **never** hides, modifies, or disables your standard Windows PIN or password tiles.
> If facial verification fails, times out, or the camera is unavailable, you can always click **Sign-in options** on the lock screen and enter your PIN or password as usual.
>
> A complete, offline emergency recovery procedure is included in this repository and installed alongside the binaries in `RECOVERY.md`.

---

## Table of Contents

- [Architectural Overview](#architectural-overview)
- [Security Architecture & Threat Model](#security-architecture--threat-model)
  - [Hardware TPM 2.0 Cryptographic Vault](#1-hardware-tpm-20-cryptographic-vault)
  - [Zero-IPC In-Process Execution](#2-zero-ipc-in-process-execution)
  - [Single-Use Proof Token](#3-single-use-proof-token)
  - [Memory Hygiene](#4-memory-hygiene)
  - [Mathematical 3D Parallax Anti-Spoofing](#5-mathematical-3d-parallax-anti-spoofing)
  - [Camera Hardware Enforcement](#6-camera-hardware-enforcement)
- [System Architecture Diagram](#system-architecture-diagram)
- [Verification & Anti-Spoofing Pipeline](#verification--anti-spoofing-pipeline)
- [Project Layout](#project-layout)
- [Installation Guide](#installation-guide)
- [Command-Line Reference](#command-line-reference)
- [Configuration Settings](#configuration-settings)
- [Troubleshooting & Logs](#troubleshooting--logs)
- [Emergency Recovery Procedure](#emergency-recovery-procedure)
- [Credits](#credits)

---

## Architectural Overview

Standard Windows Hello Face requires specialized depth-sensing or infrared cameras (850nm/940nm LEDs). Most consumer laptops and external monitors only feature standard 720p or 1080p RGB webcams.

FaceGate bridges this gap by combining modern computer vision with low-level Windows authentication internals:

- **Native Credential Provider V2**: Implements `ICredentialProvider`, `ICredentialProviderCredential2`, and `ICredentialProviderSetUserArray` directly in C++20.
- **In-Process ONNX Inference**: MediaPipe Face Mesh (478 3D landmarks) and ArcFace (512-dimensional embeddings) executed on-device via ONNX Runtime without external runtimes or background daemons.
- **Hardware Cryptography**: User credentials are encrypted using an RSA-2048 key sealed inside the motherboard's Trusted Platform Module (TPM 2.0).
- **Direct Winlogon Overlay**: A 32-bit premultiplied alpha layered window attaches directly to the secure Winlogon desktop, rendering a minimal monochrome Face ID-style glyph at ~60 FPS with zero camera pixels displayed or stored: corner brackets frame a simple face that follows your head, two chevrons point the way during the head turn, the brackets morph into a circle with a check mark on success, and the glyph shakes on rejection.
- **Windows Hello-style Lock Screen Flow**: The camera starts as soon as the lock screen appears, but nothing is drawn over the lock-screen picture (date and time). As soon as the camera recognises an enrolled face, FaceGate lifts the lock screen by itself and the sign-in page appears with the face HUD already asking for the head turn. A key press or click also lifts it, exactly as without FaceGate.
- **Unlock Sound**: A single sound plays on a successful unlock (none on scan start or rejection). It is played by a separate helper, `fgsound.exe`, so it is not cut off when `LogonUI.exe` exits right after sign-in.

---

## Security Architecture & Threat Model

### 1. Hardware TPM 2.0 Cryptographic Vault
The Windows password is required by Local Security Authority Subsystem Service (LSASS) to construct an interactive Kerberos or NTLM logon token. Storing credentials insecurely creates a critical vulnerability. FaceGate hardens this storage using the hardware TPM:
- A unique RSA-2048 key is created in the TPM via `MS_PLATFORM_CRYPTO_PROVIDER`.
- When stored, the key's Security Descriptor (DACL) is restricted strictly to Local System (`NT AUTHORITY\SYSTEM`) using the SDDL string `D:P(A;;GA;;;SY)`.
- **Impact**: Normal programs cannot read or decrypt the stored password, but anything running with admin rights can (an admin can run code as `SYSTEM`). Only `SYSTEM` (such as `LogonUI.exe` on the secure lock screen, or processes elevated to `SYSTEM` by an administrator) can access the private key.
- The password file (`%ProgramData%\FaceGate\secret.tpm`) is bound to the physical machine's TPM and is cryptographically useless if copied to another machine.

### 2. Zero-IPC In-Process Execution
Most open-source Windows unlock alternatives run an external background service or Python script communicating over Named Pipes or Localhost HTTP sockets. This introduces a major Local Privilege Escalation (LPE) vector: any process writing an unlock message to the pipe can trigger authentication.
- FaceGate operates **entirely in-process inside `LogonUI.exe`**.
- There are **no listening network ports, no RPC endpoints, and no named pipes**.
- The worker thread and camera handle are spawned on demand when the lock screen wakes and are closed immediately upon unlock or timeout.

### 3. Single-Use Proof Token
Credential serialization cannot be requested arbitrarily:
- The credential provider requires an internal proof token (`take_verified()`).
- This token is set only when the C++ decision engine reaches `State::Unlock`.
- It expires after 10 seconds and is atomically cleared on first access (`verified_at_.exchange(0)`).
- If `LogonUI` requests credential serialization without a fresh proof token, the request is immediately rejected.

### 4. Memory Hygiene
- All in-memory password representations use the RAII struct `SecurePassword`.
- When `SecurePassword` leaves scope, its internal buffer is cleared using `SecureZeroMemory` before deallocation.
- Plaintext passwords are never written to logs, registry values, or temporary disk files.

### 5. Mathematical 3D Parallax Anti-Spoofing
Standard 2D face recognition is vulnerable to presentation attacks (photos, tablet screens, printed masks). FaceGate employs a multi-tiered defense:
- **Dual-Model Texture Classification**: Uses two MiniFASNet models (`fas_v1se` and `fas_v2`) on cropped facial patches to detect moire patterns, screen refresh artifacts, and paper textures.
- **CSPRNG Challenge**: The engine requests a random head turn (LEFT or RIGHT) generated from `BCryptGenRandom`. An attacker playing a pre-recorded video cannot predict the requested direction.
- **3D Parallax DLT Homography Residual**: When a flat photo or phone screen is rotated in front of a camera, all facial landmarks transform according to a planar projective homography:
  
  $$\mathbf{x}' \sim \mathbf{H}\mathbf{x}$$

  A real human face has physical depth: the nose tip, eye sockets, and jawline exist on distinct geometric planes. Rotating a real head introduces 3D perspective parallax that breaks any 2D planar homography. By fitting a Direct Linear Transformation (DLT) homography to the landmark correspondences and measuring the median residual:
  
  $$\text{Residual} = \text{median}\left(\frac{\|\mathbf{H}\mathbf{x}_i - \mathbf{x}'_i\|}{\text{face\_width}}\right)$$

  If $\text{Residual} < 0.025$, the rotation is geometrically flat and rejected as a photo/screen attack.
- **Identity Continuity During the Turn**: Before the challenge starts, the face must match frontally (3 of the last 5 frames at cosine similarity $\geq 0.42$). During the turn the head pose naturally lowers ArcFace scores, so each frame only has to stay above a floor of $\max(\text{match} - 0.08,\ 0.34)$, with a single motion-blurred frame forgiven. The floor stays above the best stranger in the LFW calibration (0.313), so a different face swapped in mid-turn is still rejected, and the frame that completes the turn must itself pass.
- **Lock-Screen Lift**: Only a recognised enrolled face lifts the lock screen automatically (FaceGate sends a Shift tap and a click in the empty top-left corner from the sign-in desktop). Lifting it grants nothing: it only shows the same sign-in page a key press would.

### 6. Camera Hardware Enforcement
- Software cameras (OBS Virtual Camera, ManyCam, DroidCam, etc.) create virtual device links such as `swd#` or `root#`.
- FaceGate enforces `\\?\usb#` hardware prefix validation and matches the configured vendor/product ID (VID/PID). Any virtual or spoofed camera source is refused.

---

## System Architecture Diagram

```mermaid
flowchart TD
    subgraph LockScreen ["Windows Lock Screen (LogonUI.exe - SYSTEM)"]
        A[LogonUI / CredUI] --> B[FaceGateCP.dll]
        B --> C[FaceProvider / FaceCredential]
        C --> D[Worker Scanner Thread]
        C --> E[Winlogon Desktop Overlay HUD]
    end

    subgraph Engine ["fgengine (In-Process C++20)"]
        D --> F[Media Foundation Capture]
        F --> G[FaceMesh: ONNX 478 Landmarks]
        G --> H[ArcFace INT8 Embedding]
        G --> I[MiniFASNet Texture Anti-Spoof]
        G --> J[Geometry & Homography Engine]
        H & I & J --> K[Decision Engine State Machine]
    end

    subgraph SecurityVault ["Hardware & OS Security"]
        K -- "State::Unlock" --> L["Single-Use Proof Token (10s)"]
        L --> M[Secret Vault]
        M --> N["TPM 2.0 (MS_PLATFORM_CRYPTO_PROVIDER)"]
        N --> O["RSA-2048 Key (SYSTEM-only SDDL)"]
        O --> P[Decrypted Password in Memory]
        P --> Q[KERB_INTERACTIVE_UNLOCK_LOGON / CredPack]
        Q --> R[LSASS Authentication]
    end

    subgraph Display ["Visual & Audio Feedback"]
        E --> S[32-bit Layered GDI+ Window]
        S --> T["Face ID-style Glyph (brackets morph into a check-mark circle)"]
        S --> U["Curtain Watcher (key / click / recognised face lifts the lock screen)"]
        S --> V[Display State Listener]
        E --> W["fgsound.exe (unlock sound, outlives LogonUI)"]
    end
```

---

## Verification & Anti-Spoofing Pipeline

```mermaid
flowchart TD
    Start([Lock Screen Active / Display On]) --> CamInit[Open Whitelisted USB Camera]
    CamInit --> Detect[Detect Face & 478 3D Landmarks]
    
    Detect --> CheckDistance{Face Width in Range?\n170px <= w <= 330px}
    CheckDistance -- No --> Reposition[Display Guidance: Move Closer / Back]
    Reposition --> Detect
    
    CheckDistance -- Yes --> CheckMultiple{Multiple Faces\nDetected?}
    CheckMultiple -- Yes --> AbortCrowd[Display: More than one person in view]
    AbortCrowd --> Detect

    CheckMultiple -- No --> Embed[ArcFace INT8 512D Embedding]
    Embed --> MatchScore{Cosine Similarity >= 0.42\nvs Enrolled Templates}
    MatchScore -- No --> SearchTimeout{Search Timeout?\nDefault 7000ms}
    SearchTimeout -- Yes --> Fail[Authentication Failed]
    SearchTimeout -- No --> Detect

    MatchScore -- Yes --> TextureCheck{Dual MiniFASNet\nTexture Score >= 0.60}
    TextureCheck -- No --> Detect

    TextureCheck -- Yes --> Visible{Sign-in Page\nVisible?}
    Visible -- "No (lock-screen picture up)" --> Lift[Hold the Head Turn\nLift the Lock Screen Automatically]
    Lift --> Challenge
    Visible -- Yes --> Challenge[Generate CSPRNG Direction\nTurn Head Left or Right]
    Challenge --> TrackTurn[Track Yaw Angle & Landmark Trajectories]

    TrackTurn --> TurnCheck{Turn >= 12 deg in\nExpected Direction?}
    TurnCheck -- Wrong Direction --> Fail
    TurnCheck -- In Progress --> TrackTurn

    TurnCheck -- Yes --> ParallaxCheck{3D Parallax DLT\nHomography Residual >= 0.025?}
    ParallaxCheck -- "No (Flat Planar Motion)" --> SpoofDetected[Reject: Flat Media / Screen Detected]
    SpoofDetected --> Fail

    ParallaxCheck -- "Yes (Real 3D Depth)" --> IdentityMaintained{Identity Maintained During Turn?\nEvery frame >= 0.34 floor\n(one blurred frame forgiven)}
    IdentityMaintained -- No --> Fail
    IdentityMaintained -- Yes --> UnlockSuccess[Issue Proof Token & Trigger Auto-Logon]
```

---

## Project Layout

```
facegate/
├── CMakeLists.txt         # Build definition (C++20, static CRT, CFG, SDL, DelayLoad)
├── cp/                    # Credential Provider (loaded by LogonUI.exe)
│   ├── common.h / .cpp    # Configuration and shared logging
│   ├── credential.h /.cpp # ICredentialProviderCredential2 implementation
│   ├── dll.cpp            # COM exports (DllGetClassObject, DllCanUnloadNow)
│   ├── helpers.h / .cpp   # LSA package negotiation and serialization
│   ├── overlay.h / .cpp   # GDI+ layered HUD on Winlogon desktop
│   ├── provider.cpp       # ICredentialProvider and ICredentialProviderSetUserArray
│   ├── scanner.h / .cpp   # Camera/engine worker thread lifecycle
│   └── secret.h / .cpp    # TPM 2.0 RSA-2048 and DPAPI cryptography
├── engine/                # Core biometric and computer vision engine
│   ├── camera.h / .cpp    # Media Foundation hardware webcam capture
│   ├── decide.h / .cpp    # Unlock decision state machine and challenge logic
│   ├── facemesh.h / .cpp  # MediaPipe Face Landmarker on ONNX Runtime
│   ├── geom.h / .cpp      # Bilinear affine warp, Umeyama alignment, DLT homography
│   ├── image.h            # Lightweight contiguous BGR image buffer
│   ├── onnx.h / .cpp      # ONNX Runtime environment and session wrappers
│   ├── profiles.h / .cpp  # Binary template profile serialization
│   └── recog.h / .cpp     # ArcFace embeddings and MiniFASNet anti-spoofing
├── tools/                 # Administrative and testing utilities
│   ├── fgcli.cpp          # Engine self-test against reference test vectors
│   ├── fgcredtest.cpp     # CredUI prompt test harness (non-destructive)
│   ├── fgoverlaytest.cpp  # Renders the real HUD to PNGs, live curtain test, DLL/sound checks (dev only)
│   ├── fgsetup.cpp        # Enrollment, password vaulting, settings CLI
│   └── fgsound.cpp        # Plays the unlock sound in its own process (outlives LogonUI)
├── assets/                # tile.bmp, sfx_unlock.wav (see Credits), banner.jpg
├── install/               # Installation and recovery scripts
│   ├── install.cmd        # Double-click launcher: asks for admin, runs install.ps1
│   ├── install.ps1        # Admin deployment, registration and post-install verification
│   ├── uninstall.ps1      # Safe removal script
│   └── RECOVERY.md        # Offline BitLocker and recovery console procedures
└── bench/                 # Calibration and validation toolset
    ├── calibrate.py       # LFW dataset calibration over 13,000 stranger faces
    └── unlock_test.py     # Live benchmark harness with telemetry logging
```

---

## Installation & Setup Guide (Step-by-Step)

Follow this complete walkthrough to install, compile, configure, and activate FaceGate on your system from top to bottom.

---

### Step 1: System & Hardware Verification

Before beginning, ensure your PC meets the following hardware and operating system requirements:

1. **Operating System**: Windows 10 (version 1903 or newer, 64-bit) or Windows 11 (64-bit).
2. **Processor Architecture**: x86_64 with AVX2 instruction support (Intel Core 4th Gen+ or AMD Zen+).
3. **Webcam**: Standard integrated laptop camera or external USB webcam (720p 30 FPS or 1080p).
4. **Hardware TPM 2.0**:
   - Verify your TPM status by opening an Administrator PowerShell and running:
     ```powershell
     Get-Tpm
     ```
     Ensure `TpmPresent: True` and `TpmReady: True`.
   - Alternatively, press `Win + R`, type `tpm.msc`, and verify that the status reports "The TPM is ready for use" (Specification Version: 2.0).
5. **Windows 11 Smart App Control (SAC)**:
   - Because FaceGate is compiled locally from source code without an expensive commercial EV Authenticode certificate, Windows 11 **Smart App Control (SAC)** in "On" mode will block unsigned `.dll` binaries from loading into `LogonUI.exe`.
   - Ensure Smart App Control is set to **Off** or **Evaluation** mode in:
     `Windows Security` > `App & browser control` > `Smart App Control settings`.
   - If Smart App Control is strictly **On**, it blocks local developer-built binaries from executing. Alternatively, sign the compiled binaries with a local self-signed certificate (see [Troubleshooting](#windows-11-smart-app-control-sac--defender-blocks)).

---

### Step 2: Software Prerequisites & Toolchain

FaceGate is built using native C++20 and static runtime linkage to ensure zero external dependency when loaded inside `LogonUI.exe`.

Install the required developer tools:

1. **Git for Windows**:
   ```powershell
   winget install --id Git.Git -e --source winget
   ```

2. **Visual Studio 2022** (Community, Professional, or Enterprise):
   ```powershell
   winget install --id Microsoft.VisualStudio.2022.Community --override "--passive --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended"
   ```
   *Required workloads and components:*
   - **Desktop development with C++**
   - **MSVC v143 - VS 2022 C++ x64/x86 build tools**
   - **Windows 10 SDK (10.0.19041.0+) or Windows 11 SDK (10.0.22000.0+)**
   - **C++ CMake tools for Windows**

3. **CMake** (version 3.20 or newer):
   ```powershell
   winget install --id Kitware.CMake -e --source winget
   ```

---

### Step 3: Clone the Repository

Clone the project to your local workspace:

```powershell
git clone https://github.com/nyxri0f8/winface.git $env:USERPROFILE\dev\facegate
cd $env:USERPROFILE\dev\facegate
```

---

### Step 4: Verify Runtime Assets and Models

Verify that the required ONNX Runtime libraries and pre-trained neural network assets exist in your repository:

- `third_party/onnxruntime-win-x64-1.24.4/` (C++ headers and `onnxruntime.lib`)
- `models/runtime/`:
  - `face_detector.onnx` (MediaPipe SSD BlazeFace detector)
  - `face_landmarks_detector.onnx` (MediaPipe 478 3D landmark regressor)
  - `arcface_int8.onnx` (ArcFace quantized 512D biometric embedding model)
  - `fas_v1se_s4.0.onnx` and `fas_v2_s2.7.onnx` (MiniFASNet dual anti-spoofing models)
  - `canonical_face.bin` (3D reference face geometry)
  - `mesh_edges.bin` is only used by the Python bench previews; the lock-screen HUD no longer needs it
- `assets/`:
  - `tile.bmp`, `sfx_unlock.wav` (the only sound: played once when the face unlock succeeds)

Verify them with PowerShell:

```powershell
Get-ChildItem -Path models\runtime, third_party\onnxruntime-win-x64-1.24.4\lib
```

---

### Step 5: Build from Source

Generate the build system using CMake and compile the release binaries:

```powershell
# 1. Configure the build with Release profile
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# 2. Build all targets (FaceGateCP.dll, fgsetup.exe, fgcredtest.exe, fgsound.exe, fgcli.exe, fgoverlaytest.exe)
cmake --build build --config Release
```

The resulting binaries will be placed in `build\Release`:
- `FaceGateCP.dll`: The native Credential Provider loaded by `LogonUI.exe`
- `fgsetup.exe`: Administrative configuration and enrollment utility
- `fgcredtest.exe`: Non-destructive CredUI test harness
- `fgsound.exe`: Unlock sound player started by the credential provider (no window, no arguments)
- `fgcli.exe`: Offline self-test and verification utility
- `fgoverlaytest.exe`: Developer check of the lock-screen HUD, provider DLL and sound (not installed)
- `onnxruntime.dll`: Delay-loaded neural runtime

---

### Step 6: Run Engine Verification (Self-Test)

Before installing into the Windows authentication system, execute the mathematical self-test:

```powershell
.\build\Release\fgcli.exe selftest
```

This validates that the C++ pipeline reproduces MediaPipe landmarks, ArcFace cosine similarities, and MiniFASNet texture probabilities within strict tolerances against pre-computed test vectors. Ensure the command prints `PASS`.

*(Optional)* Check the lock-screen pieces without locking your PC:

```powershell
.\build\Release\fgoverlaytest.exe live                                # HUD hidden until a key press / recognised face
.\build\Release\fgoverlaytest.exe dll .\build\Release\FaceGateCP.dll  # loads the provider like LogonUI does
.\build\Release\fgoverlaytest.exe wav .\assets\sfx_unlock.wav         # sound format check (silent)
.\build\Release\fgoverlaytest.exe render $env:TEMP\hud                # renders the animation to PNG frames
```

`live` briefly shows the HUD at the top of your screen and simulates an F24 key press; every line should read `PASS`.

---

### Step 7: System Installation (Administrator PowerShell)

Open an **Administrator PowerShell** window (Right-click Start > Terminal (Admin) or PowerShell (Admin)), navigate to the repository directory, and run:

```powershell
powershell -ExecutionPolicy Bypass -File .\install\install.ps1
```

Or simply double-click `install\install.cmd` (it asks for administrator rights and keeps the window open so you can read the result).

Two dry-run options that need no admin rights and change nothing on the system:
- `.\install\install.ps1 -Check`: only the pre-flight checks.
- `.\install\install.ps1 -StageTo <folder>`: copies and hash-verifies everything into `<folder>` (no registry changes).

To **update** an existing install after pulling new code, rebuild (Step 5) and run the installer again, then restart once so `LogonUI.exe` loads the new DLL. Your faces, password and settings are kept.

**What this script does:**
0. Pre-flight: confirms every file is present, the DLL is x64 and newer than the source (otherwise: rebuild), the unlock sound is a PCM WAV, and the provider DLL loads and creates its COM object. If anything fails, nothing is changed.
1. Creates the production directory `C:\Program Files\FaceGate` and copies all binaries (including `fgsound.exe`), assets, models, and recovery documentation. Files left by older versions (`sfx_scan.wav`, `sfx_fail.wav`, `mesh_edges.bin`, a previously loaded DLL moved aside) are removed.
2. Creates the secure data directory `C:\ProgramData\FaceGate` with restricted Windows Access Control Lists (ACLs): Full Control for `SYSTEM` and `Administrators`, Read/Execute for standard `Users`.
3. Registers the COM InprocServer32 class `{C188DC15-E41E-4CCF-9DA9-8238E1D0BBDF}` in the Windows Registry (`HKLM\SOFTWARE\Classes\CLSID`).
4. Registers the provider in `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers`.
5. Sets `HKLM\SOFTWARE\FaceGate\Enabled = 0` on a fresh install (a **safe, disabled state** until enrollment and verification are complete). Re-running it to update keeps your faces, password and settings.
6. Verifies every installed file by hash and every registry value, printing `[ OK ]` / `[FAIL]` for each.

---

### Step 8: Enroll Your Face Profile

From the **same Administrator terminal**, enroll your primary face profile:

```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" enroll $env:USERNAME
```

The terminal will activate your camera and guide you through **5 distinct head poses**:
1. *Look straight at the camera*
2. *Slowly turn your head LEFT*
3. *Slowly turn your head RIGHT*
4. *Tilt your head UP a little*
5. *Tilt your head DOWN a little*

The system captures 15 sharp frames per pose (75 frames total) and computes an averaged, normalized 512-dimensional biometric template.

*(Optional)* You can enroll up to 3 distinct profiles (e.g. if you regularly wear glasses or want a secondary profile):

```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" enroll glasses
```

---

### Step 9: Store Windows Password in Hardware TPM

To allow Windows LSA to complete workstation unlocks automatically, store your account password:

```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" password
```

- Enter your Windows account password when prompted (input is hidden).
- The setup tool verifies the password with Windows via `LogonUser` before saving anything.
- An RSA-2048 key is generated in the motherboard TPM 2.0 via `MS_PLATFORM_CRYPTO_PROVIDER`.
- The key DACL is locked down to `NT AUTHORITY\SYSTEM` using SDDL (`D:P(A;;GA;;;SY)`).
- Plaintext buffers are wiped from RAM using `SecureZeroMemory`.

Verify that the stored credential matches your account:

```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" verify
```

---

### Step 10: Non-Destructive CredUI Test Mode (Recommended)

Before enabling the lock screen, test the full face authentication and password injection path without locking your PC:

1. Enable test mode:
   ```powershell
   & "C:\Program Files\FaceGate\fgsetup.exe" mode test
   ```

2. Open a **normal (non-admin) terminal** and launch the CredUI test harness:
   ```powershell
   & "C:\Program Files\FaceGate\fgcredtest.exe"
   ```

3. The standard "Windows Security" credential prompt will appear with the "Face unlock" tile.
4. Look at the camera and complete the head turn challenge.
5. The tool will verify that face recognition passed, the password was decrypted from the vault, and Windows accepted the logon credentials (`RESULT: Windows accepted the credential - face unlock path works`).

---

### Step 11: Activate Lock Screen Face Unlock

Once the CredUI test passes, activate full lock screen authentication from an Administrator terminal:

```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" mode lock
```

Now press `Win + L` to lock your workstation (restart once first if you just installed or updated):
1. The normal lock screen (date and time) appears, with nothing drawn over it. The camera is already looking.
2. Look at your camera. When it recognises you, the lock screen lifts by itself and the sign-in page appears with the FaceGate HUD at the top. (Pressing any key or clicking also lifts it.)
3. Turn your head slightly in the direction the chevrons point.
4. The brackets close into a circle, a check mark draws in, the unlock sound plays, and your desktop opens.

---

## Command-Line Reference

All configuration commands must be executed from an **Administrator PowerShell**.

### Check System Status
Displays the active operational mode, enrolled face count, password status, whitelisted camera ID, and runtime parameters:

```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" status
```

### Enroll a Face
Enrolls a face profile. The interactive terminal guides you through 5 head poses (Straight, Turn Left, Turn Right, Tilt Up, Tilt Down), capturing 15 frames per pose. You can enroll up to 3 distinct profiles (e.g., standard, with glasses, or secondary user):

```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" enroll <username>
```

To enroll a second profile:

```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" enroll glasses
```

### Delete a Face Profile
Removes a specific enrolled profile by name:

```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" remove glasses
```

### Store / Update Windows Password
Prompts for your Windows password (hidden input), validates it with the operating system via `LogonUser`, provisions an RSA-2048 key in the TPM, restricts the key to `SYSTEM`, and saves the encrypted payload:

```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" password
```

> **Note**: You must re-run this command whenever you change your Windows account password.

### Verify Password Vault
Tests whether the stored credential is valid with the Windows authentication authority without displaying it (does not require admin elevation):

```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" verify
```

### Operational Modes

Turn FaceGate completely off:
```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" mode off
```

Enable safe test mode (appears only in the CredUI Windows Security prompt, not on the lock screen):
```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" mode test
```

Enable full lock-screen authentication:
```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" mode lock
```

### Non-Destructive Live Test
Runs a full verification cycle (webcam feed, face tracking, liveness check, and head turn challenge) directly in your console without locking your computer:

```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" test
```

---

## Configuration Settings

Parameters can be adjusted in the registry using `fgsetup set <Parameter> <Value>`:

| Parameter | Default | Valid Range | Description |
| :--- | :---: | :---: | :--- |
| `SearchMs` | `7000` | `3000` - `15000` | Milliseconds to search for a matching face before timing out. |
| `ChallengeMs` | `3000` | `2000` - `6000` | Milliseconds allowed for the user to complete the head-turn challenge. |
| `MaxFails` | `3` | `1` - `5` | Consecutive failed recognitions before falling back exclusively to PIN/password. Scans that ran while the lock-screen picture was still up, or that never saw a face in range, do not count. |
| `Strictness` | `0` | `0` - `2` | Recognition threshold: `0` = Balanced (0.42), `1` = Strict (0.48), `2` = Relaxed (0.38). |
| `Sounds` | `1` | `0` or `1` | Play the unlock sound (`assets/sfx_unlock.wav`, via `fgsound.exe`) when the face unlock succeeds. There is no sound on scan start or rejection. |
| `Camera` | Auto | Device ID | Whitelist substring for the specific USB camera to use. |

Examples:

```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" set SearchMs 8000
& "C:\Program Files\FaceGate\fgsetup.exe" set Sounds 0
& "C:\Program Files\FaceGate\fgsetup.exe" set MaxFails 3
```

---

## Troubleshooting & Logs

To inspect authentication events, camera open timings, and failure reasons:

```powershell
Get-Content C:\ProgramData\FaceGate\log.txt -Tail 30
```

To list all detected video capture devices and verify USB hardware qualification:

```powershell
& "C:\Program Files\FaceGate\fgsetup.exe" cameras
```

Useful log lines:

| Log line | Meaning |
| :--- | :--- |
| `overlay revealed by face recognised behind the lock screen` | The camera recognised you and lifted the lock screen by itself. |
| `overlay revealed by user input` | A key press or click lifted the lock screen first. |
| `idle: waited behind the lock screen, no key pressed` | The lock screen stayed up for the whole search time; not counted as a failed attempt. |
| `identity lost during challenge (0.xx)` | The face dropped below the 0.34 floor twice during the head turn (usually turning too far or too fast). |
| `challenge not completed in time` | The head turn was not finished within `ChallengeMs`. |
| `fgsound.exe failed to start` | The sound fell back to in-process playback and may be cut short; re-run the installer. |

### The face HUD appears on top of the lock-screen picture
The lock screen did not react to FaceGate's automatic lift (a Shift tap plus a click in the empty top-left corner). Pressing any key still works. Please report it with the last 30 log lines.

### The unlock sound is cut off
The sound is played by `C:\Program Files\FaceGate\fgsound.exe`. If that file is missing, the provider plays the sound inside `LogonUI.exe`, which exits about a second after sign-in. Re-run the installer to restore it.

### Windows 11 Smart App Control (SAC) / Defender Blocks
If the FaceGate tile does not appear on the lock screen or if `fgsetup.exe` fails with an execution block error:
- Windows 11 Smart App Control (SAC) strictly blocks unsigned `.dll` and `.exe` binaries from loading into system processes like `LogonUI.exe`.
- **Option A (Recommended)**: Set Smart App Control to **Off** or **Evaluation** mode in **Windows Security > App & browser control > Smart App Control settings**.
- **Option B (Self-Signing)**: Generate a local code-signing certificate and sign the binaries locally:
  ```powershell
  $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=FaceGate Local" -CertStoreLocation "Cert:\CurrentUser\My"
  Export-Certificate -Cert $cert -FilePath "$env:TEMP\FaceGateLocal.cer"
  Import-Certificate -FilePath "$env:TEMP\FaceGateLocal.cer" -CertStoreLocation "Cert:\LocalMachine\Root"
  Set-AuthenticodeSignature -FilePath "C:\Program Files\FaceGate\FaceGateCP.dll" -Certificate $cert
  Set-AuthenticodeSignature -FilePath "C:\Program Files\FaceGate\fgsetup.exe" -Certificate $cert
  Set-AuthenticodeSignature -FilePath "C:\Program Files\FaceGate\fgsound.exe" -Certificate $cert
  ```

---

## Uninstallation

To completely remove FaceGate from the lock screen, delete the registered COM CLSID, and wipe all local face profiles, logs, and TPM keys:

```powershell
powershell -ExecutionPolicy Bypass -File .\install\uninstall.ps1
```

If Windows reports that `FaceGateCP.dll` is currently held open by `LogonUI.exe`, the uninstaller unregisters the provider immediately and marks the DLL for deletion. Restart the machine once to complete removal. Your Windows PIN and password will remain functional throughout the process.

---

## Emergency Recovery Procedure

If a configuration error or driver issue prevents the lock screen from behaving properly:

### Level 1: System is Booted and Accessible
If you can sign in using your PIN or password:
1. Open an Administrator terminal.
2. Disable FaceGate:
   ```powershell
   & "C:\Program Files\FaceGate\fgsetup.exe" mode off
   ```
3. Or run the uninstaller:
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\install\uninstall.ps1
   ```

### Level 2: Lock Screen Inaccessible (Recovery Environment)
1. On the Windows sign-in screen, hold **Shift** and select **Power > Restart**.
2. Select **Troubleshoot > Advanced options > Command Prompt**.
3. If BitLocker is enabled, enter your 48-digit BitLocker recovery key when prompted (or unlock via `manage-bde -unlock C: -RecoveryPassword YOUR-KEY`).
4. Identify your primary Windows drive (in recovery mode, Windows is often mounted on `D:`).
5. Load the offline registry hive and remove the FaceGate Credential Provider registration:
   ```cmd
   reg load HKLM\OFFSOFT C:\Windows\System32\config\SOFTWARE
   reg delete "HKLM\OFFSOFT\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{C188DC15-E41E-4CCF-9DA9-8238E1D0BBDF}" /f
   reg unload HKLM\OFFSOFT
   ```
6. Type `exit` and select **Continue** to boot into Windows. The default Windows sign-in screen will be restored.

---

## Credits

- **Unlock sound** (`assets/sfx_unlock.wav`): "Key Videogame SFX" by **mrstokes302**, from [Pixabay](https://pixabay.com/) (sound ID 423629), used under the [Pixabay Content License](https://pixabay.com/service/license-summary/). Converted from MP3 to 16-bit PCM WAV with the leading silence trimmed; otherwise unchanged.
- **Models**: MediaPipe Face Detector and Face Landmarker (Google), ArcFace, and MiniFASNet (Silent-Face-Anti-Spoofing) belong to their respective authors; see [License](#license).

---

## License

This project is licensed under the Apache License 2.0. Third-party neural network architectures and models (MediaPipe, ArcFace, MiniFASNet) belong to their respective authors and are subject to their respective licenses.
