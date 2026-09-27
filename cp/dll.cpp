// WinFace CP - DLL entry points, class factory, and safe loading of our own onnxruntime.dll.
#include <windows.h>
#include <initguid.h>

#include "common.h"   // defines CLSID_WinFaceProvider + credential provider GUIDs here (initguid)
#include <propkey.h>  // storage for PKEY_Identity_QualifiedUserName
#include <delayimp.h>

#pragma comment(lib, "shlwapi.lib")

namespace fgcp { HRESULT create_provider(REFIID riid, void** ppv); }
using namespace fgcp;

// onnxruntime.dll is delay-loaded. LogonUI lives in System32, which ships an OLDER onnxruntime.dll
// (used by Windows ML) - so we always load ours by absolute path from the install folder.
static FARPROC WINAPI delay_hook(unsigned notify, PDelayLoadInfo info) {
    if (notify == dliNotePreLoadLibrary && _stricmp(info->szDll, "onnxruntime.dll") == 0) {
        std::wstring p = module_dir() + L"\\onnxruntime.dll";
        return (FARPROC)LoadLibraryExW(p.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    return nullptr;
}
extern "C" const PfnDliHook __pfnDliNotifyHook2 = delay_hook;

class Factory : public IClassFactory {
public:
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    IFACEMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return r;
    }
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        return create_provider(riid, ppv);
    }
    IFACEMETHODIMP LockServer(BOOL lock) override {
        lock ? dll_addref() : dll_release();
        return S_OK;
    }

private:
    LONG refs_ = 1;
};

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}

STDAPI DllCanUnloadNow() { return g_dll_refs > 0 ? S_FALSE : S_OK; }

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
    *ppv = nullptr;
    if (clsid != CLSID_WinFaceProvider) return CLASS_E_CLASSNOTAVAILABLE;
    auto* f = new (std::nothrow) Factory();
    if (!f) return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}
