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

}  // namespace

void on_world_load(Callback callback) { g_world_load.add(callback); }
void on_frame(Callback callback) { g_frame.add(callback); }

void install() {
    nfsmw::Hook hooks[2];
    size_t count = 0;
    if (g_world_load.count)
        hooks[count++] = {"worldload", "BeginWorldLoad", 0x662950, {0x6A, 0xFF, 0x68, 0xB4, 0xA2, 0x87, 0x00}, false,
                          nfsmw::detour(&hook_begin_world_load), nfsmw::orig(&g_orig_begin_world_load)};
    if (g_frame.count)
        hooks[count++] = {"frame", "eDisplayFrame", 0x6E7220, {0xA0, 0xC8, 0x1A, 0x90, 0x00, 0x83, 0xEC}, false,
                          nfsmw::detour(&hook_display_frame), nfsmw::orig(&g_orig_display_frame)};
    if (count) nfsmw::install_hooks(hooks, count, g_log, g_ini, "events");
}

}  // namespace split_screen_support::events
