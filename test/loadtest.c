// Console load test: loads the proxy with error dialogs suppressed and checks
// that DirectInput8Create forwards to the system dinput8.
#include <windows.h>
#include <stdio.h>

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc < 2) { printf("usage: loadtest <dll>\n"); return 2; }
    HMODULE m = LoadLibraryA(argv[1]);
    if (!m) { printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }
    FARPROC f = GetProcAddress(m, "DirectInput8Create");
    printf("loaded; DirectInput8Create=%p\n", (void *)f);
    if (!f) return 1;
    // IID_IDirectInput8A
    static const GUID iid = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
    void *di = NULL;
    HRESULT hr = ((HRESULT(WINAPI *)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN))f)(GetModuleHandleA(NULL), 0x0800, &iid, &di, NULL);
    printf("DirectInput8Create hr=0x%08lx obj=%p\n", (unsigned long)hr, di);
    if (di) ((IUnknown *)di)->lpVtbl->Release((IUnknown *)di);
    FreeLibrary(m);
    return hr == 0 ? 0 : 1;
}
