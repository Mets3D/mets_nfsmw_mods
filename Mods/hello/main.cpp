/*
 * nfsmw_hello — first smoke test for the SDK on this machine's speed.exe.
 *
 *  1. Writes nfsmw_hello.log next to speed.exe: loader path, exe PE header,
 *     and the LIVE bytes at a few SDK function addresses (compare against
 *     the file on disk to confirm the addresses fit this exe build).
 *  2. Draws a bar in the top-left corner every frame, blinking green/red
 *     once a second, so it is obvious by eye that the D3D9 hook is alive.
 */

#include <nfsmw_sdk/nfsmw_sdk.h>
#include <MinHook.h>

#define NFSMW_D3D9_DEVICE_PTR 0x00982BDCu  /* IDirect3DDevice9** (checked: ~400 code refs) */

#include <cstdarg>
#include <cstdio>

NFSMW_PLUGIN_DECLARE("NFSMW Hello", "0.1.0", "Demeter")

namespace {

FILE *g_log = nullptr;

void log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_line(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

struct Probe { const char *name; unsigned addr; };

// Function entries taken from the SDK's sdk_addrs.json (all in .text).
const Probe kProbes[] = {
    {"IAttachable::GetIHandle",               0x402820},
    {"IVehicleAI::GetIHandle",                0x403700},
    {"IRacer::GetIHandle",                    0x403850},
    {"FEPlayerCarDB::GetCareerRecordByHandle", 0x56f120},
    {"GRaceStatus::DisableBarriers",          0x5dbf00},
    {"BeginGameFlowUnloadTrack",              0x667340},
};

struct D3DRECT { long x1, y1, x2, y2; };
constexpr unsigned long kClearTarget = 1;
constexpr int kVtPresent = 17, kVtBeginScene = 41, kVtEndScene = 42, kVtClear = 43;
using ClearFn = long (NFSMW_STDCALL *)(void *, unsigned long, const D3DRECT *,
                                       unsigned long, unsigned long, float,
                                       unsigned long);
using PresentFn = long (NFSMW_STDCALL *)(void *, const void *, const void *, void *, const void *);
using SceneFn = long (NFSMW_STDCALL *)(void *);

volatile unsigned g_begin = 0, g_end = 0, g_present = 0;
PresentFn g_orig_present = nullptr;
SceneFn g_orig_begin = nullptr, g_orig_end = nullptr;

long NFSMW_STDCALL hook_begin(void *self) { ++g_begin; return g_orig_begin(self); }
long NFSMW_STDCALL hook_end(void *self) { ++g_end; return g_orig_end(self); }

long NFSMW_STDCALL hook_present(void *self, const void *a, const void *b, void *c, const void *d) {
    const unsigned n = ++g_present;
    if (n == 1) log_line("first Present call, device=%p", self);
    // Blink once a second (assuming ~60 fps): proof by eye that the hook runs.
    auto clear = reinterpret_cast<ClearFn>((*static_cast<void ***>(self))[kVtClear]);
    const bool green = (n / 60) % 2 == 0;
    D3DRECT r = {20, 20, 260, 70};
    clear(self, 1, &r, kClearTarget, green ? 0xFF20C020u : 0xFFC02020u, 0.0f, 0);
    return g_orig_present(self, a, b, c, d);
}

// Poll the engine's device global independently of the SDK's worker so the log
// shows what is actually there: pointer, vtable, and who owns the EndScene slot.
DWORD WINAPI diag_thread(LPVOID) {
    void *dev = nullptr;
    while (!(dev = *reinterpret_cast<void **>(NFSMW_D3D9_DEVICE_PTR))) Sleep(50);
    void **vt = *static_cast<void ***>(dev);
    char mod[MAX_PATH] = "?";
    HMODULE hm = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(vt[kVtEndScene]), &hm))
        GetModuleFileNameA(hm, mod, sizeof(mod));
    log_line("[diag] device=%p vtable=%p EndScene=%p (in %s)", dev, static_cast<void *>(vt),
             vt[kVtEndScene], mod);

    // Vtable patches got silently reverted within a few frames in the first test, so hook
    // the d3d9.dll function bodies themselves (MinHook) instead of the device's vtable.
    int ok = (MH_Initialize() == MH_OK);
    ok += (MH_CreateHook(vt[kVtBeginScene], reinterpret_cast<void *>(&hook_begin),
                         reinterpret_cast<void **>(&g_orig_begin)) == MH_OK);
    ok += (MH_CreateHook(vt[kVtEndScene], reinterpret_cast<void *>(&hook_end),
                         reinterpret_cast<void **>(&g_orig_end)) == MH_OK);
    ok += (MH_CreateHook(vt[kVtPresent], reinterpret_cast<void *>(&hook_present),
                         reinterpret_cast<void **>(&g_orig_present)) == MH_OK);
    ok += (MH_EnableHook(MH_ALL_HOOKS) == MH_OK);
    log_line("[diag] MinHook steps ok: %d/5", ok);

    for (int i = 0; i < 360; ++i) {  // 30 minutes of 5 s samples
        Sleep(5000);
        void *cur = *reinterpret_cast<void **>(NFSMW_D3D9_DEVICE_PTR);
        void **curvt = cur ? *static_cast<void ***>(cur) : nullptr;
        log_line("[diag] t=%ds device=%p vtable=%p slots(B/E/P)=%p/%p/%p calls B=%u E=%u P=%u",
                 (i + 1) * 5, cur, static_cast<void *>(curvt),
                 curvt ? curvt[kVtBeginScene] : nullptr, curvt ? curvt[kVtEndScene] : nullptr,
                 curvt ? curvt[kVtPresent] : nullptr, g_begin, g_end, g_present);
    }
    return 0;
}

}  // namespace

NFSMW_PLUGIN_MAIN() {
    g_log = fopen("nfsmw_hello.log", "w");
    log_line("nfsmw_hello 0.1.0 starting");

    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, sizeof(path));
    log_line("host exe: %s", path);

    auto *base = reinterpret_cast<unsigned char *>(GetModuleHandleA(nullptr));
    unsigned nt = *reinterpret_cast<unsigned *>(base + 0x3C);
    log_line("image base %p, PE timestamp 0x%08x", static_cast<void *>(base),
             *reinterpret_cast<unsigned *>(base + nt + 8));

    for (const Probe &p : kProbes) {
        const auto *b = reinterpret_cast<const unsigned char *>(p.addr);
        log_line("%-40s %#x : %02x %02x %02x %02x %02x %02x %02x %02x", p.name,
                 p.addr, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
    }

    if (HANDLE h = CreateThread(nullptr, 0, diag_thread, nullptr, 0, nullptr)) CloseHandle(h);
    log_line("waiting for the D3D9 device");
    return NFSMW_OK;
}
