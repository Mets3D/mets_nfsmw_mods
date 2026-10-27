#include "events.h"

#include "hooks.h"
#include "split_screen_support.h"

namespace split_screen_support::events {

namespace {

struct List {
    Callback callbacks[8];
    int count = 0;
    void add(Callback c) {
        if (count < 8) callbacks[count++] = c;
    }
    void call() const {
        for (int i = 0; i < count; ++i) callbacks[i]();
    }
};
List g_world_load, g_frame;

template <typename T, int N = 6>
struct Callbacks {
    T callbacks[N];
    int count = 0;
    void add(T c) {
        if (c && count < N) callbacks[count++] = c;
    }
};
Callbacks<HudFilter> g_hud_filter;
Callbacks<HudMade> g_hud_made;
Callbacks<HudDestroy> g_hud_destroy;
Callbacks<HudUpdate> g_hud_before, g_hud_after;

void (*g_orig_begin_world_load)();
void hook_begin_world_load() {
    g_world_load.call();
    g_orig_begin_world_load();
}

void (*g_orig_display_frame)();
void hook_display_frame() {
    g_frame.call();
    g_orig_display_frame();
}

void *(__fastcall *g_orig_hud)(void *, void *, int, const char *, void *, int);
void *__fastcall hook_hud(void *self, void *edx, int type, const char *package, void *player, int number) {
    for (int i = 0; i < g_hud_filter.count; ++i) g_hud_filter.callbacks[i](type, package);
    void *result = g_orig_hud(self, edx, type, package, player, number);
    for (int i = 0; i < g_hud_made.count; ++i) g_hud_made.callbacks[i](self, type, package, player, number);
    return result;
}

void (__fastcall *g_orig_hud_dtor)(void *, void *);
void __fastcall hook_hud_dtor(void *self, void *edx) {
    for (int i = 0; i < g_hud_destroy.count; ++i) g_hud_destroy.callbacks[i](self);
    g_orig_hud_dtor(self, edx);
}

void (__fastcall *g_orig_hud_update)(void *, void *, void *, float);
void __fastcall hook_hud_update(void *self, void *edx, void *player, float dt) {
    for (int i = 0; i < g_hud_before.count; ++i) g_hud_before.callbacks[i](self, player);
    g_orig_hud_update(self, edx, player, dt);
    for (int i = 0; i < g_hud_after.count; ++i) g_hud_after.callbacks[i](self, player);
}

}  // namespace

void on_world_load(Callback callback) { g_world_load.add(callback); }
void on_frame(Callback callback) { g_frame.add(callback); }
void on_hud_construct(HudFilter callback) { g_hud_filter.add(callback); }
void on_hud_made(HudMade callback) { g_hud_made.add(callback); }
void on_hud_destroy(HudDestroy callback) { g_hud_destroy.add(callback); }
void on_hud_update(HudUpdate before, HudUpdate after) {
    g_hud_before.add(before);
    g_hud_after.add(after);
}

void install() {
    nfsmw::Hook hooks[5];
    size_t count = 0;
    if (g_world_load.count)
        hooks[count++] = {"worldload", "BeginWorldLoad", 0x662950, {0x6A, 0xFF, 0x68, 0xB4, 0xA2, 0x87, 0x00}, false,
                          nfsmw::detour(&hook_begin_world_load), nfsmw::orig(&g_orig_begin_world_load)};
    if (g_frame.count)
        hooks[count++] = {"frame", "eDisplayFrame", 0x6E7220, {0xA0, 0xC8, 0x1A, 0x90, 0x00, 0x83, 0xEC}, false,
                          nfsmw::detour(&hook_display_frame), nfsmw::orig(&g_orig_display_frame)};
    if (g_hud_filter.count || g_hud_made.count)
        hooks[count++] = {"hud", "FEngHud::FEngHud", 0x5A6600, {0x6A, 0xFF, 0x68, 0xB6, 0x39, 0x87, 0x00}, true, nfsmw::detour(&hook_hud), nfsmw::orig(&g_orig_hud)};
    if (g_hud_destroy.count)
        hooks[count++] = {"hud", "FEngHud::~FEngHud", 0x5A0910, {0x6A, 0xFF, 0x68, 0x9A, 0x34, 0x87, 0x00}, true, nfsmw::detour(&hook_hud_dtor),
                          nfsmw::orig(&g_orig_hud_dtor)};
    if (g_hud_before.count || g_hud_after.count)
        hooks[count++] = {"hud", "FEngHud::Update", 0x58CA30, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, true, nfsmw::detour(&hook_hud_update),
                          nfsmw::orig(&g_orig_hud_update)};
    if (count) nfsmw::install_hooks(hooks, count, g_log, nullptr, "events");
}

}  // namespace split_screen_support::events
