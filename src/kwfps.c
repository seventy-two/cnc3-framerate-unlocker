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
static int pacing_on, client_rate, frame_cap; // [pacing] in kwfps.ini

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
        // Replay playback paces itself to real time (0x5af051) and stalls if
        // pushed faster, so turbo is for live matches only.
        uint8_t *rec = *(uint8_t **)0x00bfbf4cu; // TheRecorder; mode 1 = playback
        if (!turbo_on && rec && *(int *)(rec + 0x18) == 1) {
            log_line("turbo isn't available while playing a replay");
            turbo_key_was_down = down;
            return;
        }
        uint8_t *gd = (uint8_t *)*G_GLOBALDATA;
        if (gd) {
            turbo_on = !turbo_on;
            if (!pacing_on) gd[GD_USE_FPS_LIMIT] = turbo_on ? 0 : 1;
            log_line("turbo %s", turbo_on ? "on" : "off");
        }
    }
    turbo_key_was_down = down;
}

// ---------- pacing: real-time simulation steps, drawing at any frame rate ----------
//
// Stock: each drawn frame runs a fixed number of the logic frame's 6 sub-steps,
// so game speed is tied to frame rate. Pacing replaces GameEngine::update
// (0x5b0f9d, called from 0x7a5e48) with a version that runs sub-steps on a
// real-time clock (6 x logic fps = 90 a second) and draws every frame, with a
// continuous interpolation value. The engine's "max fps" (client frames per
// second, used for client timings) is set to ClientRate in DllMain.

#define CALL_ENGINE_UPDATE 0x007a5e48u // e8 rel32 -> 0x5b0f9d
#define FN_SCRIPT_PRE      0x00577a8du // thiscall TheScriptEngine
#define FN_SCRIPT_PAUSED   0x0055e4e2u // thiscall TheScriptEngine, returns bool
#define FN_DEBUG_SETTIME   0x004f60f7u // cdecl(int)
#define FN_SET_ALPHA       0x005b0f6du // thiscall engine: +0x48 = sub/6
#define G_C24C0C     ((void **)0x00c24c0cu)
#define G_SCRIPTENG  ((void **)0x00bf9ff8u)
#define G_GAMECLIENT ((uint8_t **)0x00bf45c8u)
#define G_DEBUG      ((void **)0x00bdafacu)
#define G_XPIPE      ((void **)0x00c02650u)
#define G_NET_SPEED  ((volatile float *)0x00b7a634u) // network game-speed factor, 1.0 normally
#define PATCH_DIV6   0x0055c5b3u // GameLogic::update: push 6/pop eax/cdq/idiv ecx -> mov eax,1
#define PATCH_WINDOW 0x004bf073u // Drawable interpolation window (imm8 in cmp eax,6)

typedef char(__fastcall *FnC0)(void *self, void *edx);
typedef void(__cdecl *FnCdeclI)(int);
#define VFN(obj, off) (*(void **)(*(uintptr_t *)(obj) + (off)))

static double pace_acc, pace_last;
static int trace_left = -1; // >0: frames of pacing trace still to log ([pacing] Trace)

static void engine_substep(uint8_t *e) {
    int *E = (int *)e;
    uint8_t *client = *G_GAMECLIENT;
    if (E[0x10] == 6 && e[0x4c]) {
        e[0x4c] = 0;
        E[0x11] = *G_MAX_FPS / *G_LOGIC_FPS;
    }
    E[0x10]++;
    ((FnCdeclI)FN_DEBUG_SETTIME)(*(int *)(*G_GAMELOGIC + 0x38) * 10 - 1 + E[0x10]);
    ((Fn0)FN_SET_ALPHA)(e, 0);
    if (E[0x10] <= 6) {
        ((Fn1)VFN(e, 0x94))(e, 0, E[0x10]);
        return;
    }
    if (E[0x14] % 25 == 0) { // the engine's logic-rate statistic (+0x58)
        DWORD t = timeGetTime();
        double el = (double)(DWORD)(t - (DWORD)E[0x15]);
        ((float *)e)[0x16] = (float)(E[0x14] / (el * 0.001));
        E[0x14] = 0;
        E[0x15] = (int)timeGetTime();
    }
    int old = E[0x10];
    E[0x14]++;
    E[0x10] = 1;
    ((Fn0)FN_SET_ALPHA)(e, 0);
    ((Fn1)VFN(e, 0x94))(e, 0, 1);
    if (client[0xd4] == 0) { // logic couldn't start a new frame (network wait)
        E[0x10] = old;
        ((Fn0)FN_SET_ALPHA)(e, 0);
    } else {
        e[0x4c] = 1;
    }
}

static void __fastcall my_engine_update(void *self, void *edx) {
    (void)edx;
    uint8_t *e = (uint8_t *)self;
    void *x = *G_C24C0C;
    ((Fn0)VFN(x, 0x14))(x, 0);
    void *se = *G_SCRIPTENG;
    ((Fn0)FN_SCRIPT_PRE)(se, 0);
    char paused = ((FnC0)FN_SCRIPT_PAUSED)(se, 0);
    uint8_t *client = *G_GAMECLIENT;
    double t = now_ms();
    double dt = pace_last > 0 ? t - pace_last : 0;
    pace_last = t;
    if (paused) {
        pace_acc = 0;
        ((Fn0)VFN(e, 0x98))(e, 0);
        client[0xd4] = 0;
        ((Fn0)VFN(*G_DEBUG, 0x98))(*G_DEBUG, 0);
        return;
    }
    double rate = 6.0 * *G_LOGIC_FPS * (*G_NET_SPEED > 0 ? *G_NET_SPEED : 1.0f); // sub-steps a second
    if (turbo_on) rate *= 50;
    pace_acc += dt * rate / 1000.0;
    int max_steps = turbo_on ? 60 : 6; // below 15 fps the game slows rather than spiralling
    int n = (int)pace_acc;
    if (n > max_steps) { n = max_steps; pace_acc = n; }
    pace_acc -= n;
    client[0xd4] = 1;
    for (int i = 0; i < n; i++) engine_substep(e);
    // Continuous interpolation: (sub - 1 + fraction) / 6 runs 0..1 across a
    // logic frame and meets itself at the boundary (one sub-step of latency).
    float a = (float)((*(int *)(e + 0x40) - 1 + pace_acc) / 6.0);
    *(float *)(e + 0x48) = a < 0 ? 0 : a > 1 ? 1 : a;
    if (n == 0) client[0xd4] = 1; // advance the client frame every drawn frame
    if (trace_left > 0 && logic_frame() > 300) {
        trace_left--;
        log_line("TRACE dt %.2f steps %d sub %d acc %.3f alpha %.4f lf %d", dt, n, *(int *)(e + 0x40), pace_acc,
                 *(float *)(e + 0x48), logic_frame());
    }
    ((Fn0)VFN(e, 0x98))(e, 0);   // client update and draw
    ((Fn0)VFN(*G_DEBUG, 0x98))(*G_DEBUG, 0);
    ((Fn0)VFN(*G_XPIPE, 0x14))(*G_XPIPE, 0);
}

// Precise frame cap (the engine's own limiter is off in pacing mode).
static double cap_next;
static void cap_wait(void) {
    if (!pacing_on || frame_cap <= 0 || turbo_on) { cap_next = 0; return; }
    double period = 1000.0 / frame_cap, t = now_ms();
    if (cap_next == 0 || t - cap_next > period * 2) cap_next = t;
    while ((t = now_ms()) < cap_next - 1.5) Sleep(1);
    while (now_ms() < cap_next) YieldProcessor();
    cap_next += period;
}

static int patch_bytes(uint32_t addr, const uint8_t *expect, const uint8_t *with, size_t n, const char *name) {
    if (memcmp((void *)(uintptr_t)addr, expect, n) != 0) {
        log_line("patch %s: unexpected bytes at %08x; skipped", name, addr);
        return 0;
    }
    DWORD old;
    VirtualProtect((void *)(uintptr_t)addr, n, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)(uintptr_t)addr, with, n);
    VirtualProtect((void *)(uintptr_t)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)addr, n);
    return 1;
}

// Applied in DllMain, before the exe's startup code reads the max fps.
static void install_pacing(void) {
    if (pacing_on) {
        int old = *G_MAX_FPS;
        DWORD prot;
        VirtualProtect((void *)G_MAX_FPS, 4, PAGE_READWRITE, &prot);
        *G_MAX_FPS = client_rate;
        VirtualProtect((void *)G_MAX_FPS, 4, prot, &prot);
        static const uint8_t div_old[] = {0x6a, 0x06, 0x8b, 0xc8, 0x58, 0x99, 0xf7, 0xf9};
        static const uint8_t div_new[] = {0xb8, 0x01, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90};
        int ok = patch_bytes(PATCH_DIV6, div_old, div_new, 8, "logic 6/ratio");
        uint8_t call_old[5] = {0xe8}, call_new[5] = {0xe8};
        int32_t rel_old = (int32_t)(0x005b0f9du - (CALL_ENGINE_UPDATE + 5));
        int32_t rel_new = (int32_t)((uintptr_t)my_engine_update - (CALL_ENGINE_UPDATE + 5));
        memcpy(call_old + 1, &rel_old, 4);
        memcpy(call_new + 1, &rel_new, 4);
        if (ok) ok = patch_bytes(CALL_ENGINE_UPDATE, call_old, call_new, 5, "engine update");
        if (!ok) { // put the constant back so the stock loop stays safe
            VirtualProtect((void *)G_MAX_FPS, 4, PAGE_READWRITE, &prot);
            *G_MAX_FPS = old;
            VirtualProtect((void *)G_MAX_FPS, 4, prot, &prot);
            pacing_on = 0;
        }
        log_line("pacing %s: max fps %d -> %d, frame cap %d", pacing_on ? "on" : "FAILED", old, *G_MAX_FPS, frame_cap);
        if (pacing_on) timeBeginPeriod(1);
    }
    // Drawable interpolation window: stock 6 client frames = 3 logic frames at
    // 30 fps. Keep it at 3 logic frames for the actual client rate, or units
    // freeze short of their stop point and then jump.
    int win = 3 * *G_MAX_FPS / *G_LOGIC_FPS;
    if (win > 127) win = 127;
    static const uint8_t w_old[] = {0x06};
    uint8_t w_new[] = {(uint8_t)win};
    if (win != 6 && patch_bytes(PATCH_WINDOW, w_old, w_new, 1, "interpolation window"))
        log_line("interpolation window 6 -> %d client frames", win);
}

static void __fastcall hk_engine_update(void *self, void *edx) {
    apply_camera();
    apply_money();
    poll_turbo();
    if (pacing_on && *G_GLOBALDATA) (*G_GLOBALDATA)[GD_USE_FPS_LIMIT] = 0;
    cap_wait();
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
    if (interval > spike_ms && logic_frame() > 0) {
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
    // In turbo, draw (and present, which waits for vsync) only every 16th frame
    // so the simulation isn't held to the refresh rate.
    static unsigned turbo_frame;
    if (turbo_on && (++turbo_frame & 15)) return;
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
    pacing_on = GetPrivateProfileIntA("pacing", "Enabled", 0, ini);
    client_rate = GetPrivateProfileIntA("pacing", "ClientRate", 165, ini);
    frame_cap = GetPrivateProfileIntA("pacing", "FrameCap", 162, ini);
    trace_left = GetPrivateProfileIntA("pacing", "Trace", 0, ini);
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
            install_pacing();
        }
        s_start = now_ms();
    } else if (reason == DLL_PROCESS_DETACH && logf) {
        log_line("exit");
        fclose(logf);
    }
    return TRUE;
}
