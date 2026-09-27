// Plays the unlock sound to the end. The credential provider starts this as its own process because LogonUI
// exits a moment after a successful sign-in and would cut an in-process sound off after ~1 s.
// Deliberately takes no arguments: it runs as SYSTEM, so it only ever plays assets\sfx_unlock.wav next to itself.
// Sound credit: "Key Videogame SFX" by mrstokes302 (Pixabay, ID 423629) - see README, Credits.
#include <windows.h>
#include <mmsystem.h>
#include <shlwapi.h>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shlwapi.lib")

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t p[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, p, MAX_PATH);
    if (n == 0 || n >= MAX_PATH || !PathRemoveFileSpecW(p) || !PathAppendW(p, L"assets\\sfx_unlock.wav")) return 1;
    return PlaySoundW(p, nullptr, SND_FILENAME | SND_SYNC | SND_NODEFAULT) ? 0 : 1;
}
