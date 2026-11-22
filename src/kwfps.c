// kwfps: timing probe (and later frame pacing) for Command & Conquer 3: Kane's Wrath.
//
// Loaded as a dinput8.dll proxy next to RetailExe\1.3\cnc3ep1.dat. It forwards
// DirectInput8Create to the system DLL and swaps a few vtable slots so it can
// time each part of a frame. Every second it writes a line to kwfps.log; any
// frame much slower than the target also gets its own line with a breakdown.
//
// Addresses are for the Steam 1.3 exe (same layout as the 4K Add-On's patched
// copy). If any slot doesn't hold the expected function, nothing is hooked.

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// ---------- dinput8 forwarding ----------

typedef HRESULT(WINAPI *DI8Create)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
static HMODULE real_dinput8;

static FARPROC real_proc(const char *name) {
    if (!real_dinput8) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        strcat(path, "\\dinput8.dll");
        real_dinput8 = LoadLibraryA(path);
    }
    return real_dinput8 ? GetProcAddress(real_dinput8, name) : NULL;
}

__declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD v, REFIID r, LPVOID *o, LPUNKNOWN u) {
    DI8Create f = (DI8Create)real_proc("DirectInput8Create");
    return f ? f(h, v, r, o, u) : E_FAIL;
}
__declspec(dllexport) HRESULT WINAPI DllCanUnloadNow(void) {
    FARPROC f = real_proc("DllCanUnloadNow");
    return f ? ((HRESULT(WINAPI *)(void))f)() : S_FALSE;
}
__declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID c, REFIID r, LPVOID *o) {
    FARPROC f = real_proc("DllGetClassObject");
    return f ? ((HRESULT(WINAPI *)(REFCLSID, REFIID, LPVOID *))f)(c, r, o) : E_FAIL;
}
__declspec(dllexport) HRESULT WINAPI DllRegisterServer(void) { return E_NOTIMPL; }
__declspec(dllexport) HRESULT WINAPI DllUnregisterServer(void) { return E_NOTIMPL; }

// ---------- engine addresses (Steam 1.3) ----------

#define VT_ENGINE   0x00aa4604u // Win32GameEngine
#define VT_LOGIC    0x00aa46bcu // W3DGameLogic
#define VT_CLIENT   0x00aa56b4u // W3DGameClient
#define VT_DISPLAY  0x00aa657cu // W3DDisplay

#define FN_ENGINE_UPDATE   0x007a5e45u // engine +0x14: whole frame (client, logic, draw)
#define FN_ENGINE_LOGIC    0x005c3b55u // engine +0x94: logic sub-step(s) for this frame
#define FN_ENGINE_CLIENT   0x005cb486u // engine +0x98: client side (UI, audio, GameClient)
#define FN_LOGIC_UPDATE    0x0055bbeau // logic  +0x34: GameLogic::update(sub 1..6)
#define FN_CLIENT_UPDATE   0x007b979fu // client +0x14: GameClient::update (includes draw)
#define FN_DISPLAY_DRAW    0x007f0dc6u // display +0x30: W3DDisplay::draw

#define G_LOGIC_FPS  ((volatile int *)0x00b7d4ccu)
#define G_MAX_FPS    ((volatile int *)0x00b7d4d0u)
#define G_GAMELOGIC  ((volatile uint8_t **)0x00bf7f98u)
#define G_GLOBALDATA ((volatile uint8_t **)0x00bfbf24u) // TheWritableGlobalData
#define GD_CAMERA_MIN_HEIGHT 0xcc // DefaultCameraMinHeight (float, stock 120)
#define GD_CAMERA_MAX_HEIGHT 0xd0 // DefaultCameraMaxHeight (float, stock 300)

#define GD_USE_FPS_LIMIT 0x52     // UseFPSLimit (bool); off = run as fast as possible
#define GD_MP_MONEY_MULT 0xf78    // MultiPlayMoneyMult: 8 floats, one per player count

static float camera_max_height; // 0 = leave the game's value
static float money_mult;        // 0 = leave the game's value
static int turbo_vk;            // virtual key that toggles turbo; 0 = none
static int turbo_on, turbo_key_was_down;

typedef void(__fastcall *Fn0)(void *self, void *edx);
typedef void(__fastcall *Fn1)(void *self, void *edx, int a);

static Fn0 o_engine_update, o_engine_client, o_client_update, o_display_draw;
static Fn1 o_engine_logic, o_logic_update;

// ---------- timing ----------

// D3D9 puts the x87 FPU into single precision, so this is built with SSE maths
// (-mfpmath=sse) and times are kept small by counting from a base tick.
static double qpc_ms;
static int64_t qpc_base;
static inline double now_ms(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - qpc_base) * qpc_ms;
}

typedef struct { double sum, max; int n; } Acc;
static inline void acc_add(Acc *a, double v) { a->sum += v; if (v > a->max) a->max = v; a->n++; }
static inline double acc_avg(const Acc *a) { return a->n ? a->sum / a->n : 0; }

// Per-second accumulators.
static Acc s_interval, s_update, s_client, s_clientupd, s_draw, s_logic, s_sub[7];
static int s_frames, s_spikes;
static double s_start, last_frame_start;

// Current frame's breakdown, for spike lines.
static double f_client, f_draw, f_logic, f_sub_ms[7];
static int f_sub_count;

static int depth_client; // GameClient::update can be re-entered; time only the outer call
static FILE *logf;
static double spike_ms = 25.0;

static void log_line(const char *fmt, ...) {
    if (!logf) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(logf, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fputc('\n', logf);
}

static int logic_frame(void) {
    const uint8_t *gl = (const uint8_t *)*G_GAMELOGIC;
    return gl ? *(const int *)(gl + 0x38) : -1;
}

static void flush_second(double t) {
    if (s_sub[1].n + s_sub[2].n + s_sub[3].n + s_sub[4].n + s_sub[5].n + s_sub[6].n > 0) {
        double secs = (t - s_start) / 1000.0;
        log_line("fps %5.1f | frame avg %5.2f max %6.2f | update %5.2f/%6.2f | client %5.2f/%6.2f"
                 " (gameclient %5.2f/%6.2f, draw %5.2f/%6.2f) | logic %5.2f/%6.2f"
                 " | sub1 %5.2f/%6.2f sub2 %5.2f/%6.2f sub3 %5.2f/%6.2f sub4 %5.2f/%6.2f sub5 %5.2f/%6.2f sub6 %5.2f/%6.2f"
                 " | logicsteps/s %d | spikes %d | lf %d",
                 s_frames / secs, acc_avg(&s_interval), s_interval.max, acc_avg(&s_update), s_update.max,
                 acc_avg(&s_client), s_client.max, acc_avg(&s_clientupd), s_clientupd.max, acc_avg(&s_draw), s_draw.max,
                 acc_avg(&s_logic), s_logic.max,
                 acc_avg(&s_sub[1]), s_sub[1].max, acc_avg(&s_sub[2]), s_sub[2].max, acc_avg(&s_sub[3]), s_sub[3].max,
                 acc_avg(&s_sub[4]), s_sub[4].max, acc_avg(&s_sub[5]), s_sub[5].max, acc_avg(&s_sub[6]), s_sub[6].max,
                 s_sub[1].n, s_spikes, logic_frame());
        fflush(logf);
    }
    memset(&s_interval, 0, sizeof s_interval);
    memset(&s_update, 0, sizeof s_update);
    memset(&s_client, 0, sizeof s_client);
    memset(&s_clientupd, 0, sizeof s_clientupd);
    memset(&s_draw, 0, sizeof s_draw);
    memset(&s_logic, 0, sizeof s_logic);
    memset(s_sub, 0, sizeof s_sub);
    s_frames = s_spikes = 0;
    s_start = t;
}

// ---------- hooks ----------

// Keeps GameData's camera ceiling at the configured height. Checked every frame
// because the INI loads after the DLL; the camera picks it up when a map loads.
static void apply_camera(void) {
    uint8_t *gd = (uint8_t *)*G_GLOBALDATA;
    if (!gd || camera_max_height <= 0) return;
    float *maxh = (float *)(gd + GD_CAMERA_MAX_HEIGHT);
    if (*maxh != camera_max_height) {
        log_line("camera max height %.0f -> %.0f (min %.0f)", *maxh, camera_max_height, *(float *)(gd + GD_CAMERA_MIN_HEIGHT));
        *maxh = camera_max_height;
    }
}

// Income multiplier for skirmish/multiplayer. Replays don't store it, so a
// replay must be played back with the same value or it will go out of sync.
static void apply_money(void) {
    uint8_t *gd = (uint8_t *)*G_GLOBALDATA;
    if (!gd || money_mult <= 0) return;
    float *m = (float *)(gd + GD_MP_MONEY_MULT);
    if (m[0] != money_mult) {
        log_line("money multiplier %.2f -> %.2f", m[0], money_mult);
        for (int i = 0; i < 8; i++) m[i] = money_mult;
    }
}

// Turbo: switch the frame limiter off so the simulation (one sub-step per
// frame) runs as fast as the PC can draw. Only reacts while the game has focus.
static void poll_turbo(void) {
    if (!turbo_vk) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    int down = pid == GetCurrentProcessId() && (GetAsyncKeyState(turbo_vk) & 0x8000);
    if (down && !turbo_key_was_down) {
        uint8_t *gd = (uint8_t *)*G_GLOBALDATA;
        if (gd) {
            turbo_on = !turbo_on;
            gd[GD_USE_FPS_LIMIT] = turbo_on ? 0 : 1;
            log_line("turbo %s", turbo_on ? "on" : "off");
        }
    }
    turbo_key_was_down = down;
}

static void __fastcall hk_engine_update(void *self, void *edx) {
    apply_camera();
    apply_money();
    poll_turbo();
    double t0 = now_ms();
    if (last_frame_start > 0) acc_add(&s_interval, t0 - last_frame_start);
    double interval = last_frame_start > 0 ? t0 - last_frame_start : 0;
    last_frame_start = t0;
    if (t0 - s_start >= 1000.0) flush_second(t0);

    f_client = f_draw = f_logic = 0;
    f_sub_count = 0;
    o_engine_update(self, edx);
    double dt = now_ms() - t0;
    acc_add(&s_update, dt);
    s_frames++;

    // A spike line describes the previous frame's gap plus this frame's work.
    if (interval > spike_ms && f_sub_count > 0 && logic_frame() > 0) {
        s_spikes++;
        char subs[96] = "";
        for (int i = 1; i <= 6; i++)
            if (f_sub_ms[i] > 0) {
                char b[16];
                snprintf(b, sizeof b, " s%d=%.1f", i, f_sub_ms[i]);
                strcat(subs, b);
            }
        log_line("SPIKE gap %.1f ms | this frame: update %.1f client %.1f draw %.1f logic %.1f |%s | lf %d",
                 interval, dt, f_client, f_draw, f_logic, subs, logic_frame());
    }
    memset(f_sub_ms, 0, sizeof f_sub_ms);
}

static void __fastcall hk_engine_client(void *self, void *edx) {
    double t0 = now_ms();
    o_engine_client(self, edx);
    f_client = now_ms() - t0;
    acc_add(&s_client, f_client);
}

static void __fastcall hk_engine_logic(void *self, void *edx, int sub) {
    double t0 = now_ms();
    o_engine_logic(self, edx, sub);
    f_logic = now_ms() - t0;
    acc_add(&s_logic, f_logic);
}

static void __fastcall hk_logic_update(void *self, void *edx, int sub) {
    double t0 = now_ms();
    o_logic_update(self, edx, sub);
    double dt = now_ms() - t0;
    if (sub >= 1 && sub <= 6) {
        acc_add(&s_sub[sub], dt);
        f_sub_ms[sub] += dt;
        f_sub_count++;
    }
}

static void __fastcall hk_client_update(void *self, void *edx) {
    if (depth_client++) { o_client_update(self, edx); depth_client--; return; }
    double t0 = now_ms();
    o_client_update(self, edx);
    acc_add(&s_clientupd, now_ms() - t0);
    depth_client--;
}

static void __fastcall hk_display_draw(void *self, void *edx) {
    double t0 = now_ms();
    o_display_draw(self, edx);
    double dt = now_ms() - t0;
    f_draw += dt;
    acc_add(&s_draw, dt);
}

// ---------- install ----------

typedef struct { uint32_t vt, off, expect; void *hook; void **orig; const char *name; } Slot;

static int install(void) {
    Slot slots[] = {
        {VT_ENGINE, 0x14, FN_ENGINE_UPDATE, (void *)hk_engine_update, (void **)&o_engine_update, "engine.update"},
        {VT_ENGINE, 0x94, FN_ENGINE_LOGIC, (void *)hk_engine_logic, (void **)&o_engine_logic, "engine.logic"},
        {VT_ENGINE, 0x98, FN_ENGINE_CLIENT, (void *)hk_engine_client, (void **)&o_engine_client, "engine.client"},
        {VT_LOGIC, 0x34, FN_LOGIC_UPDATE, (void *)hk_logic_update, (void **)&o_logic_update, "logic.update"},
        {VT_CLIENT, 0x14, FN_CLIENT_UPDATE, (void *)hk_client_update, (void **)&o_client_update, "client.update"},
        {VT_DISPLAY, 0x30, FN_DISPLAY_DRAW, (void *)hk_display_draw, (void **)&o_display_draw, "display.draw"},
    };
    int n = sizeof slots / sizeof slots[0];
    for (int i = 0; i < n; i++) {
        uint32_t *p = (uint32_t *)(slots[i].vt + slots[i].off);
        if (IsBadReadPtr(p, 4) || *p != slots[i].expect) {
            log_line("unexpected exe layout at %s (%08x holds %08x, expected %08x); not hooking",
                     slots[i].name, (unsigned)(uintptr_t)p, IsBadReadPtr(p, 4) ? 0 : *p, slots[i].expect);
            return 0;
        }
    }
    for (int i = 0; i < n; i++) {
        uint32_t *p = (uint32_t *)(slots[i].vt + slots[i].off);
        DWORD old;
        VirtualProtect(p, 4, PAGE_READWRITE, &old);
        *slots[i].orig = (void *)(uintptr_t)*p;
        *p = (uint32_t)(uintptr_t)slots[i].hook;
        VirtualProtect(p, 4, old, &old);
    }
    return 1;
}

static void open_log(HMODULE self) {
    char path[MAX_PATH];
    GetModuleFileNameA(self, path, MAX_PATH);
    char *slash = strrchr(path, '\\');
    if (slash) slash[1] = 0;
    char ini[MAX_PATH];
    snprintf(ini, sizeof ini, "%skwfps.ini", path);
    char buf[32];
    GetPrivateProfileStringA("probe", "SpikeMs", "25", buf, sizeof buf, ini);
    spike_ms = atof(buf);
    GetPrivateProfileStringA("camera", "MaxHeight", "0", buf, sizeof buf, ini);
    camera_max_height = (float)atof(buf);
    GetPrivateProfileStringA("cheats", "MoneyMult", "0", buf, sizeof buf, ini);
    money_mult = (float)atof(buf);
    turbo_vk = GetPrivateProfileIntA("keys", "TurboKey", 0x7A, ini); // F11
    strcat(path, "kwfps.log");
    logf = fopen(path, "a");
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        qpc_ms = 1000.0 / (double)f.QuadPart;
        LARGE_INTEGER b;
        QueryPerformanceCounter(&b);
        qpc_base = b.QuadPart;
        open_log(h);
        // Only touch engine addresses inside the game itself (cnc3ep1.dat at its
        // fixed base); anywhere else this is a plain dinput8 forwarder.
        char exe[MAX_PATH];
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        size_t len = strlen(exe);
        int is_game = len >= 11 && _stricmp(exe + len - 11, "cnc3ep1.dat") == 0 &&
                      (uintptr_t)GetModuleHandleA(NULL) == 0x400000u;
        if (!is_game) {
            log_line("kwfps loaded in %s: not the game, forwarding only", exe);
        } else {
            log_line("kwfps loaded; logic fps %d, max fps %d", *G_LOGIC_FPS, *G_MAX_FPS);
            if (install()) log_line("hooks installed");
        }
        s_start = now_ms();
    } else if (reason == DLL_PROCESS_DETACH && logf) {
        log_line("exit");
        fclose(logf);
    }
    return TRUE;
}
