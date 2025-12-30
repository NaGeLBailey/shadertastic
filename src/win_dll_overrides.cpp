#ifdef _WIN32
#include "win_dll_overrides.h"

FARPROC WINAPI delay_load_hook(const unsigned dliNotify, PDelayLoadInfo pdli) {
    if (dliNotify == dliNotePreLoadLibrary) {
        const HMODULE h = LoadLibraryA("shadertastic_lib\\onnxruntime.dll");
        return reinterpret_cast<FARPROC>(h);
    }
    return nullptr;
}

ExternC const PfnDliHook __pfnDliNotifyHook2 = delay_load_hook;
#endif
